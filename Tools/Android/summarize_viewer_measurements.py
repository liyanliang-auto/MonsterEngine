#!/usr/bin/env python3
"""Bounded, dependency-free consumer of native viewer measurement schema 2."""
import argparse
import csv
import hashlib
import io
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
import sys

# Fixed production contract; unknown columns/versions fail rather than migrate.
CONTRACT = [
    {"name":"frameId","type":"uint64","jsonPath":"frameId"},
    {"name":"sceneGeneration","type":"uint64","jsonPath":"sceneGeneration"},
    {"name":"surfaceGeneration","type":"uint64","jsonPath":"surfaceGeneration"},
    {"name":"configGeneration","type":"uint64","jsonPath":"configGeneration"},
    {"name":"submissionToken","type":"uint64","jsonPath":"submissionToken"},
    {"name":"activeGaussians","type":"uint64","jsonPath":"counters.activeGaussians"},
    {"name":"sortEntries","type":"uint64","jsonPath":"counters.sortEntries"},
    {"name":"sortCountFrame","type":"uint64","jsonPath":"counters.sortCountFrame"},
    {"name":"residentBytes","type":"uint64","jsonPath":"counters.residentBytes"},
    {"name":"uploadedBytes","type":"uint64","jsonPath":"counters.uploadedBytes"},
    {"name":"cpuLeaseBytes","type":"uint64","jsonPath":"counters.cpuLeaseBytes"},
    {"name":"cpuFrameMilliseconds","type":"float64","jsonPath":"counters.cpuFrameMilliseconds"},
    {"name":"gpuMilliseconds","type":"float64","jsonPath":"counters.gpuMilliseconds"},
    {"name":"gpuTimingFrame","type":"uint64","jsonPath":"counters.gpuTimingFrame"},
    {"name":"presentReturnCadenceMilliseconds","type":"float64","jsonPath":"timing.presentReturnCadenceMilliseconds"},
    {"name":"degraded","type":"uint64","jsonPath":"degraded"},
    {"name":"streamBytesRead","type":"uint64","jsonPath":"streaming.bytesRead"},
    {"name":"streamPendingReads","type":"uint64","jsonPath":"streaming.pendingReads"},
    {"name":"streamActiveReads","type":"uint64","jsonPath":"streaming.activeReads"},
    {"name":"streamUploadedPages","type":"uint64","jsonPath":"streaming.uploadedPages"},
    {"name":"streamEvictedPages","type":"uint64","jsonPath":"streaming.evictedPages"},
    {"name":"measurementSchemaVersion","type":"uint64","jsonPath":"measurement.measurementSchemaVersion"},
    {"name":"pipelineGeneration","type":"uint64","jsonPath":"measurement.pipelineGeneration"},
    {"name":"ownerStartNanoseconds","type":"uint64","jsonPath":"measurement.ownerStartNanoseconds"},
    {"name":"presentReturnNanoseconds","type":"uint64","jsonPath":"measurement.presentReturnNanoseconds"},
    {"name":"sessionMilliseconds","type":"float64","jsonPath":"measurement.sessionMilliseconds"},
    {"name":"acquireMilliseconds","type":"float64","jsonPath":"measurement.acquireMilliseconds"},
    {"name":"presentMilliseconds","type":"float64","jsonPath":"measurement.presentMilliseconds"},
    {"name":"ownerWorkMilliseconds","type":"float64","jsonPath":"measurement.ownerWorkMilliseconds"},
    {"name":"sortStatus","type":"uint64","jsonPath":"measurement.sortStatus"},
    {"name":"sortOverflow","type":"uint32","jsonPath":"measurement.sortOverflow"},
    {"name":"gpuPreprocessMilliseconds","type":"float64","jsonPath":"measurement.gpuPreprocessMilliseconds"},
    {"name":"gpuPreprocessStatus","type":"uint64","jsonPath":"measurement.gpuPreprocessStatus"},
    {"name":"gpuScanMilliseconds","type":"float64","jsonPath":"measurement.gpuScanMilliseconds"},
    {"name":"gpuScanStatus","type":"uint64","jsonPath":"measurement.gpuScanStatus"},
    {"name":"gpuAssignKeysMilliseconds","type":"float64","jsonPath":"measurement.gpuAssignKeysMilliseconds"},
    {"name":"gpuAssignKeysStatus","type":"uint64","jsonPath":"measurement.gpuAssignKeysStatus"},
    {"name":"gpuRadixSortMilliseconds","type":"float64","jsonPath":"measurement.gpuRadixSortMilliseconds"},
    {"name":"gpuRadixSortStatus","type":"uint64","jsonPath":"measurement.gpuRadixSortStatus"},
    {"name":"gpuTileBoundsMilliseconds","type":"float64","jsonPath":"measurement.gpuTileBoundsMilliseconds"},
    {"name":"gpuTileBoundsStatus","type":"uint64","jsonPath":"measurement.gpuTileBoundsStatus"},
    {"name":"gpuRenderMilliseconds","type":"float64","jsonPath":"measurement.gpuRenderMilliseconds"},
    {"name":"gpuRenderStatus","type":"uint64","jsonPath":"measurement.gpuRenderStatus"},
    {"name":"gpuPipelineMilliseconds","type":"float64","jsonPath":"measurement.gpuPipelineMilliseconds"},
    {"name":"gpuPipelineStatus","type":"uint64","jsonPath":"measurement.gpuPipelineStatus"},
    {"name":"cameraX","type":"float64","jsonPath":"measurement.cameraX"},
    {"name":"cameraY","type":"float64","jsonPath":"measurement.cameraY"},
    {"name":"cameraZ","type":"float64","jsonPath":"measurement.cameraZ"},
    {"name":"cameraYaw","type":"float64","jsonPath":"measurement.cameraYaw"},
    {"name":"cameraPitch","type":"float64","jsonPath":"measurement.cameraPitch"},
    {"name":"cameraDistance","type":"float64","jsonPath":"measurement.cameraDistance"},
    {"name":"externalCamera","type":"uint64","jsonPath":"measurement.externalCamera"},
    {"name":"renderWidth","type":"uint64","jsonPath":"measurement.renderWidth"},
    {"name":"renderHeight","type":"uint64","jsonPath":"measurement.renderHeight"},
    {"name":"presentationWidth","type":"uint64","jsonPath":"measurement.presentationWidth"},
    {"name":"presentationHeight","type":"uint64","jsonPath":"measurement.presentationHeight"},
    {"name":"displayWidth","type":"uint64","jsonPath":"measurement.displayWidth"},
    {"name":"displayHeight","type":"uint64","jsonPath":"measurement.displayHeight"},
    {"name":"lodSelectionMilliseconds","type":"float64","jsonPath":"measurement.lodSelectionMilliseconds"},
    {"name":"uploadRecordMilliseconds","type":"float64","jsonPath":"measurement.uploadRecordMilliseconds"},
    {"name":"desiredNodes","type":"uint64","jsonPath":"measurement.desiredNodes"},
    {"name":"selectedNodes","type":"uint64","jsonPath":"measurement.selectedNodes"},
    {"name":"requestPages","type":"uint64","jsonPath":"measurement.requestPages"},
    {"name":"prefetchPages","type":"uint64","jsonPath":"measurement.prefetchPages"},
    {"name":"maxSelectedProxyErrorPixels","type":"float64","jsonPath":"measurement.maxSelectedProxyErrorPixels"},
    {"name":"selectedLeafNodes","type":"uint64","jsonPath":"measurement.selectedLeafNodes"},
    {"name":"selectedProxyNodes","type":"uint64","jsonPath":"measurement.selectedProxyNodes"},
    {"name":"streamPendingUploads","type":"uint64","jsonPath":"measurement.streamPendingUploads"},
    {"name":"streamReadyBytes","type":"uint64","jsonPath":"measurement.streamReadyBytes"},
    {"name":"streamDeferredPages","type":"uint64","jsonPath":"measurement.streamDeferredPages"},
    {"name":"streamLoading","type":"uint64","jsonPath":"measurement.streamLoading"},
    {"name":"streamRootReady","type":"uint64","jsonPath":"measurement.streamRootReady"},
    {"name":"streamRequests","type":"uint64","jsonPath":"measurement.streamRequests"},
]
STAGES = ('Preprocess', 'Scan', 'AssignKeys', 'RadixSort', 'TileBounds', 'Render', 'Pipeline')
STATUS = {0: 'pending', 1: 'valid', 2: 'unsupported', 3: 'invalid_returned_interval', 4: 'dropped'}
GENERATIONS = ('sceneGeneration', 'surfaceGeneration', 'configGeneration', 'pipelineGeneration')
MAX_FILES, MAX_FILE_BYTES, MAX_TOTAL_BYTES, MAX_RECORDS = 512, 16 << 20, 256 << 20, 250000


