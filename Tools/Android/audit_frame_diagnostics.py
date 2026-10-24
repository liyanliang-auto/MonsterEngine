#!/usr/bin/env python3
"""Reconcile preserved P0 windows and audit native/system timing without acquisition."""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import json
from pathlib import Path
import sys
import profiling_tools
import summarize_viewer_measurements as measurements

MAX_JSON_BYTES = 16 << 20
MAX_INPUT_BYTES = 2 << 30
MAX_EVENTS = 1000000
MAX_ANALYSIS_EVENTS = 200000
MAX_ANALYSIS_OBSERVATIONS = 262144
MAX_LIFECYCLE_CHAINS = 100000
MAX_LIFECYCLE_REPORT_BYTES = 256 << 20
INLINE_HISTORY_LIMIT = 32
MAX_RAW_LIFECYCLE_EVENT_OBSERVATIONS = 1000000
CONTEXT = ('requestId', 'frameId', 'submissionId', 'sceneId', 'pageId')
VK_RESULTS = {'vulkan.acquire', 'vulkan.queue_submit', 'vulkan.queue_present',
              'vulkan.frame_fence', 'vulkan.image_fence', 'vulkan.present_fence',
              'vulkan.present_fence_reset', 'vulkan.cancel_submit', 'vulkan.submit_status'}


def read_document(path):
    """Read one bounded exact-integer JSON document, rejecting duplicate keys."""
    with Path(path).open('rb') as stream:
        raw = stream.read(MAX_JSON_BYTES + 1)
    if len(raw) > MAX_JSON_BYTES:
        raise ValueError('JSON input exceeds byte bound: ' + str(path))
    return measurements.load_json(raw.decode('utf-8-sig'))


def source_identity(path):
    """Hash an existing artifact in place; timestamps are not experiment identity."""
    path = Path(path).resolve(strict=True)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': profiling_tools.sha256(path)}


def native_union(directory):
    """Reconstruct overlapping exact native rings by epoch and sequence, not sums."""
    events, exports, sources = {}, [], []
    total_bytes = raw_count = duplicates = inexact = 0
    files = sorted(Path(directory).glob('*.json'))
    if not files or len(files) > measurements.MAX_FILES:
        raise ValueError('Missing or excessive native snapshot files')
    for path in files:
        source = source_identity(path)
        total_bytes += source['bytes']
        if total_bytes > MAX_INPUT_BYTES:
            raise ValueError('Native input byte bound exceeded')
        sources.append(source)
        document = read_document(path)
        payload = document.get('viewer', document)
        payload = payload.get('payload', payload)
        # Kotlin's parsed profiling object is not authoritative for uint64.
        exact = payload.get('profilingRawJSON')
        if exact is None:
            if payload.get('profiling', {}).get('enabled'):
                inexact += 1
            continue
        export = measurements.load_json(exact)
        if export.get('schemaVersion') != 1 or export.get('clockDomain') != 'CLOCK_MONOTONIC':
            raise ValueError('Unsupported native profiling schema/clock')
        incoming = export.get('events')
        if not isinstance(incoming, list) or len(incoming) > 16384:
            raise ValueError('Invalid native event ring')
        epoch = export.get('captureEpoch')
        if type(epoch) is not int or epoch <= 0:
            raise ValueError('Missing native capture epoch')
        sequences = set()
        for event in incoming:
            for key in ('eventSequence', 'timestampNs', 'threadId', 'captureEpoch', *CONTEXT):
                if type(event.get(key)) is not int or not 0 <= event[key] < (1 << 64):
                    raise ValueError('Inexact/invalid native integer: ' + key)
            if (event['captureEpoch'] != epoch or event['eventSequence'] <= 0 or
                    event.get('clockDomain') != 'CLOCK_MONOTONIC' or
                    event.get('phase') not in ('B', 'E', 'I') or not isinstance(event.get('name'), str)):
                raise ValueError('Invalid native event identity')
            key = (epoch, event['eventSequence'])
            if key in events:
                if events[key] != event:
                    raise ValueError('Conflicting native sequence identity')
                duplicates += 1
            events[key] = event
            sequences.add(event['eventSequence'])
            if len(events) > MAX_EVENTS:
                raise ValueError('Native unique-event bound exceeded')
        raw_count += len(incoming)
        exports.append({'source': str(path.resolve()), 'captureSequence': document.get('captureSequence'),
                        **{k: v for k, v in export.items() if k != 'events'},
                        'eventCount': len(incoming), 'sequences': sequences})
    ordered = sorted(events.values(), key=lambda e: (e['captureEpoch'], e['eventSequence']))
    epochs = defaultdict(list)
    for event in ordered:
        epochs[event['captureEpoch']].append(event)
    losses = []
    for epoch, rows in epochs.items():
        observations = [e for e in exports if e['captureEpoch'] == epoch]
        if any(type(e['captureSequence']) is not int for e in observations):
            raise ValueError('Missing exact snapshot capture sequence')
        observations.sort(key=lambda e: e['captureSequence'])
        gaps = [[a['eventSequence'] + 1, b['eventSequence'] - 1]
                for a, b in zip(rows, rows[1:]) if b['eventSequence'] > a['eventSequence'] + 1]
        links = []
        for old, new in zip(observations, observations[1:]):
            links.append({'from': old['source'], 'to': new['source'],
                          'overlappingEvents': len(old['sequences'] & new['sequences']),
                          'droppedCounterDelta': new['droppedEvents'] - old['droppedEvents'],
                          'overwrittenCounterDelta': new['overwrittenEvents'] - old['overwrittenEvents']})
        counters = {}
        for field in ('droppedEvents', 'overwrittenEvents', 'recordCalls'):
            series = [e[field] for e in observations]
            if any(type(v) is not int or v < 0 for v in series):
                raise ValueError('Invalid cumulative profiler counter')
            resets = sum(b < a for a, b in zip(series, series[1:]))
            counters[field] = {'first': series[0], 'last': series[-1],
                               'delta': series[-1] - series[0] if not resets else None,
                               'decreases': resets, 'sumAcrossSnapshots': 'not_applicable'}
        losses.append({'captureEpoch': epoch, 'uniqueEvents': len(rows),
                       'firstMonotonicNs': min(r['timestampNs'] for r in rows),
                       'lastMonotonicNs': max(r['timestampNs'] for r in rows),
                       'firstSequence': rows[0]['eventSequence'], 'lastSequence': rows[-1]['eventSequence'],
                       'irrecoverableInternalSequenceGaps': gaps,
                       'missingInternalSequenceCount': sum(b - a + 1 for a, b in gaps),
                       'prefixNotObservedThroughSequence': rows[0]['eventSequence'] - 1,
                       'cumulativeCounters': counters, 'snapshotLinks': links,
                       'interpretation': 'Overwrites are ring replacement operations, not lost union events. Internal gaps are unrecovered by all preserved snapshots; origin (contention/drop/eviction) is not inferred. Prefix and post-final tail coverage are not proved.'})
    return ordered, sources, {'rawEventObservations': raw_count, 'duplicatesMerged': duplicates,
                             'uniqueEvents': len(ordered), 'inexactEnabledSnapshotsExcluded': inexact,
                             'ringExportObservations': [{k: v for k, v in e.items() if k != 'sequences'} for e in exports],
                             'epochs': losses}


