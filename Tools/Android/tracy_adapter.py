#!/usr/bin/env python3
"""Import bounded Perfetto CPU thread slices through the official pinned Tracy CLI."""
import argparse
import json
from pathlib import Path
import sys
from profiling_tools import DEFAULT_CACHE, query, run, sha256, tool, write_json


def import_trace(trace, output, cache, max_slices, name_prefix=None, pid=None):
    """Convert thread-local slices only; retain Perfetto as authoritative system data."""
    if output.exists() or output.with_suffix(".chrome.json").exists():
        raise ValueError("Refusing to overwrite imported evidence")
    if not 1 <= max_slices <= 250000:
        raise ValueError("max-slices must be between 1 and 250000")
    filters = ["s.dur>=0"]
    if name_prefix is not None:
        if not name_prefix or len(name_prefix) > 256 or "\x00" in name_prefix:
            raise ValueError("name-prefix must contain 1 to 256 non-NUL characters")
        # Literal prefix rather than LIKE/GLOB: wildcard characters stay literal.
        literal = name_prefix.replace("'", "''")
        filters.append("substr(s.name,1," + str(len(name_prefix)) + ")='" + literal + "'")
    if pid is not None:
        if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 0:
            raise ValueError("pid must be a positive integer")
        filters.append("p.pid=" + str(pid))
    sql = """SELECT s.id,s.ts,s.dur,s.name,t.tid,p.pid,t.name AS thread_name
             FROM slice s JOIN thread_track tt ON tt.id=s.track_id
             JOIN thread t ON t.utid=tt.utid LEFT JOIN process p ON p.upid=t.upid
             WHERE """ + " AND ".join(filters) + " ORDER BY s.ts,s.id"
    data = query(trace, sql, cache, max_slices)
    if data["truncated"]:
        raise ValueError("Slice limit exceeded; select --name-prefix and/or --pid instead of silently truncating")
    if not data["rows"]:
        raise ValueError("No complete thread slices; cannot establish Tracy import compatibility")
    events = []
    threads = set()
    origin = min(int(r["ts"]) for r in data["rows"])
    for row in data["rows"]:
        tid = int(row["tid"])
        row_pid = int(row["pid"]) if row["pid"].isdigit() else 0
        # Android/Linux tids are system-wide; retain process IDs in metadata.
        identity = (row_pid, tid)
        if identity not in threads:
            threads.add(identity)
            events.append({"ph": "M", "name": "thread_name", "pid": row_pid, "tid": tid,
                           "args": {"name": str(row["thread_name"]) + " pid=" + str(row_pid)}})
        events.append({"ph": "X", "name": row["name"], "pid": row_pid, "tid": tid,
                       "ts": (int(row["ts"]) - origin) / 1000, "dur": int(row["dur"]) / 1000,
                       "args": {"perfettoSliceId": row["id"]}})
    chrome = output.with_suffix(".chrome.json")
    write_json(chrome, {"traceEvents": events, "displayTimeUnit": "ns"})
    result = run([tool(cache, "tracy"), chrome, output], timeout=120)
    report = {"schemaVersion": 1, "tracyVersion": "0.14.1", "sourceTraceSha256": sha256(trace),
              "chromeSha256": sha256(chrome), "sourceOriginNs": origin, "completeSlices": len(data["rows"]),
              "selection": {"namePrefix": name_prefix, "pid": pid, "maxSlices": max_slices, "sql": sql,
                            "scope": "All complete thread slices" if name_prefix is None and pid is None else "Explicit filtered subset; authoritative trace retained"},
              "conversion": result, "available": result.get("exitCode") == 0 and output.is_file(),
              "limitations": ["Offline CPU thread slices only; no native live Tracy connection.",
                              "Original Perfetto retains scheduler, FrameTimeline, clocks, counters and IDs; import does not preserve their semantics.",
                              "Relative CPU times converted ns to Chrome microseconds; no GPU clock alignment or physical-display attribution."]}
    if report["available"]:
        report["tracySha256"] = sha256(output)
        report["tracyBytes"] = output.stat().st_size
    write_json(output.with_suffix(".import.json"), report)
    if not report["available"]:
        raise RuntimeError(json.dumps(report))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("version", "probe", "import"))
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-slices", type=int, default=250000)
    parser.add_argument("--name-prefix", help="Literal slice-name prefix; default imports all complete thread slices")
    parser.add_argument("--pid", type=int, help="Select this process ID; combined with name-prefix when provided")
    args = parser.parse_args()
    try:
        if args.action == "import":
            if not args.trace or not args.output:
                parser.error("import requires --trace and --output")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            report = import_trace(args.trace, args.output, args.cache, args.max_slices, args.name_prefix, args.pid)
        else:
            report = run([tool(args.cache, "tracy")])
            report["expectedVersion"] = "0.14.1"
            report["versionMatches"] = "0.14.1" in report.get("stdout", "") + report.get("stderr", "")
            if args.output:
                write_json(args.output, report)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