def load_json(text):
    """Reject ambiguous keys and nonstandard NaN/Infinity JSON."""
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('Duplicate JSON key: ' + key)
            result[key] = value
        return result
    def bad_constant(value):
        raise ValueError('Nonfinite JSON constant: ' + value)
    return json.loads(text, object_pairs_hook=pairs, parse_constant=bad_constant)


def numeric(value, column):
    """Keep unsigned integers exact; no float -> integer conversion."""
    if value is None or value == '':
        return None
    if column['type'] in ('uint32', 'uint64'):
        if isinstance(value, bool):
            value = int(value)
        if isinstance(value, str):
            if not value.isascii() or not value.isdigit():
                raise ValueError('Expected unsigned integer: ' + column['name'])
            value = int(value)
        bits = 32 if column['type'] == 'uint32' else 64
        if not isinstance(value, int) or not 0 <= value <= (1 << bits) - 1:
            raise ValueError('Invalid ' + column['type'] + ': ' + column['name'])
        return value
    if isinstance(value, bool) or not isinstance(value, (int, float, str)):
        raise ValueError('Expected number: ' + column['name'])
    result = float(value)
    if not math.isfinite(result):
        raise ValueError('Nonfinite number: ' + column['name'])
    return result


def validate(row):
    if row['measurementSchemaVersion'] != 2:
        raise ValueError('Unsupported measurement schema; expected 2')
    for key in (*GENERATIONS, 'frameId', 'submissionToken'):
        if row[key] is None:
            raise ValueError('Missing row identity: ' + key)
    for stage in STAGES:
        prefix = 'gpu' + stage
        state, value = row[prefix + 'Status'], row[prefix + 'Milliseconds']
        if state not in STATUS or (state == 1) != (value is not None):
            raise ValueError('Inconsistent GPU status/value: ' + stage)
        if value is not None and (value < 0 or row['frameId'] == 0):
            raise ValueError('Invalid GPU origin/interval: ' + stage)
    if row['sortStatus'] not in STATUS:
        raise ValueError('Unknown sort status')
    if row['sortStatus'] == 1:
        if row['sortCountFrame'] != row['frameId'] or row['frameId'] == 0 or row['sortEntries'] is None:
            raise ValueError('Sort count origin mismatch')
    elif row['sortEntries'] is not None or row['sortCountFrame'] is not None:
        raise ValueError('Unresolved sort count has values')
    if row['gpuPipelineStatus'] == 1:
        if row['gpuTimingFrame'] != row['frameId'] or row['gpuMilliseconds'] != row['gpuPipelineMilliseconds']:
            raise ValueError('Pipeline GPU origin/value mismatch')
    elif row['gpuMilliseconds'] is not None or row['gpuTimingFrame'] is not None:
        raise ValueError('Unresolved pipeline has aggregate values')
    return row