def pair_intervals(events):
    """Pair scopes and request latency only through gap-free exact context."""
    ranks, observed = {}, defaultdict(list)
    for event in events:
        observed[event['captureEpoch']].append(event['eventSequence'])
    for epoch, sequences in observed.items():
        ranks[epoch] = {s: i for i, s in enumerate(sorted(sequences))}

    def continuous(a, b):
        return (a['captureEpoch'] == b['captureEpoch'] and b['eventSequence'] > a['eventSequence'] and
                b['timestampNs'] >= a['timestampNs'] and
                b['eventSequence'] - a['eventSequence'] ==
                ranks[a['captureEpoch']][b['eventSequence']] - ranks[a['captureEpoch']][a['eventSequence']])

    stack, requests = defaultdict(list), defaultdict(list)
    intervals, latencies = [], []
    unmatched, rejected, outcomes = Counter(), Counter(), defaultdict(Counter)
    for event in events:
        epoch, name, phase = event['captureEpoch'], event['name'], event['phase']
        key = (epoch, event['threadId'], name, *(event[k] for k in CONTEXT))
        if phase == 'B':
            stack[key].append(event)
        elif phase == 'E':
            if stack[key]:
                begin = stack[key].pop()
                if continuous(begin, event):
                    value = event.get('value')
                    if type(value) is int and name in VK_RESULTS and value >= 1 << 63:
                        value -= 1 << 64
                    intervals.append({'name': name, 'captureEpoch': epoch, 'threadId': event['threadId'],
                                      'beginSequence': begin['eventSequence'], 'endSequence': event['eventSequence'],
                                      'startMonotonicNs': begin['timestampNs'], 'endMonotonicNs': event['timestampNs'],
                                      'milliseconds': (event['timestampNs'] - begin['timestampNs']) / 1e6,
                                      'endValue': value,
                                      'identity': {k: event[k] for k in CONTEXT}})
                else:
                    rejected[name] += 1
            else:
                unmatched[name] += 1
        if phase in ('E', 'I'):
            value = event.get('value')
            if type(value) is int:
                # Byte/count payloads are unsigned; decode ONLY documented VkResult scopes.
                if name in VK_RESULTS and value >= 1 << 63:
                    value -= 1 << 64
                outcomes[name][str(value)] += 1
        if name == 'frame.request_received' and event['requestId'] > 0:
            requests[(epoch, event['requestId'])].append(event)
        if name == 'owner.render' and phase == 'B' and event['requestId'] > 0:
            candidates = requests[(epoch, event['requestId'])]
            # Request thread may differ; other available causal IDs must agree.
            compatible = [r for r in candidates if all(not r[k] or not event[k] or r[k] == event[k]
                                                       for k in ('frameId', 'submissionId', 'sceneId', 'pageId'))]
            if len(compatible) == 1 and continuous(compatible[0], event):
                origin = compatible[0]
                latencies.append({'captureEpoch': epoch, 'requestId': event['requestId'],
                                  'beginSequence': origin['eventSequence'], 'endSequence': event['eventSequence'],
                                  'startMonotonicNs': origin['timestampNs'], 'endMonotonicNs': event['timestampNs'],
                                  'milliseconds': (event['timestampNs'] - origin['timestampNs']) / 1e6})
            else:
                rejected['request_received_to_owner_render'] += 1
    durations = defaultdict(list)
    for interval in intervals:
        durations[interval['name']].append(interval['milliseconds'])
    return {'scopes': intervals, 'requestToOwner': latencies,
            'eventCounts': dict(Counter(e['name'] + ':' + e['phase'] for e in events)),
            'scopeMilliseconds': {k: measurements.distribution(v, len(v)) for k, v in durations.items()},
            'requestToOwnerMilliseconds': measurements.distribution([r['milliseconds'] for r in latencies], len(latencies)),
            'unmatchedEnds': dict(unmatched), 'rejectedAmbiguousOrGapPairs': dict(rejected),
            'unmatchedBegins': dict(sum((Counter({key[2]: len(rows)}) for key, rows in stack.items()), Counter())),
            'outcomeValues': {k: dict(v) for k, v in outcomes.items()},
            'outcomeInterpretation': 'Only allowlisted VkResult values are signed. vulkan.submit_dispatch is boolean; submit_status may be initialized VK_NOT_READY without any attempted vkQueueSubmit. Inspect queue_submit boundaries and submitted token together; no failure count is inferred from status alone.',
            'joinRule': 'Exact capture epoch/thread/name/request/frame/submission/scene/page context for scopes; unique same-epoch request cause with compatible explicit IDs for request-to-owner; no known union sequence gap. Zero IDs are unavailable. No cross-frame GPU subtraction.'}


def owner_attempts(intervals, events):
    """Bound complete owner attempts using disjoint contained same-clock CPU scopes."""
    owners = [s for s in intervals if s['name'] == 'owner.render']
    relevant = {'vulkan.frame_fence', 'vulkan.image_fence', 'vulkan.present_fence',
                'vulkan.acquire', 'vulkan.queue_submit', 'vulkan.queue_present'}
    child_index, outcome_index = defaultdict(list), defaultdict(list)
    for scope in intervals:
        if scope['name'] in relevant:
            child_index[(scope['captureEpoch'], scope['threadId'], scope['identity']['requestId'])].append(scope)
    for event in events:
        if event['name'] in (relevant | {'vulkan.submit_status', 'vulkan.submitted', 'frame.submitted'}) and event['phase'] in ('I', 'E'):
            outcome_index[(event['captureEpoch'], event['threadId'], event['requestId'])].append(event)
    attempts = []
    for owner in owners:
        request = owner['identity']['requestId']
        if not request:
            continue
        key = (owner['captureEpoch'], owner['threadId'], request)
        children = [s for s in child_index[key] if
                    s['startMonotonicNs'] >= owner['startMonotonicNs'] and s['endMonotonicNs'] <= owner['endMonotonicNs']
                    and (not owner['identity']['sceneId'] or s['identity']['sceneId'] == owner['identity']['sceneId'])]
        spans = sorted((s['startMonotonicNs'], s['endMonotonicNs']) for s in children)
        union = []
        for start, end in spans:
            if union and start <= union[-1][1]:
                union[-1][1] = max(union[-1][1], end)
            else:
                union.append([start, end])
        covered = sum(end - start for start, end in union)
        explicit = [e for e in outcome_index[key] if owner['beginSequence'] <= e['eventSequence'] <= owner['endSequence']]
        outcomes = []
        for e in explicit:
            value = e.get('value')
            if type(value) is int and e['name'] in VK_RESULTS and value >= 1 << 63:
                value -= 1 << 64
            outcomes.append({'sequence': e['eventSequence'], 'name': e['name'], 'value': value,
                             'identity': {k: e[k] for k in CONTEXT}})
        attempts.append({'captureEpoch': owner['captureEpoch'], 'requestId': request, 'threadId': owner['threadId'],
                         'ownerBeginSequence': owner['beginSequence'], 'ownerEndSequence': owner['endSequence'],
                         'ownerMilliseconds': owner['milliseconds'],
                         'observedFenceAcquireSubmitPresentUnionMilliseconds': covered / 1e6,
                         'otherOwnerMilliseconds': owner['milliseconds'] - covered / 1e6,
                         'childScopes': children, 'outcomes': outcomes,
                         'interpretation': 'Other owner time is unassigned CPU wall time, not GPU idle or scheduler attribution. Child causal frame/token IDs remain explicit; containment does not invent missing generations. Failed/unsubmitted attempts remain included.'})
    return {'completeAttempts': len(attempts), 'attempts': attempts,
            'otherOwnerMilliseconds': measurements.distribution([a['otherOwnerMilliseconds'] for a in attempts], len(attempts)),
            'observedWaitCallUnionMilliseconds': measurements.distribution([a['observedFenceAcquireSubmitPresentUnionMilliseconds'] for a in attempts], len(attempts)),
            'limitation': 'Only owners with a complete gap-free scope are eligible; loss can bias this subset. Complete scope time is host wall time and does not identify why the call blocked.'}


def collect_frames(directory, session):
    """Use the established exact schema consumer with a bounded large capture allowance."""
    previous = measurements.MAX_TOTAL_BYTES
    try:
        measurements.MAX_TOTAL_BYTES = MAX_INPUT_BYTES
        return measurements.collect(directory, session, True)
    finally:
        measurements.MAX_TOTAL_BYTES = previous


