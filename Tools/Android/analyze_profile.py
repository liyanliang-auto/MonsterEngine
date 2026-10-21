#!/usr/bin/env python3
"""Analyze preserved four-scenario frames, profiling stages, memory and trace loss."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import sys
import summarize_viewer_measurements as measurements
from profiling_tools import DEFAULT_CACHE, query, sha256, write_json


def distribution(values):
    """Use the established consumer's distribution and missing-value semantics."""
    return measurements.distribution(values, len(values))


def profile_objects(value):
    """Find explicit schema-1 profiling exports without guessing frame schemas."""
    if isinstance(value, dict):
        if isinstance(value.get("profilingRawJSON"), str):
            # org.json parses uint64 > INT64_MAX as Double. Only the original
            # native JSON string preserves signed VkResult encoding exactly.
            exact = json.loads(value["profilingRawJSON"])
            if exact.get("schemaVersion") != 1 or not isinstance(exact.get("events"), list):
                raise ValueError("Invalid exact profiling export")
            yield exact
            for key, child in value.items():
                if key not in ("profiling", "profilingRawJSON"):
                    yield from profile_objects(child)
            return
        if value.get("schemaVersion") == 1 and isinstance(value.get("events"), list):
            yield value
        else:
            for child in value.values():
                yield from profile_objects(child)


def event_summary(events):
    """Pair same-thread scopes; missing ends, identities and gaps remain explicit."""
    stacks = defaultdict(list)
    durations = defaultdict(list)
    counts = Counter()
    values = defaultdict(Counter)
    arrival = defaultdict(list)
    unmatched_ends = Counter()
    uncertain_pairs = Counter()
    sequences = defaultdict(set)
    for event in events:
        sequences[event.get("captureEpoch", 0)].add(event["eventSequence"])
    sequence_ranks = {epoch: {sequence: rank for rank, sequence in enumerate(sorted(observed))}
                      for epoch, observed in sequences.items()}
    request_times = {}
    requested_to_owner = []
    unique_ids = {k: set() for k in ("requestId", "frameId", "submissionId", "sceneId", "pageId")}
    for event in events:
        name, phase = event["name"], event["phase"]
        counts[name + ":" + phase] += 1
        for identity, observed in unique_ids.items():
            if event.get(identity, 0):
                observed.add(event[identity])
        # Scope retains this full identity through its lifetime. A missing end
        # must not consume a different request/page's begin after ring loss.
        # value is intentionally excluded: SetValue updates the end result.
        key = (event.get("captureEpoch", 0), event["threadId"], name,
               *(event.get(identity, 0) for identity in unique_ids))
        if phase == "B":
            stacks[key].append(event)
        elif phase == "E":
            if stacks[key]:
                beginning = stacks[key][-1]
                ranks = sequence_ranks[key[0]]
                begin_sequence, end_sequence = beginning["eventSequence"], event["eventSequence"]
                complete = (end_sequence > begin_sequence and
                            end_sequence - begin_sequence == ranks[end_sequence] - ranks[begin_sequence])
                if not complete:
                    # With no span ID, missing E1+B2 can masquerade as B1+E2
                    # even for identical context. Keep both boundaries unmatched.
                    uncertain_pairs[name] += 1
                    unmatched_ends[name] += 1
                elif event["timestampNs"] >= beginning["timestampNs"]:
                    stacks[key].pop()
                    durations[name].append((event["timestampNs"] - beginning["timestampNs"]) / 1e6)
            else:
                unmatched_ends[name] += 1
            value = int(event.get("value", 0))
            values[name][str(value - (1 << 64) if value >= (1 << 63) else value)] += 1
        if phase in ("I", "B"):
            arrival[(event.get("captureEpoch", 0), name)].append(event["timestampNs"])
        if name == "frame.request_received" and event.get("requestId"):
            request_times[(event.get("captureEpoch", 0), event["requestId"])] = event["timestampNs"]
        if name == "owner.render" and phase == "B" and event.get("requestId"):
            origin = request_times.get((event.get("captureEpoch", 0), event["requestId"]))
            if origin is not None and event["timestampNs"] >= origin:
                requested_to_owner.append((event["timestampNs"] - origin) / 1e6)
    interarrival = defaultdict(list)
    for (_, name), times in arrival.items():
        interarrival[name].extend((b - a) / 1e6 for a, b in zip(times, times[1:]) if b >= a)
    return {"eventCounts": dict(counts), "scopeMilliseconds": {k: distribution(v) for k, v in durations.items()},
            "outcomeValues": {k: dict(v) for k, v in values.items()},
            "eventInterarrivalMilliseconds": {k: distribution(v) for k, v in interarrival.items()},
            "requestReceivedToOwnerRenderMilliseconds": distribution(requested_to_owner),
            "unmatchedScopeEnds": dict(unmatched_ends),
            "uncertainScopePairsAcrossSequenceLoss": dict(uncertain_pairs),
            "unmatchedScopeBegins": dict(sum((Counter({k[2]: len(v)}) for k, v in stacks.items() if v), Counter())),
            "observedNonzeroIdentityCounts": {k: len(v) for k, v in unique_ids.items()},
            "limits": "All native durations use one CLOCK_MONOTONIC domain. Window clipping creates unmatched boundary pairs. GPU is asynchronous; IDs zero are unavailable."}