def path_value(source, path):
    for key in path.split('.'):
        if not isinstance(source, dict):
            return None
        source = source.get(key)
    return source


def json_rows(document):
    """Accept native envelope, payload, diagnostic root, or extracted report.json."""
    if not isinstance(document, dict):
        raise ValueError('Expected snapshot object')
    source = document.get('viewer', document)
    if not isinstance(source, dict):
        raise ValueError('Viewer snapshot unavailable')
    source = source.get('payload', source)
    if not isinstance(source, dict):
        raise ValueError('Snapshot payload unavailable')
    if 'measurementSchemaVersion' in source and source['measurementSchemaVersion'] != 2:
        raise ValueError('Unsupported payload measurement schema')
    diagnostics = source.get('diagnostics', source)
    if not isinstance(diagnostics, dict) or diagnostics.get('schemaVersion') != 2:
        raise ValueError('Unsupported or unavailable native diagnostics schema')
    frames = diagnostics.get('frames')
    if not isinstance(frames, list) or len(frames) > 512:
        raise ValueError('Invalid/beyond-capacity frame ring')
    rows, identities = [], []
    for frame in frames:
        if not isinstance(frame, dict) or frame.get('schemaVersion') != 2:
            raise ValueError('Unsupported native frame schema')
        rows.append(validate({c['name']: numeric(path_value(frame, c['jsonPath']), c) for c in CONTRACT}))
        identities.append({k: frame.get(k) for k in ('contentHash', 'buildIdentity', 'requestedConfig', 'effectiveConfig')})
    loss = {k: diagnostics.get(k) for k in ('droppedFrames', 'unresolvedOverwrittenFrames', 'unmatchedGPURecords', 'capacity')}
    return rows, identities, loss