def reconcile_windows(previous):
    """Reconstruct all baseline frames; reported window counts are comparison only."""
    gate_path = previous / 'logs/p0-verification-01/p0-gate.json'
    gate = read_document(gate_path)
    historical = gate.get('baseline', {}).get('windows', [])
    if len(historical) != 12:
        raise ValueError('Expected twelve preserved independent baseline windows')
    results, sources, seen = [], [source_identity(gate_path)], set()
    for round_index in (1, 2, 3):
        directory = previous / ('captures/p0-verification-01/round' + str(round_index))
        context_path = directory / 'harness/metadata.json'
        context = read_document(context_path)
        capture_path = directory / 'capture.json'
        capture = read_document(capture_path)
        if capture.get('completed') is not True or capture.get('captureSession') != context.get('captureSession'):
            raise ValueError('Historical capture incomplete/session mismatch')
        sources += [source_identity(context_path), source_identity(capture_path)]
        for window in context['scenarios']:
            key = (round_index, window['name'])
            if key in seen:
                raise ValueError('Duplicate historical round/scenario')
            seen.add(key)
            matches = [w for w in historical if (w['roundIndex'], w['scenario']) == key]
            if len(matches) != 1 or window.get('roundIndex') != round_index:
                raise ValueError('Historical window round mismatch')
            raw = directory / 'harness/raw'
            if (raw / window['name']).is_dir():
                raw = raw / window['name']
            rows, raw_sources, _, _, _, _, observations = collect_frames(raw, context['captureSession'])
            sources += raw_sources
            actual = measurements.window_acceptance(rows, window, context, observations)
            expected = matches[0]['acceptance']
            comparable = ('status', 'reasons', 'thresholdMisses', 'successfulFrameCount', 'fullWindowSeconds', 'windowFramesPerSecond')
            mismatch = [k for k in comparable if actual[k] != expected.get(k)]
            for metric in ('p95', 'p99'):
                if actual['cpuStartIntervalMilliseconds'][metric] != expected.get('cpuStartIntervalMilliseconds', {}).get(metric):
                    mismatch.append('cpuStartIntervalMilliseconds.' + metric)
            results.append({'roundIndex': round_index, 'scenario': window['name'],
                            'captureSession': context['captureSession'], 'metadata': window,
                            'reconciliation': 'mismatch' if mismatch else 'pass', 'mismatchFields': mismatch,
                            'acceptance': actual})
    if len(results) != 12 or len(seen) != 12:
        raise ValueError('Incomplete historical window reconstruction')
    return results, sources


def trace_audit(directory, processor, windows):
    """Query pinned old trace; only bracketed measured clock mappings enable joins."""
    trace = directory / 'system.pftrace'
    if not trace.is_file():
        return {'status': 'unavailable', 'reason': 'missing_system_trace', 'queries': {}}, []
    sources = [source_identity(trace)]
    if processor is None:
        return {'status': 'unavailable', 'reason': 'trace_processor_not_explicitly_selected', 'queries': {}}, sources
    processor = processor.resolve(strict=True)
    if profiling_tools.sha256(processor) != profiling_tools.lock_data()['perfetto']['sha256']:
        raise ValueError('Trace processor does not match existing pinned tool')
    if processor.name != profiling_tools.lock_data()['perfetto']['file']:
        raise ValueError('Trace processor must retain the pinned tool filename')
    sources.append(source_identity(processor))
    queries = {}

    def query(name, sql, bound=10000):
        result = profiling_tools.query(trace, sql, processor.parent, bound)
        queries[name] = {'sql': sql, **result}
        if result['truncated']:
            raise ValueError('Trace query bound exceeded: ' + name)
        return result['rows']

    clocks = query('clocks', 'SELECT * FROM clock_snapshot ORDER BY snapshot_id,clock_id', 10000)
    bounds = query('traceBounds', 'SELECT * FROM trace_bounds')
    query('lossStats', 'SELECT name,idx,severity,source,value FROM stats WHERE value!=0 ORDER BY name,idx')
    query('scopeInventory', "SELECT name,count(*) AS samples,avg(dur)/1e6 AS mean_ms,max(dur)/1e6 AS max_ms FROM slice WHERE name GLOB 'ME/*' AND dur>=0 GROUP BY name ORDER BY name")
    query('systemTables', "SELECT name FROM sqlite_master WHERE name IN ('sched','thread_state','actual_frame_timeline_slice','expected_frame_timeline_slice') ORDER BY name")
    snapshots = defaultdict(dict)
    for row in clocks:
        snapshots[(row.get('machine_id'), row['snapshot_id'])][row['clock_name']] = row
    mappings = []
    machines = {key[0] for key in snapshots}
    for snapshot, rows in snapshots.items():
        mono, boot = rows.get('MONOTONIC'), rows.get('BOOTTIME')
        if len(machines) == 1 and mono and boot and int(boot['ts']) == int(boot['clock_value']) and mono['ts'] == boot['ts']:
            mappings.append({'snapshot': list(snapshot), 'monotonicNs': int(mono['clock_value']),
                             'traceNs': int(boot['ts']), 'offsetNs': int(boot['clock_value']) - int(mono['clock_value'])})
    mappings.sort(key=lambda r: r['monotonicNs'])
    correlated = []
    for window in windows:
        start, end = window['startOwnerNanoseconds'], window['endOwnerNanoseconds']
        bracket = [m for m in mappings if m['monotonicNs'] <= start]
        closing = [m for m in mappings if m['monotonicNs'] >= end]
        item = {'scenario': window['name'], 'status': 'unavailable'}
        if not bracket or not closing:
            item['reason'] = 'clock_snapshots_do_not_bracket_window'
        else:
            a, b = bracket[-1], closing[0]
            offsets = {m['offsetNs'] for m in mappings if a['monotonicNs'] <= m['monotonicNs'] <= b['monotonicNs']}
            if max(offsets) - min(offsets) > 1000000:
                item['reason'] = 'changing_clock_offset_no_exact_interpolation'
            elif not bounds or start + min(offsets) < int(bounds[0]['start_ts']) or end + max(offsets) > int(bounds[0]['end_ts']):
                item['reason'] = 'mapped_window_outside_trace_bounds'
            else:
                lo, hi = start + min(offsets), end + max(offsets)
                item.update({'status': 'mapped' if len(offsets) == 1 else 'bounded_clock_correlation',
                             'startTraceNs': lo, 'endTraceNs': hi, 'observedOffsetMinNs': min(offsets),
                             'observedOffsetMaxNs': max(offsets), 'observedOffsetSpreadNs': max(offsets) - min(offsets),
                             'clockUncertainty': 'Observed bracketing snapshot offset envelope; intermediate offset is not independently sampled continuously. Variation <=1 ms permits window correlation only, never exact per-frame joins.'})
                # Static ATrace names contain no frame/token. These are same-clock
                # window aggregates, never exact Vulkan-to-display matches.
                name = str(len(correlated))
                item['ownerThreads'] = query('ownerThreads' + name,
                    "SELECT DISTINCT t.utid,t.tid,t.name,p.pid,p.name AS process_name FROM slice s JOIN thread_track tt ON tt.id=s.track_id JOIN thread t ON t.utid=tt.utid LEFT JOIN process p ON p.upid=t.upid WHERE s.name='ME/owner.render' AND s.ts<" + str(hi) + ' AND s.ts+s.dur>' + str(lo))
                tids = [int(r['utid']) for r in item['ownerThreads']]
                if len(tids) == 1:
                    item['scheduling'] = query('scheduling' + name,
                        'SELECT state,count(*) AS samples,sum(min(ts+dur,' + str(hi) + ')-max(ts,' + str(lo) + ')) AS nanoseconds FROM thread_state WHERE utid=' + str(tids[0]) + ' AND dur>0 AND ts<' + str(hi) + ' AND ts+dur>' + str(lo) + ' GROUP BY state')
                else:
                    item['schedulingUnavailableReason'] = 'owner_thread_not_unique'
        correlated.append(item)
    return {'status': 'observed', 'clockMappingRule': 'trace BOOTTIME confirmed by matching clock_snapshot.ts to paired BOOTTIME value; MONOTONIC requires bracketing observations without extrapolation. Equal offsets enable exact mapping; <=1 ms observed variation enables only explicitly uncertain window correlation using the expanded offset envelope.',
            'mappingObservations': mappings, 'windows': correlated, 'queries': queries,
            'displayJoin': 'unavailable: no observed Vulkan submission-to-physical-display buffer mapping'}, sources