def analyze(directory, cache):
    """Read one owner session and preserve raw evidence hashes and availability."""
    capture = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
    metadata = json.loads((directory / "harness/metadata.json").read_text(encoding="utf-8"))
    if not capture.get("completed") or not metadata.get("captureCompleted"):
        raise ValueError("Cannot report an incomplete capture as completed")
    raw = directory / "harness/raw"
    # Profiling rows supplement the existing frame schema, increasing raw JSON size.
    measurements.MAX_TOTAL_BYTES = 1024 << 20
    rows, sources, losses, records, duplicates, identities = measurements.collect(raw, capture["captureSession"])
    events = {}
    exports = []
    for path in sorted(raw.glob("*.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        for export in profile_objects(document):
            if len(export["events"]) > 16384:
                raise ValueError("Profiling export exceeds native ring capacity")
            exports.append({"source": path.name, **{k: v for k, v in export.items() if k != "events"}})
            for event in export["events"]:
                if event.get("clockDomain") != "CLOCK_MONOTONIC":
                    raise ValueError("Native profiling clock is not the expected Android MONOTONIC domain")
                key = (event.get("captureEpoch", 0), event["eventSequence"])
                if key in events and events[key] != event:
                    raise ValueError("Conflicting profiling event identity")
                events[key] = event
                if len(events) > 1000000:
                    raise ValueError("Profiling unique-event bound exceeded")
    ordered = sorted(events.values(), key=lambda e: (e["timestampNs"], e["eventSequence"]))
    pages = defaultdict(list)
    for event in ordered:
        if event.get("pageId") and event["name"].startswith("Stream/"):
            pages[(event.get("sceneId", 0), event["pageId"])].append(event)
    page_summary = []
    for (scene, page), observations in sorted(pages.items()):
        page_summary.append({"sceneId": scene, "pageId": page,
                             "firstMonotonicNs": observations[0]["timestampNs"],
                             "lastMonotonicNs": observations[-1]["timestampNs"],
                             "events": dict(Counter(e["name"] + ":" + e["phase"] for e in observations)),
                             "requestIds": sorted({e["requestId"] for e in observations if e.get("requestId")}),
                             "frameIds": sorted({e["frameId"] for e in observations if e.get("frameId")}),
                             "submissionIds": sorted({e["submissionId"] for e in observations if e.get("submissionId")}),
                             "correlation": "Only each raw event's explicit identity is causal. Page/window grouping does not imply all listed requests, frames and submissions caused every event."})
    sequences = defaultdict(list)
    for event in ordered:
        sequences[event.get("captureEpoch", 0)].append(event["eventSequence"])
    gaps = {str(k): sum(max(0, b-a-1) for a, b in zip(sorted(v), sorted(v)[1:])) for k, v in sequences.items()}
    scenarios = {}
    for window in metadata["scenarios"]:
        start, end = window["startOwnerNanoseconds"], window["endOwnerNanoseconds"]
        if not isinstance(start, int) or not isinstance(end, int) or not 0 < start < end:
            raise ValueError("Invalid scenario monotonic boundaries")
        selected = [r for r in rows if start <= r["ownerStartNanoseconds"] < end]
        stage_events = [e for e in ordered if start <= e["timestampNs"] < end]
        scenarios[window["name"]] = {"window": window, "frames": measurements.summarize(selected),
                                     "profiling": event_summary(stage_events)}
    memory = []
    for path in sorted((directory / "memory").glob("sample-*.json")):
        item = json.loads(path.read_text(encoding="utf-8"))
        pss = re.search(r"TOTAL PSS:\s*(\d+)", item.get("stdout", ""))
        rss = re.search(r"TOTAL RSS:\s*(\d+)", item.get("stdout", ""))
        memory.append({"source": path.name, "sha256": sha256(path), "exitCode": item.get("exitCode"),
                       "pssKiB": int(pss[1]) if pss and item.get("exitCode") == 0 else None,
                       "rssKiB": int(rss[1]) if rss and item.get("exitCode") == 0 else None,
                       "hostPollDurationMilliseconds": (item["hostEndMonotonicNs"]-item["hostStartMonotonicNs"])/1e6})
    trace = directory / "system.pftrace"
    trace_queries = {}
    if trace.exists():
        queries = {
            "lossStats": "SELECT name,idx,severity,source,value FROM stats WHERE value!=0 ORDER BY name,idx",
            "clockAlignment": "SELECT * FROM clock_snapshot LIMIT 256",
            "meScopeStages": "SELECT name,count(*) AS samples,avg(dur)/1e6 AS mean_ms,max(dur)/1e6 AS max_ms FROM slice WHERE name GLOB 'ME/*' AND dur>=0 GROUP BY name ORDER BY name",
            "frameTimelineTables": "SELECT name FROM sqlite_master WHERE name LIKE '%frame_timeline%' ORDER BY name",
            "memoryCounters": "SELECT DISTINCT name FROM counter_track WHERE name LIKE '%mem%' OR name LIKE '%rss%' OR name LIKE '%gpu%' ORDER BY name"}
        for name, sql in queries.items():
            try:
                trace_queries[name] = {"sql": sql, "result": query(trace, sql, cache, 10000)}
            except (OSError, ValueError, RuntimeError) as error:
                trace_queries[name] = {"sql": sql, "status": "unavailable", "error": str(error)}
    return {"schemaVersion": 1, "capture": capture, "identities": identities, "scenarios": scenarios,
            "frameRows": len(rows), "rawFrameObservations": records, "duplicatesMerged": duplicates,
            "rawSources": sources, "nativeFrameRetention": losses,
            "profiling": {"uniqueEvents": len(events), "observedSequenceGapsByEpoch": gaps,
                          "exportObservations": exports, "summary": event_summary(ordered), "pages": page_summary},
            "memory": {"samples": memory, "pssKiB": distribution([x["pssKiB"] for x in memory if x["pssKiB"] is not None]),
                       "rssKiB": distribution([x["rssKiB"] for x in memory if x["rssKiB"] is not None]),
                       "gpu": capture["gpuMemory"], "scenarioCorrelation": "Host polling clock is not aligned to device; process samples apply to full capture only."},
            "traceQueries": trace_queries,
            "limits": ["CPU owner cadence and GPU Pipeline overlap asynchronously; their difference is not an attributable idle duration.",
                       "Native profile MONOTONIC and trace processor BOOTTIME require measured clock_snapshot mapping before temporal joins.",
                       "TextureView FrameTimeline cannot be joined to Vulkan submission tokens without an observed buffer/token mapping.",
                       "PSS/RSS and managed Vulkan bytes are different memory scopes; unavailable driver GPU memory is not zero.",
                       "Scope pairing excludes unmatched begin/end; loss counters are cumulative, not summed across exports.",
                       "Tools-off retains frame ring/readback/export and harness polling; comparison is combined diagnostic overhead, not isolated tracing cost.",
                       "Physical phone, actual 16 KiB runtime, visual threshold and startup splatSmoke failure remain outside this tool report."]}


def compare(current, baseline):
    """Require matched package/config/settings before calculating paired cadence deltas."""
    identity_fields = ("apkSha256", "manifestSha256", "serial", "package", "apkMembers", "buildIdentitySha256")
    mismatches = [k for k in identity_fields if current["capture"].get(k) != baseline["capture"].get(k)]
    if current["identities"] != baseline["identities"]:
        mismatches.append("native build/config/content identities")
    if current["capture"]["mode"] != "diagnostic" or baseline["capture"]["mode"] != "off":
        mismatches.append("expected diagnostic current and tools-off baseline")
    if mismatches:
        return {"status": "unmatched", "mismatches": mismatches, "overheadClaim": "unavailable"}
    deltas = {}
    for name, value in current["scenarios"].items():
        prior = baseline["scenarios"].get(name)
        if not prior:
            raise ValueError("Missing baseline scenario: " + name)
        before = prior["frames"]["metrics"]["cpuFrameMilliseconds"]["mean"]
        after = value["frames"]["metrics"]["cpuFrameMilliseconds"]["mean"]
        deltas[name] = {"offMeanFrameIntervalMs": before, "diagnosticMeanFrameIntervalMs": after,
                        "observedDifferenceMs": after-before if before is not None and after is not None else None}
    return {"status": "identity-matched", "scenarios": deltas,
            "interpretation": "One paired run includes run-order/cache/scheduler variance plus tracing/events/memory polling. This is observed combined difference, not isolated causal profiler cost."}


def markdown(report):
    """Render a concise reviewable summary while detailed evidence stays in JSON."""
    lines = ["# MonsterEngine profiling evidence", "", "Mode: " + report["capture"]["mode"], "",
             "| Scenario | Frames | CPU interval mean ms | GPU Pipeline mean ms | Request-to-owner mean ms |", "| --- | ---: | ---: | ---: | ---: |"]
    for name, scenario in report["scenarios"].items():
        metrics = scenario["frames"]
        values = [metrics["metrics"]["cpuFrameMilliseconds"]["mean"], metrics["gpu"]["Pipeline"]["milliseconds"]["mean"],
                  scenario["profiling"]["requestReceivedToOwnerRenderMilliseconds"]["mean"]]
        lines.append("| " + name + " | " + str(metrics["rows"]) + " | " + " | ".join("unavailable" if v is None else "%.3f" % v for v in values) + " |")
    lines += ["", "Unique profiling events: " + str(report["profiling"]["uniqueEvents"]), "", "PSS/RSS samples: " + str(len(report["memory"]["samples"])), ""]
    if "comparison" in report:
        lines += ["Matched comparison: " + report["comparison"]["status"], ""]
    lines.extend("- " + item for item in report["limits"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--baseline", type=Path, help="Previously analyzed tools-off report JSON")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    args = parser.parse_args()
    try:
        if args.output.resolve().is_relative_to((args.input / "harness/raw").resolve()):
            raise ValueError("Output must remain outside raw evidence")
        report = analyze(args.input, args.cache)
        if args.baseline:
            baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
            report["comparison"] = compare(report, baseline)
        write_json(args.output, report)
        args.output.with_suffix(".md").write_text(markdown(report), encoding="utf-8")
        print(markdown(report))
        return 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print("Profiling analysis failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