def csv_rows(text):
    reader = csv.reader(io.StringIO(text))
    if next(reader, None) != [c['name'] for c in CONTRACT]:
        raise ValueError('Unsupported CSV columns/schema')
    result = []
    for cells in reader:
        if not cells:
            continue
        if len(cells) != len(CONTRACT) or len(result) >= 512:
            raise ValueError('Invalid CSV row width/capacity')
        result.append(validate({c['name']: numeric(v, c) for c, v in zip(CONTRACT, cells)}))
    return result, [], {}


def merge_rows(previous, incoming):
    """Merge only monotonic pending -> resolved observations; reject conflicts."""
    result = dict(previous)
    mutable = {'sortEntries', 'sortCountFrame', 'sortStatus', 'sortOverflow', 'gpuMilliseconds', 'gpuTimingFrame'}
    groups = [('sortStatus', ('sortEntries', 'sortCountFrame', 'sortOverflow'))]
    for stage in STAGES:
        prefix = 'gpu' + stage
        fields = (prefix + 'Milliseconds',)
        if stage == 'Pipeline':
            fields += ('gpuMilliseconds', 'gpuTimingFrame')
        groups.append((prefix + 'Status', fields))
        mutable.update((prefix + 'Status', prefix + 'Milliseconds'))
    for key in previous.keys() - mutable:
        if previous[key] != incoming[key]:
            raise ValueError('Conflicting duplicate frame field: ' + key)
    for state, fields in groups:
        old, new = previous[state], incoming[state]
        if old == 0 and new != 0:
            result.update({k: incoming[k] for k in (state, *fields)})
        elif new != 0 and (old != new or any(previous[k] != incoming[k] for k in fields)):
            raise ValueError('Conflicting resolved duplicate: ' + state)
    return validate(result)


def distribution(values, total):
    values = sorted(v for v in values if v is not None)
    def percentile(p):
        if not values:
            return None
        at = (len(values) - 1) * p
        lo = int(at)
        return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (at - lo)
    return {'count': len(values), 'missing': total - len(values), 'mean': math.fsum(values) / len(values) if values else None,
            'min': values[0] if values else None, 'p50': percentile(.5), 'p95': percentile(.95), 'p99': percentile(.99),
            'max': values[-1] if values else None}