def audit(config, config_path):
    """Build a fresh in-place evidence report with independent target status."""
    if config.get('schemaVersion') != 1:
        raise ValueError('Expected audit input schemaVersion=1')
    previous = Path(config['previousRoundPath']).resolve(strict=True)
    diagnostic = Path(config['diagnosticCapturePath']).resolve(strict=True)
    if not diagnostic.is_relative_to(previous):
        raise ValueError('Diagnostic capture is outside selected preserved round')
    windows, sources = reconcile_windows(previous)
    sources.append(source_identity(config_path))
    sources += [source_identity(Path(__file__)), source_identity(Path(measurements.__file__)),
                source_identity(profiling_tools.LOCK)]
    for relative in ('regression-conclusion.md', 'verification-report.md', 'logs/regression-r3/window-audit.json'):
        sources.append(source_identity(previous / relative))
    context_path = diagnostic / 'harness/metadata.json'
    context = read_document(context_path)
    capture_path = diagnostic / 'capture.json'
    capture = read_document(capture_path)
    if not capture.get('completed') or context.get('captureCompleted') is not True or capture.get('captureSession') != context.get('captureSession'):
        raise ValueError('Diagnostic capture incomplete/session mismatch')
    sources += [source_identity(context_path), source_identity(capture_path)]
    events, native_sources, loss = native_union(diagnostic / 'harness/raw')
    if not events:
        raise ValueError('No exact native profiling events available')
    sources += native_sources
    intervals = pair_intervals(events)
    intervals['ownerAttempts'] = owner_attempts(intervals['scopes'], events)
    frame_rows, frame_sources, frame_loss, _, _, _, _ = collect_frames(diagnostic / 'harness/raw', context['captureSession'])
    intervals['originalFrameIntervals'] = [
        {k: row[k] for k in (*measurements.GENERATIONS, 'frameId', 'submissionToken',
                             'ownerStartNanoseconds', 'presentReturnNanoseconds', 'ownerWorkMilliseconds',
                             'sessionMilliseconds', 'acquireMilliseconds', 'presentMilliseconds',
                             'cpuFrameMilliseconds', 'presentReturnCadenceMilliseconds',
                             'gpuTimingFrame', 'sortCountFrame', 'sortStatus', 'sortOverflow',
                             *(field for stage in measurements.STAGES for field in ('gpu' + stage + 'Status', 'gpu' + stage + 'Milliseconds')))}
        for row in frame_rows]
    intervals['originalFrameJoinRule'] = 'Frame rows retain complete original scene/surface/config/pipeline/frame/token identities and asynchronous GPU origin validation. Raw native scopes lack surface/config/pipeline generations: no exact frame attribution is invented from timestamp proximity or matching frame numbers alone.'
    loss['frameRetentionObservations'] = frame_loss
    sources += frame_sources
    trace, trace_sources = trace_audit(diagnostic, Path(config['traceProcessorPath']) if config.get('traceProcessorPath') else None, context['scenarios'])
    sources += trace_sources
    quality_directory = previous / 'captures/p0-verification-01/quality'
    quality_files = sorted(p for p in quality_directory.rglob('*') if p.is_file())
    if len(quality_files) > 512:
        raise ValueError('Quality evidence file bound exceeded')
    sources += [source_identity(p) for p in quality_files]
    source_map = {s['path']: {k: s[k] for k in ('path', 'bytes', 'sha256')} for s in sources}
    for expected in config.get('sourceIdentities', []):
        actual = source_map.get(str(Path(expected['path']).resolve()))
        if actual is None or any(actual[k] != expected[k] for k in ('bytes', 'sha256')):
            raise ValueError('Declared input source identity mismatch: ' + expected['path'])
    diagnostic_windows = []
    for window in context['scenarios']:
        start, end = window['startOwnerNanoseconds'], window['endOwnerNanoseconds']
        scopes = [s for s in intervals['scopes'] if start <= s['startMonotonicNs'] and s['endMonotonicNs'] <= end]
        by_name = defaultdict(list)
        for scope in scopes:
            by_name[scope['name']].append(scope['milliseconds'])
        selected = [e for e in events if start <= e['timestampNs'] < end]
        diagnostic_windows.append({'scenario': window['name'], 'startMonotonicNs': start, 'endMonotonicNs': end,
                                   'events': len(selected), 'scopeMilliseconds': {k: measurements.distribution(v, len(v)) for k, v in by_name.items()},
                                   'eventNames': dict(Counter(e['name'] + ':' + e['phase'] for e in selected))})
    mismatches = sum(w['reconciliation'] != 'pass' for w in windows)
    return {'schemaVersion': 1, 'status': 'fail' if mismatches else 'pass',
            'statusMeaning': 'Offline analysis contract only; no optimization, target, quality or native admission acceptance.',
            'targetStatus': 'fail' if any(w['acceptance']['status'] == 'fail' for w in windows) else 'unverified',
            'evidenceSufficiency': 'blocked', 'sourceFiles': list(source_map.values()),
            'windows': windows, 'clockDomains': {'native': 'CLOCK_MONOTONIC nanoseconds', 'system': trace},
            'coverage': {'baselineWindowsReconstructed': len(windows), 'windowReconciliationMismatches': mismatches,
                         'diagnosticWindows': diagnostic_windows, 'nativeUniqueEvents': len(events)},
            'historicalImages': {'files': [source_identity(p) for p in quality_files],
                                 'pngCount': sum(p.suffix.lower() == '.png' for p in quality_files),
                                 'nativeSidecarCount': sum(p.name.endswith('.native.json') for p in quality_files),
                                 'qualityStatus': 'blocked',
                                 'interpretation': 'File existence index only. Observation JSON cannot replace PNG pixels or sufficiently-loaded same-pose original-frame proof.'},
            'loss': loss, 'intervals': intervals,
            'attribution': {'supported': ['All twelve baseline windows are independently reconstructed from preserved raw frame snapshots.',
                                           'CPU request/owner/fence/acquire/submit/present intervals are observed only for complete causal same-clock pairs.',
                                           'GPU stage durations are asynchronous original-frame compute measurements and exclude fullscreen presentation and uploads.'],
                            'unknown': ['Historical degradation detail is redacted; aggregate degraded does not identify a cause.',
                                        'Historical native quota/allocation/admission and sufficiently-loaded original reference are unavailable.',
                                        'Physical presentation cannot be matched to Vulkan tokens.',
                                        'Unobserved owner/pacing intervals and lost boundaries prevent a complete critical-path explanation.'],
                            'nextAction': 'Use bounded additive diagnostic and original-reference evidence before selecting any single P1 candidate.'},
            'limitations': ['Native ring overwrites/drops are cumulative; union sequence gaps and frame retention are different quantities.',
                            'No epoch/session/generation crossing or interpolation over changing clock offsets.',
                            'Window clipping creates unmatched scope boundaries; intervals are never defaulted to zero.',
                            'Nested CPU intervals and GPU Pipeline children overlap; means cannot be subtracted or summed to claim a bottleneck.',
                            'Same-time system scheduling aggregates support window correlation, not exact request/frame/display causality.',
                            'Tools-off retains existing frame/readback/export observers; diagnostic is a separate short replay, not a paired optimization result.']}


def bounded_lifecycle_report(report):
    """Check the exact pretty-printed output bound without allocating JSON bytes."""
    size = 1  # final newline used by CLI/capture writers
    for piece in json.JSONEncoder(indent=2, allow_nan=False).iterencode(report):
        size += len(piece.encode('utf-8'))
        if size > MAX_LIFECYCLE_REPORT_BYTES:
            raise ValueError('Lifecycle report byte bound exceeded; no eligible report emitted')
    return report


def reconcile_lifecycle(events, observations, conflicts=None):
    """Reconcile indexed native histories; no origin/work history is copied per chain."""
    conflicts = conflicts or []
    if len(events) + len(conflicts) > MAX_ANALYSIS_EVENTS:
        raise ValueError('Lifecycle analysis event bound exceeded; no chains evaluated')
    if len(observations) > MAX_ANALYSIS_OBSERVATIONS:
        raise ValueError('Lifecycle analysis observation bound exceeded; no chains evaluated')
    output_budget = 0
    def reserve_output(value):
        # Charge shared events/histories and every constant-size chain before
        # retaining it. The final exact check also includes wrapper/loss fields.
        nonlocal output_budget
        for piece in json.JSONEncoder(indent=2, allow_nan=False).iterencode(value):
            output_budget += len(piece.encode('utf-8'))
            if output_budget > MAX_LIFECYCLE_REPORT_BYTES:
                raise ValueError('Lifecycle report construction byte bound exceeded; no chains evaluated')
    reserve_output(events)
    origins, works, scenes = defaultdict(list), defaultdict(list), defaultdict(list)
    for event in events:
        scene = event['sceneGeneration']
        if event.get('stage') not in ('demand', 'prefetch', 'queued', 'cache_hit', 'read_started',
                'read_complete', 'cpu_ready', 'upload_recorded', 'upload_submitted', 'gpu_complete',
                'active_ready', 'first_use', 'cancelled', 'failed', 'evicted', 'already_resident',
                'upstream_demand', 'upstream_retired', 'work_coalesced', 'validated',
                'ready_transferred', 'work_rejected'):
            raise ValueError('Unsupported native lifecycle stage; no chains evaluated')
        scenes[scene].append(event)
        if event.get('upstreamOriginId') is not None:
            origins[(scene, event['upstreamOriginId'])].append(event)
        if event.get('workId') is not None:
            works[(scene, event['workId'])].append(event)
    # Sort each index once, including direct API callers. All later history queries
    # are prefix counts, interval searches, or a fixed number of stage lookups.
    for index in (origins, works, scenes):
        for rows in index.values():
            rows.sort(key=lambda e: e['sequence'])
    gaps = {scene: [[a['sequence'] + 1, b['sequence'] - 1]
                    for a, b in zip(rows, rows[1:]) if b['sequence'] > a['sequence'] + 1]
            for scene, rows in scenes.items()}
    gap_ends = {scene: [b for a, b in ranges] for scene, ranges in gaps.items()}
    conflict_sequences = defaultdict(list)
    for conflict in conflicts:
        conflict_sequences[conflict.get('sceneGeneration')].append(conflict.get('sequence', 0))
    for values in conflict_sequences.values():
        values.sort()
    dropped_scenes = {o.get('sceneGeneration') for o in observations if (o.get('dropped') or 0) > 0}

    def has_gap(scene, lo, hi):
        ranges = gaps.get(scene, [])
        pos = bisect_left(gap_ends.get(scene, []), lo)
        return pos < len(ranges) and ranges[pos][0] <= hi

    def has_conflict(scene, lo, hi):
        values = conflict_sequences.get(scene, [])
        pos = bisect_left(values, lo)
        return pos < len(values) and values[pos] <= hi

    immutable = ('originSceneGeneration', 'originPageId', 'originFrameId', 'originRequestId',
                 'originSurfaceGeneration', 'originConfigGeneration', 'originPipelineGeneration', 'representationFlags')
    physical = ('queued', 'cpu_ready', 'ready_transferred', 'upload_recorded',
                'upload_submitted', 'gpu_complete', 'active_ready')
    terminals = ('cancelled', 'failed', 'work_rejected', 'evicted', 'upstream_retired')
    histories, origin_index, work_index = [], {}, {}

    def positive_id(value):
        return type(value) is int and 0 < value < (1 << 64)

    consumer_fields = ('sceneGeneration', 'frameId', 'selectionVersion', 'residencyVersion',
                       'surfaceGeneration', 'configGeneration', 'pipelineGeneration')

    def consumer_upload_issue(work, consumer):
        # Query only the retained physical-work prefix at this consumer. A later
        # upload cannot validate an earlier use; equal tokens can share a submit.
        if not work:
            return 'successful_consumer_last_upload_unavailable'
        n = bisect_left(work['sequences'].get('upload_submitted', []), consumer['sequence'])
        if not n:
            return 'successful_consumer_last_upload_unavailable'
        upload = work['stages']['upload_submitted'][n - 1]
        if (not positive_id(upload.get('submissionToken')) or
                not positive_id(consumer.get('pageLifetime')) or
                upload.get('pageLifetime') != consumer.get('pageLifetime')):
            return 'successful_consumer_last_upload_token_or_lifetime_unavailable_or_conflicting'
        chunks = bisect_right(work['chunk_sequences'], consumer['sequence'])
        matching_slots = bisect_right(work['slot_sequences'].get(consumer['pageLifetime'], []), consumer['sequence'])
        if chunks != matching_slots or bisect_right(work['null_submissions'], consumer['sequence']):
            return 'successful_consumer_physical_chunks_unavailable_or_conflicting'
        if consumer['submissionToken'] < upload['submissionToken']:
            return 'successful_consumer_submission_precedes_last_upload'
        return None

    def history(rows, kind, key):
        ref = len(histories)
        entry = {'kind': kind, 'sceneGeneration': key[0],
                 'id': key[1], 'eventSequences': [e['sequence'] for e in rows]}
        reserve_output(entry)
        histories.append(entry)
        return ref

    # One immutable origin summary and one set of origin/work association lists.
    for key, rows in sorted(origins.items()):
        reasons, ambiguous = [], []
        identities = {tuple(row.get(k) for k in immutable) for row in rows}
        identity = {name: rows[0].get(name) for name in immutable}
        if len(identities) != 1:
            ambiguous.append('conflicting_immutable_origin')
        if any(identity[name] is None for name in immutable):
            reasons.append('origin_identity_unavailable')
        if identity['originSceneGeneration'] != key[0] or any(e.get('pageId') != identity['originPageId'] for e in rows):
            ambiguous.append('origin_scene_or_page_mismatch')
        demands = [e for e in rows if e['stage'] == 'upstream_demand']
        if len(demands) != 1:
            (ambiguous if demands else reasons).append('duplicate_upstream_demand' if demands else 'missing_upstream_demand')
        linked = defaultdict(list)
        for e in rows:
            if e.get('workId') is not None:
                linked[e['workId']].append(e)
        associations = [e for e in rows if e['stage'] == 'already_resident']
        retired = [e['sequence'] for e in rows if e['stage'] == 'upstream_retired']
        origin_index[key] = dict(rows=rows, identity=identity, demands=demands, linked=linked,
                                 reasons=reasons, ambiguous=ambiguous, associations=associations,
                                 retired=retired, terminal={e['stage'] for e in rows if e['stage'] in terminals},
                                 history=history(rows, 'origin', key))
    if sum(max(1, len(info['linked'])) for info in origin_index.values()) > MAX_LIFECYCLE_CHAINS:
        raise ValueError('Lifecycle chain bound exceeded; no chains evaluated')

    # Each physical work retains its accepted upstream cause. Current-origin
    # association/consumer rows never overwrite it, but all must name that cause.
    for key, rows in sorted(works.items()):
        causes = {e.get('workOriginId') for e in rows}
        cause = next(iter(causes)) if len(causes) == 1 else None
        uncertainty, missing = [], []
        if len(causes) != 1 or cause is None:
            uncertainty.append('missing_or_conflicting_work_origin')
        accepted = origin_index.get((key[0], cause))
        demands = accepted['demands'] if accepted else []
        if len(demands) != 1:
            (uncertainty if demands else missing).append('duplicate_work_origin_upstream_demand' if demands else 'missing_work_origin_upstream_demand')
        if accepted and (accepted['reasons'] or accepted['ambiguous']):
            uncertainty.append('accepted_work_origin_unavailable_or_conflicting')
        if any(e.get('upstreamOriginId') != cause for e in rows
               if e['stage'] in physical + ('cache_hit', 'read_started', 'read_complete', 'validated')):
            uncertainty.append('physical_stage_origin_differs_from_accepted_cause')
        original_rows = [e for e in rows if e.get('upstreamOriginId') == cause]
        identities = {tuple(e.get(name) for name in immutable) for e in original_rows}
        if len(identities) != 1 or any(v is None for values in identities for v in values):
            uncertainty.append('original_work_origin_identity_unavailable_or_conflicting')
        if len(demands) == 1:
            demand_identity = tuple(demands[0].get(name) for name in immutable)
            if identities != {demand_identity} or any(e.get('pageId') != demands[0].get('originPageId') for e in rows):
                uncertainty.append('accepted_demand_physical_cause_mismatch')
        stages = defaultdict(list)
        for e in original_rows:
            stages[e['stage']].append(e)
        sequences = {stage: [e['sequence'] for e in values] for stage, values in stages.items()}
        # Prefix maxima/minima and inverted slot/token indexes make repeated
        # chunk validation independent of the number of origins borrowing work.
        chunk_rows = [e for e in original_rows if e['stage'] in ('upload_recorded', 'upload_submitted', 'gpu_complete', 'active_ready')]
        slot_sequences = defaultdict(list)
        chunk_sequences, chunk_max_time, chunk_min_time = [], [], []
        max_time, min_time = 0, 1 << 64
        for e in chunk_rows:
            chunk_sequences.append(e['sequence'])
            slot_sequences[e.get('pageLifetime')].append(e['sequence'])
            max_time = max(max_time, e['timestampNanoseconds'])
            min_time = min(min_time, e['timestampNanoseconds'])
            chunk_max_time.append(max_time)
            chunk_min_time.append(min_time)
        submission_rows = stages['upload_submitted']
        null_submissions = [e['sequence'] for e in submission_rows if not positive_id(e.get('submissionToken'))]
        # Completion and active-ready require the last observed submitted upload
        # token before them. No chunk-to-DMA pairing or consumer-token equality.
        invalid_completion = []
        for stage in ('gpu_complete', 'active_ready'):
            for e in stages[stage]:
                n = bisect_left(sequences.get('upload_submitted', []), e['sequence'])
                if not n or not positive_id(e.get('submissionToken')) or e.get('submissionToken') != submission_rows[n - 1].get('submissionToken'):
                    invalid_completion.append(e['sequence'])
        invalid_completion.sort()
        ref = history(rows, 'work', key)
        reserve_output(sequences)
        histories[ref]['physicalStageSequences'] = sequences
        work_index[key] = dict(rows=rows, rows_sequences=[e['sequence'] for e in rows], cause=cause, demands=demands, stages=stages, sequences=sequences,
                               uncertainty=uncertainty, missing=missing, history=ref,
                               chunk_sequences=chunk_sequences, slot_sequences=slot_sequences,
                               chunk_max_time=chunk_max_time, null_submissions=null_submissions,
                               chunk_min_time=chunk_min_time,
                               invalid_completion=invalid_completion,
                               terminal={e['stage'] for e in rows if e['stage'] in terminals})
    chains, pool = [], []
    for (scene, origin), info in sorted(origin_index.items()):
        identity, demands = info['identity'], info['demands']
        for work, linked_rows in sorted(info['linked'].items()) if info['linked'] else [(None, [])]:
            missing, uncertainty = list(info['reasons']), list(info['ambiguous'])
            consumption = [e for e in linked_rows if e['stage'] == 'first_use']
            candidate = consumption[0] if consumption else None
            first = candidate if candidate and positive_id(candidate.get('submissionToken')) else None
            if first is None:
                missing.append('missing_successful_first_use')
            w = work_index.get((scene, work))
            cutoff = first['sequence'] if first else (1 << 64)
            work_origin = w['cause'] if w else None
            counts = {s: bisect_right(values, cutoff) for s, values in w['sequences'].items()} if w else {}
            def stage_rows(stage):
                return w['stages'].get(stage, []) if w else []
            def stage_count(stage):
                return counts.get(stage, 0)
            def unique(stage):
                return stage_rows(stage)[0] if stage_count(stage) == 1 else None
            if w:
                missing.extend(w['missing'])
                uncertainty.extend(w['uncertainty'])
            else:
                uncertainty.append('missing_or_conflicting_work_origin')
                missing.append('missing_work_origin_upstream_demand')
            cache_hit = bool(stage_count('cache_hit'))
            required = list(physical) + (['cache_hit', 'validated'] if cache_hit else ['read_started', 'read_complete', 'validated'])
            for stage in required:
                if not stage_count(stage):
                    missing.append('missing_' + stage)
                elif stage_count(stage) != 1 and stage not in ('upload_recorded', 'upload_submitted'):
                    uncertainty.append('repeated_' + stage + '_without_attempt_identity')
            if cache_hit and (stage_count('read_started') or stage_count('read_complete')):
                uncertainty.append('cache_and_read_paths_share_work_without_attempt_identity')
            if origin != work_origin and not any(e['stage'] in ('work_coalesced', 'already_resident', 'first_use') for e in linked_rows):
                missing.append('missing_work_association')
            ordered_stages = ['queued'] + (['cache_hit', 'validated'] if cache_hit else ['read_started', 'read_complete', 'validated']) + list(physical[1:])
            ordered = [unique(s) for s in ordered_stages if unique(s) is not None]
            if any(b['sequence'] <= a['sequence'] or b['timestampNanoseconds'] < a['timestampNanoseconds'] for a, b in zip(ordered, ordered[1:])):
                uncertainty.append('physical_stage_order_or_clock_conflict')
            accepted_demands = w['demands'] if w else []
            if len(accepted_demands) == 1 and stage_count('queued'):
                queued = stage_rows('queued')[0]
                if queued['sequence'] <= accepted_demands[0]['sequence'] or queued['timestampNanoseconds'] < accepted_demands[0]['timestampNanoseconds']:
                    uncertainty.append('work_acceptance_precedes_origin')
            nr, ns, nc = (stage_count(s) for s in ('upload_recorded', 'upload_submitted', 'gpu_complete'))
            if nr and ns and nc:
                recordings, submissions, completed = (stage_rows(s) for s in ('upload_recorded', 'upload_submitted', 'gpu_complete'))
                # All repeated chunks, not only unique stages, precede completion.
                preceding_chunks = bisect_left(w['chunk_sequences'], completed[0]['sequence'])
                if (submissions[0]['sequence'] <= recordings[0]['sequence'] or
                        recordings[nr - 1]['sequence'] >= completed[0]['sequence'] or
                        submissions[ns - 1]['sequence'] >= completed[0]['sequence'] or
                        (preceding_chunks and w['chunk_max_time'][preceding_chunks - 1] > completed[0]['timestampNanoseconds']) or
                        bisect_right(w['null_submissions'], cutoff)):
                    uncertainty.append('upload_submission_completion_order_or_token_unavailable')
            if w:
                nslots = bisect_right(w['chunk_sequences'], cutoff)
                ready = unique('ready_transferred')
                if nslots and ready and (w['chunk_sequences'][0] <= ready['sequence'] or
                        w['chunk_min_time'][nslots - 1] < ready['timestampNanoseconds']):
                    uncertainty.append('physical_stage_order_or_clock_conflict')
            if w and bisect_right(w['invalid_completion'], cutoff):
                uncertainty.append('completion_or_ready_last_upload_token_unavailable_or_conflicting')
            involved_sequences = [e['sequence'] for e in demands[:2] + accepted_demands[:2]]
            if w and w['rows'][0]['sequence'] <= cutoff:
                involved_sequences.extend((w['rows'][0]['sequence'], w['rows'][bisect_right(w['rows_sequences'], cutoff) - 1]['sequence']))
            if first:
                involved_sequences.append(first['sequence'])
            if involved_sequences:
                lo, hi = min(involved_sequences), max(involved_sequences)
                if has_gap(scene, lo, hi):
                    uncertainty.append('unrecovered_sequence_gap_between_stages')
                if has_conflict(scene, lo, hi):
                    uncertainty.append('conflicting_duplicate_event_between_stages')
            if scene in dropped_scenes:
                uncertainty.append('queue_drop_location_unknown')
            if first is not None:
                lifetime = first.get('pageLifetime')
                nslots = bisect_right(w['chunk_sequences'], cutoff) if w else 0
                same_slots = bisect_right(w['slot_sequences'].get(lifetime, []), cutoff) if w else 0
                if not positive_id(lifetime) or nslots != same_slots:
                    uncertainty.append('missing_or_conflicting_physical_slot_lifetime')
                if any(not positive_id(first.get(k)) for k in consumer_fields):
                    missing.append('successful_consumer_generation_or_mapping_unavailable')
                upload_issue = consumer_upload_issue(w, first)
                if upload_issue:
                    uncertainty.append(upload_issue)
                if demands and (first['timestampNanoseconds'] < demands[0]['timestampNanoseconds'] or first['sequence'] <= demands[0]['sequence']):
                    uncertainty.append('consumption_precedes_upstream_origin')
                if ordered and (first['sequence'] <= ordered[-1]['sequence'] or first['timestampNanoseconds'] < ordered[-1]['timestampNanoseconds']):
                    uncertainty.append('consumption_precedes_residency')
                if info['retired'] and info['retired'][0] < first['sequence']:
                    uncertainty.append('retired_origin_consumed')
            terminal = sorted(info['terminal'] | (w['terminal'] if w else set()))
            if w and any(stage_count(s) and (first is None or stage_rows(s)[0]['sequence'] < first['sequence']) for s in terminals[:-1]):
                uncertainty.append('terminal_event_before_consumption')
            status = 'ambiguous' if uncertainty else 'incomplete' if missing else 'complete_retained_chain'
            # Small compatibility fields are retained. Long lists are stored once
            # in histories; references include exact prefix counts at this cutoff.
            inline_origin = len(info['rows']) <= INLINE_HISTORY_LIMIT
            inline_physical = sum(counts.values()) <= INLINE_HISTORY_LIMIT
            associations = info['associations']
            chain = {'sceneGeneration': scene, 'upstreamOriginId': origin, 'workId': work,
                           'workOriginId': work_origin, 'origin': identity,
                           'relationship': 'original' if origin == work_origin else 'coalesced',
                           'roles': [name for bit, name in ((1, 'desired'), (2, 'selected_fallback'), (4, 'mandatory'), (8, 'prefetch')) if (identity['representationFlags'] or 0) & bit],
                           'status': status, 'missingStages': sorted(set(missing)), 'ambiguities': sorted(set(uncertainty)),
                           'cacheHitReadStages': 'not_applicable' if cache_hit else 'required',
                           'associationOnlyAlreadyResidentSequences': [e['sequence'] for e in associations if not positive_id(e.get('submissionToken'))] if inline_origin else None,
                           'successfulAlreadyResidentObservations': [e for e in associations if positive_id(e.get('submissionToken')) and all(positive_id(e.get(k)) for k in consumer_fields) and consumer_upload_issue(w, e) is None] if inline_origin else None,
                           'terminalObservations': terminal, 'successfulFirstUse': first,
                           'outcome': 'consumed' if first else 'failed' if 'failed' in terminal else 'cancelled' if 'cancelled' in terminal else 'rejected' if 'work_rejected' in terminal else 'evicted' if 'evicted' in terminal else 'retired' if 'upstream_retired' in terminal else 'associated_only' if associations else 'pending_or_capture_tail_missing',
                           'originToSuccessfulConsumptionMilliseconds': (first['timestampNanoseconds'] - demands[0]['timestampNanoseconds']) / 1e6 if status == 'complete_retained_chain' else None,
                           'physicalStageSequences': {s: w['sequences'][s][:n] for s, n in counts.items() if n} if inline_physical and w else None,
                           'originEventSequences': [e['sequence'] for e in info['rows']] if inline_origin else None,
                           'originHistoryRef': info['history'],
                           'physicalHistoryRef': w['history'] if w else None,
                           'physicalStagePrefixCounts': counts, 'physicalHistoryThroughSequence': cutoff if first else None}
            reserve_output(chain)
            chains.append(chain)
    selected, lifetimes = defaultdict(list), defaultdict(set)
    for e in events:
        if e.get('demandSequence') is not None:
            selected[(e['sceneGeneration'], e.get('pageId'), e['demandSequence'])].append(e)
        if e.get('pageId') is not None and e.get('pageLifetime') is not None:
            lifetimes[(e['sceneGeneration'], e['pageId'])].add(e['pageLifetime'])
    for key, rows in selected.items():
        demand = [e for e in rows if e['stage'] == 'demand']
        use = [e for e in rows if e['stage'] == 'first_use']
        valid = (len(demand) == 1 and len(use) == 1 and use[0]['sequence'] > demand[0]['sequence'] and
                 use[0]['timestampNanoseconds'] >= demand[0]['timestampNanoseconds'] and key[0] not in dropped_scenes)
        consumer_issue = None
        if len(use) == 1:
            if (not positive_id(use[0].get('submissionToken')) or
                    any(not positive_id(use[0].get(k)) for k in consumer_fields)):
                consumer_issue = 'successful_consumer_identity_unavailable'
            else:
                consumer_issue = consumer_upload_issue(work_index.get((key[0], use[0].get('workId'))), use[0])
            valid = valid and consumer_issue is None
        if valid:
            valid = (not has_gap(key[0], demand[0]['sequence'], use[0]['sequence']) and
                     not has_conflict(key[0], demand[0]['sequence'], use[0]['sequence']))
        pool.append({'sceneGeneration': key[0], 'pageId': key[1], 'poolDemandSequence': key[2],
                     'milliseconds': (use[0]['timestampNanoseconds'] - demand[0]['timestampNanoseconds']) / 1e6 if valid else None,
                     'consumerIssue': consumer_issue,
                     'status': 'observed_pool_only' if valid else 'unavailable_or_ambiguous'})
    return bounded_lifecycle_report({'lifecycleReportSchemaVersion': 2, 'chains': chains,
            'chainStatusCounts': dict(Counter(c['status'] for c in chains)),
            'histories': histories, 'retainedEvents': events,
            'analysisBounds': {'events': MAX_ANALYSIS_EVENTS, 'observations': MAX_ANALYSIS_OBSERVATIONS,
                               'chains': MAX_LIFECYCLE_CHAINS, 'reportBytes': MAX_LIFECYCLE_REPORT_BYTES,
                               'inlineHistoryElements': INLINE_HISTORY_LIMIT, 'truncated': False},
            'coalescingRelations': [e for e in events if e['stage'] == 'work_coalesced'],
            'pageLifetimes': [{'sceneGeneration': s, 'pageId': p, 'physicalLifetimes': sorted(values), 'reloadOrReuseObserved': len(values) > 1}
                              for (s, p), values in sorted(lifetimes.items())],
            'unassignedEvents': [e for e in events if e.get('upstreamOriginId') is None],
            'sequenceGaps': [{'sceneGeneration': s, 'ranges': g, 'unobservedPrefixThrough': scenes[s][0]['sequence'] - 1} for s, g in gaps.items()],
            'poolOnlySelectionLatency': pool, 'conflictingDuplicates': conflicts})