def summarize(rows):
    count = len(rows)
    metrics = {c['name']: distribution([r[c['name']] for r in rows], count) for c in CONTRACT
               if c['name'] not in (*GENERATIONS, 'frameId', 'submissionToken', 'sortCountFrame', 'gpuTimingFrame',
                                     'measurementSchemaVersion', 'ownerStartNanoseconds', 'presentReturnNanoseconds')
               and not c['name'].endswith('Status')}
    gpu = {}
    for stage in STAGES:
        prefix = 'gpu' + stage
        states = Counter(STATUS[r[prefix + 'Status']] for r in rows)
        gpu[stage] = {'statusCounts': dict(states), 'validFraction': states['valid'] / count if count else None,
                      'milliseconds': distribution([r[prefix + 'Milliseconds'] for r in rows], count),
                      'origin': 'same full row identity and submissionToken; frameId > 0',
                      'sourceGroups': []}
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[k] for k in GENERATIONS)].append(row)
    coverage = []
    for identity, items in groups.items():
        items.sort(key=lambda r: r['submissionToken'])
        frames = sorted({r['frameId'] for r in items if r['frameId'] > 0})
        gaps = sum(max(0, b - a - 1) for a, b in zip(frames, frames[1:]))
        times = [r['ownerStartNanoseconds'] for r in items if r['ownerStartNanoseconds']]
        for stage in STAGES:
            valid = [r for r in items if r['gpu' + stage + 'Status'] == 1]
            if valid:
                gpu[stage]['sourceGroups'].append({'generations': dict(zip(GENERATIONS, identity)),
                                                   'count': len(valid), 'firstFrameId': min(r['frameId'] for r in valid),
                                                   'lastFrameId': max(r['frameId'] for r in valid),
                                                   'firstSubmissionToken': min(r['submissionToken'] for r in valid),
                                                   'lastSubmissionToken': max(r['submissionToken'] for r in valid)})
        deltas = {}
        for key in ('streamBytesRead', 'uploadedBytes', 'streamUploadedPages', 'streamEvictedPages', 'streamRequests'):
            series = [r[key] for r in items if r[key] is not None]
            resets = sum(b < a for a, b in zip(series, series[1:]))
            deltas[key] = {'first': series[0] if series else None, 'last': series[-1] if series else None,
                           'delta': series[-1] - series[0] if series and not resets else None, 'decreases': resets}
        coverage.append({'generations': dict(zip(GENERATIONS, identity)), 'rows': len(items), 'computeRows': len(frames),
                         'firstFrameId': frames[0] if frames else None, 'lastFrameId': frames[-1] if frames else None,
                         'observedFrameIdGaps': gaps, 'observedOwnerSpanMilliseconds': (max(times) - min(times)) / 1e6 if times else None,
                         'cumulativeCounterDeltas': deltas})
    rates = {}
    for key in ('cpuFrameMilliseconds', 'presentReturnCadenceMilliseconds'):
        mean = metrics[key]['mean']
        rates[key] = {'observedIntervalsPerSecond': 1000 / mean if mean and mean > 0 else None,
                      'meaning': 'CPU owner start cadence' if key.startswith('cpu') else 'successful present-return cadence; not physical display FPS'}
    return {'rows': count, 'metrics': metrics, 'gpu': gpu, 'sortStatusCounts': dict(Counter(STATUS[r['sortStatus']] for r in rows)),
            'cadence': rates, 'coverageGroups': coverage,
            'detailWaitingIndicators': {'rowsWithRequests': sum((r['requestPages'] or 0) > 0 for r in rows),
                                        'rowsLoading': sum(r['streamLoading'] == 1 for r in rows),
                                        'rowsWithPendingUploads': sum((r['streamPendingUploads'] or 0) > 0 for r in rows)}}