def audit_lifecycle_capture(directory):
    """Read an actual bounded capture, preserving exact raw identities and losses."""
    directory = Path(directory).resolve(strict=True)
    metadata_path = directory / 'harness/metadata.json'
    metadata = read_document(metadata_path)
    sources = [source_identity(metadata_path)]
    paths = sorted((directory / 'harness/raw').rglob('*.json'))
    if not paths or len(paths) > 4096:
        raise ValueError('Missing or excessive lifecycle raw snapshots')
    total, duplicates, raw_event_count = 0, 0, 0
    events, conflicts, observations, transports, profiler, unknown_identity = {}, [], [], [], [], []
    frame_keys = set()

    def check_retained_event_bound():
        # Unknown-scene observations and conflicting sides retain rows too;
        # byte-equivalent known duplicates do not add retained event rows.
        if len(events) + len(conflicts) + len(unknown_identity) > MAX_ANALYSIS_EVENTS:
            raise ValueError('Lifecycle event bound exceeded')

    for path in paths:
        source = source_identity(path)
        total += source['bytes']
        if total > MAX_INPUT_BYTES:
            raise ValueError('Lifecycle raw byte bound exceeded')
        sources.append(source)
        document = read_document(path)
        payload = document.get('payload', document)
        if document.get('captureSession') not in (None, metadata.get('captureSession')):
            raise ValueError('Lifecycle snapshot capture session mismatch')
        diagnostics = payload.get('diagnostics', {})
        transports.append({'source': str(path), 'captureSequence': document.get('captureSequence'),
                           'diagnosticTransport': payload.get('diagnosticTransport'),
                           'droppedFrames': diagnostics.get('droppedFrames'),
                           'unresolvedOverwrittenFrames': diagnostics.get('unresolvedOverwrittenFrames'),
                           'profilingEnabled': payload.get('profilingEnabled'),
                           'profilingRawJSONFile': payload.get('profilingRawJSONFile')})
        exact_profile = payload.get('profilingRawJSON')
        profile_name = payload.get('profilingRawJSONFile')
        if profile_name is not None:
            profile_path = (directory / 'harness' / profile_name).resolve(strict=True)
            if not profile_path.is_relative_to((directory / 'harness/profiling').resolve()):
                raise ValueError('Profiling reference escapes capture profiling directory')
            profile_source = source_identity(profile_path)
            total += profile_source['bytes']
            if total > MAX_INPUT_BYTES:
                raise ValueError('Lifecycle/profile byte bound exceeded')
            sources.append(profile_source)
            profile_document = read_document(profile_path)
        elif exact_profile is not None:
            profile_document = measurements.load_json(exact_profile)
        else:
            profile_document = None
        if profile_document is not None:
            incoming_profile = profile_document.get('events', [])
            if not isinstance(incoming_profile, list) or len(incoming_profile) > 16384:
                raise ValueError('Invalid profiler event ring bound')
            profiler.append({'source': str(path), 'captureSequence': document.get('captureSequence'),
                             **{k: v for k, v in profile_document.items() if k != 'events'},
                             'eventCount': len(incoming_profile),
                             'firstRetainedSequence': min((e['eventSequence'] for e in incoming_profile), default=None),
                             'lastRetainedSequence': max((e['eventSequence'] for e in incoming_profile), default=None)})
        frames = diagnostics.get('frames', [])
        if not isinstance(frames, list) or len(frames) > 512:
            raise ValueError('Invalid lifecycle frame ring')
        for frame in frames:
            detail = frame.get('diagnosticsP0', {}).get('detail', {})
            scene = frame.get('sceneGeneration')
            key = (scene, frame.get('frameId'), frame.get('surfaceGeneration'), frame.get('configGeneration'), frame.get('measurement', {}).get('pipelineGeneration'))
            if key not in frame_keys:
                observations.append({'source': str(path), 'frameIdentity': list(key), 'sceneGeneration': scene,
                                     **{k: v for k, v in detail.items() if k != 'events'}})
                frame_keys.add(key)
                if len(observations) > MAX_ANALYSIS_OBSERVATIONS:
                    raise ValueError('Lifecycle analysis observation bound exceeded; no chains evaluated')
            if detail.get('schemaVersion') != 2:
                continue
            if detail.get('clockDomain') != 'native_clock_monotonic_nanoseconds':
                raise ValueError('Unsupported lifecycle clock domain')
            incoming = detail.get('events', [])
            if not isinstance(incoming, list) or len(incoming) > 16:
                raise ValueError('Invalid lifecycle delta bound')
            raw_event_count += len(incoming)
            if raw_event_count > MAX_RAW_LIFECYCLE_EVENT_OBSERVATIONS:
                raise ValueError('Lifecycle raw event observation bound exceeded; no chains evaluated')
            for e in incoming:
                for name in ('sequence', 'timestampNanoseconds'):
                    if type(e.get(name)) is not int or not 0 < e[name] < 1 << 64:
                        raise ValueError('Missing/inexact lifecycle integer: ' + name)
                for name, value in e.items():
                    if name not in ('stage',) and value is not None and (type(value) is not int or not 0 <= value < 1 << 64):
                        raise ValueError('Invalid lifecycle numeric field: ' + name)
                if e.get('sceneGeneration') is None:
                    unknown_identity.append({'source': str(path), 'event': e, 'reason': 'scene_namespace_unavailable_no_join'})
                    check_retained_event_bound()
                    continue
                event_key = (e['sceneGeneration'], e['sequence'])
                if event_key in events:
                    if events[event_key] != e:
                        conflicts.append({'sceneGeneration': e['sceneGeneration'], 'sequence': e['sequence'], 'retained': events[event_key], 'conflicting': e, 'source': str(path)})
                    else:
                        duplicates += 1
                else:
                    events[event_key] = e
                check_retained_event_bound()
    ordered = sorted(events.values(), key=lambda e: (e['sceneGeneration'], e['sequence']))
    result = reconcile_lifecycle(ordered, observations, conflicts)
    return bounded_lifecycle_report({'schemaVersion': 2, 'kind': 'upstream_lifecycle_audit', 'status': 'pass' if not conflicts else 'fail',
            'statusMeaning': 'Analyzer executed; complete retained subsets do not establish capture completeness, quality, performance or budget feasibility.',
            'captureSession': metadata.get('captureSession'), 'captureCompleted': metadata.get('captureCompleted'),
            'profilingMode': metadata.get('profilingMode'), 'sourceFiles': sources,
            'uniqueEvents': len(events), 'duplicateObservationsMerged': duplicates,
            'unknownIdentityEvents': unknown_identity,
            'upstreamEvidenceStatus': 'observed' if ordered else 'unavailable',
            'loss': {'detailQueueAndRegistryObservations': observations, 'nativeFrameAndTransportExports': transports,
                     'eventGenerationUnavailableCounts': dict(Counter(k for e in ordered for k in ('surfaceGeneration', 'configGeneration', 'pipelineGeneration') if e.get(k) is None)),
                     'profilerRingExports': profiler,
                     'captureBoundary': {'historyBeforeCapture': 'unavailable', 'postFinalTail': 'unavailable', 'captureCompleted': metadata.get('captureCompleted')},
                     'counterRule': 'Cumulative snapshots are observations, never summed. Queue overwrites differ from union sequence gaps. Drops before sequence allocation have unknown location; positive drops conservatively block full-chain eligibility.',
                     'perfetto': {'status': 'unavailable', 'reason': 'This lifecycle consumer does not parse or align Perfetto; separate trace_audit must preserve actual parse/settings/clock/loss failures.'}},
            **result})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument('--input', type=Path)
    selector.add_argument('--lifecycle-capture', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.lifecycle_capture is not None:
            output = args.output.resolve()
            if output.exists() or output.is_relative_to(args.lifecycle_capture.resolve() / 'harness'):
                raise ValueError('Refusing existing output or raw harness destination')
            report = audit_lifecycle_capture(args.lifecycle_capture)
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open('x', encoding='utf-8') as stream:
                json.dump(report, stream, indent=2, allow_nan=False)
                stream.write('\n')
            print('Lifecycle audit written: ' + str(output))
            return 0 if report['status'] == 'pass' else 1
        config = read_document(args.input)
        previous = Path(config['previousRoundPath']).resolve(strict=True)
        output = args.output.resolve()
        if output == args.input.resolve() or output.is_relative_to(previous):
            raise ValueError('Output cannot overwrite input or preserved round')
        if output.exists():
            raise ValueError('Audit output already exists; choose a new report')
        report = audit(config, args.input)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write('\n')
        print('Diagnostic audit written: ' + str(output) + '; analysis=' + report['status'] + '; target=' + report['targetStatus'])
        return 0 if report['status'] == 'pass' else 1
    except (OSError, ValueError, TypeError, KeyError, OverflowError, RecursionError, RuntimeError) as error:
        print('Diagnostic audit failed: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