def collect(input_path, capture_session):
    if input_path.is_dir():
        files = []
        for candidate in input_path.iterdir():
            if candidate.is_file() and candidate.suffix.lower() in ('.json', '.csv'):
                files.append(candidate)
                if len(files) > MAX_FILES:
                    raise ValueError('Too many input files')
        files.sort()
    else:
        files = [input_path]
    if not files:
        raise ValueError('No input snapshots/CSVs')
    merged, identities, sources, losses = {}, {}, [], []
    total_bytes = records = duplicates = 0
    for path in files:
        with path.open('rb') as stream:
            raw = stream.read(MAX_FILE_BYTES + 1)
        total_bytes += len(raw)
        if len(raw) > MAX_FILE_BYTES or total_bytes > MAX_TOTAL_BYTES:
            raise ValueError('Input byte limit exceeded')
        text = raw.decode('utf-8-sig')
        rows, metadata, loss = csv_rows(text) if path.suffix.lower() == '.csv' else json_rows(load_json(text))
        records += len(rows)
        if records > MAX_RECORDS:
            raise ValueError('Input record limit exceeded')
        sources.append({'name': path.name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(), 'rows': len(rows)})
        if loss:
            losses.append({'name': path.name, **loss})
        for index, row in enumerate(rows):
            key = (capture_session, *(row[k] for k in GENERATIONS), row['frameId'], row['submissionToken'] if row['frameId'] == 0 else None)
            if key in merged:
                duplicates += 1
                merged[key] = merge_rows(merged[key], row)
            else:
                merged[key] = row
            if metadata:
                encoded = json.dumps(metadata[index], sort_keys=True, allow_nan=False)
                if key in identities and identities[key] != encoded:
                    raise ValueError('Conflicting duplicate source/config identity')
                identities[key] = encoded
    return list(merged.values()), sources, losses, records, duplicates, [json.loads(s) for s in sorted(set(identities.values()))]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='One raw snapshot/CSV, or flat directory containing ONLY raw chunks')
    parser.add_argument('--capture-session', required=True, help='Unique owner lifetime/capture label; never combine restarts')
    parser.add_argument('--metadata', type=Path, help='Optional schemaVersion=1 scenario windows and gesture/context metadata')
    parser.add_argument('--output', type=Path, required=True, help='Summary JSON outside the input directory')
    args = parser.parse_args(argv)
    try:
        if args.output.resolve() == args.input.resolve() or (args.metadata and args.output.resolve() == args.metadata.resolve()):
            raise ValueError('Output must not overwrite input/metadata')
        if args.input.is_dir() and args.output.resolve().parent == args.input.resolve():
            raise ValueError('Output must be outside the raw input directory')
        if len(args.capture_session) > 128 or not args.capture_session:
            raise ValueError('Invalid capture session label')
        rows, sources, losses, records, duplicates, identities = collect(args.input, args.capture_session)
        scenarios, context = {}, None
        if args.metadata:
            with args.metadata.open('rb') as stream:
                raw = stream.read((1 << 20) + 1)
            if len(raw) > 1 << 20:
                raise ValueError('Metadata byte limit exceeded')
            context = load_json(raw.decode('utf-8-sig'))
            if not isinstance(context, dict) or context.get('schemaVersion') != 1 or context.get('captureSession') != args.capture_session:
                raise ValueError('Metadata schema/session mismatch')
            windows = context.get('scenarios', [])
            if not isinstance(windows, list) or len(windows) > 32:
                raise ValueError('Invalid scenario list')
            for window in windows:
                name, start, end = window['name'], window['startOwnerNanoseconds'], window['endOwnerNanoseconds']
                if not isinstance(name, str) or not name or name in scenarios:
                    raise ValueError('Duplicate/invalid scenario name')
                if type(start) is not int or type(end) is not int or not 0 < start < end:
                    raise ValueError('Invalid owner-clock scenario window')
                filters = window.get('generations', {})
                if not isinstance(filters, dict) or any(k not in GENERATIONS or type(v) is not int or v < 0 for k, v in filters.items()):
                    raise ValueError('Invalid generation filters')
                selected = [r for r in rows if start <= r['ownerStartNanoseconds'] < end and all(r[k] == v for k, v in filters.items())]
                scenarios[name] = {'windowMilliseconds': (end - start) / 1e6, 'metadata': window, 'summary': summarize(selected)}
        result = {'schemaVersion': 1, 'measurementSchemaVersion': 2, 'captureSession': args.capture_session,
                  'rawRecords': records, 'duplicatesMerged': duplicates, 'sources': sources, 'identities': identities,
                  'nativeRetentionObservations': losses, 'summary': summarize(rows), 'scenarios': scenarios, 'context': context,
                  'limitations': ['Frame ID gaps include reserved/cancelled/unsubmitted origins; not an exact sample-loss count.',
                                  'Native droppedFrames is cumulative ring eviction, not necessarily missed capture samples; do not sum snapshots.',
                                  'Pending GPU origins can become permanently unavailable at overwrite; query-level reasons are unavailable.',
                                  'Metrics overlap; do not sum nested CPU or Pipeline/stage intervals.',
                                  'CSV lacks model/build/config/shader hashes; retain adjacent native JSON and package identities.',
                                  'No physical-display cadence, hardware counters, per-page latency or dedicated GPU upload timestamp.',
                                  'Serialization/publication and polling overhead remain unmeasured unless separately evidenced.',
                                  'Stream loading/request counters indicate demand; they do not prove visual convergence or image quality.']}
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        return 0
    except (OSError, ValueError, TypeError, KeyError, OverflowError, RecursionError) as error:
        print('Measurement summary failed: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
