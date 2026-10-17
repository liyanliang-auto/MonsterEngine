"""Analyze one fixed-camera Bicycle SH32/SH16 timing and linear capture pair."""
import argparse
import csv
import hashlib
import json
import math
import re
import statistics
from pathlib import Path


GATE = {'representation': 'normalized linear RGB RGBA8_UNORM',
        'maeMaximum': 1 / 255, 'psnrMinimumDB': 40,
        'maximumAbsoluteError': 16 / 255, 'visualInspectionRequired': True}
FIXED = {'mode': 'A', 'width': 1920, 'height': 1080, 'displaySHDegree': 3,
         'rasterSharedMemoryMode': 1, 'tightTileCulling': 2,
         'radixSortBlocksPerWorkgroup': 32, 'radixSortPhaseTiming': False,
         'benchmarkFixedCameraSeconds': 0, 'benchmarkFreezeSelection': True,
         'projectedFusion': False, 'fullLeaf': False, 'vsync': False,
         'warmupSeconds': 3, 'sampleSeconds': 8, 'rounds': 1,
         'captureSettleSeconds': 3, 'functionalSmoke': True}
MEMORY_KEYS = ('shLogicalBytes', 'shTailPaddingBytes', 'sourceLogicalBytes',
               'sourceReservedBytes', 'sourceBackingBytes', 'shReservedBytes',
               'shBackingBytes', 'shRequiredAllocationBytes', 'shAllocationAlignmentBytes',
               'initialUploadPayloadBytes', 'initialUploadStagingLogicalBytes',
               'initialUploadStagingAlignmentBytes', 'initialUploadStagingPeakBackingBytes')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def summary(values):
    ordered = sorted(values)
    require(ordered and all(math.isfinite(v) and v > 0 for v in ordered),
            'Missing, nonfinite or nonpositive timing')
    index = (len(ordered) - 1) * .95
    lo, hi = math.floor(index), math.ceil(index)
    return {'count': len(ordered), 'mean': statistics.mean(ordered),
            'median': statistics.median(ordered),
            'p95': ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)}


def csv_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as handle:
        return list(csv.DictReader(handle))


def numeric(details, key, integer=True):
    value = int(details[key]) if integer else float(details[key])
    require(math.isfinite(value) and value >= 0, 'Invalid counter: ' + key)
    return value


def image_metrics(a, b, channels):
    count, changed, total, squared, maximum = 0, 0, 0, 0, 0
    for channel in channels:
        for x, y in zip(a[channel::4], b[channel::4]):
            difference = abs(x - y)
            count += 1
            changed += difference != 0
            total += difference
            squared += difference * difference
            maximum = max(maximum, difference)
    mse = squared / count / (255 * 255)
    return {'MAE': total / count / 255, 'RMSE': math.sqrt(mse),
            'PSNRdB': 10 * math.log10(1 / mse) if mse else None,
            'PSNRInfinite': mse == 0, 'maxAbsoluteError': maximum / 255,
            'changedChannels': changed, 'channelCount': count}


def compare_images(left, right):
    names = sorted(p.name for p in left.glob('capture_*.json'))
    require(names == ['capture_000.json'] == sorted(p.name for p in right.glob('capture_*.json')),
            'Expected exactly one matching capture')
    a, b = read_json(left / names[0]), read_json(right / names[0])
    for key in ('mode', 'width', 'height', 'format', 'colorSpace', 'background', 'exposure',
                'sequenceSeconds', 'view', 'projection', 'cameraPosition', 'focal', 'shDegree',
                'selectionSHA256', 'cameraSHA256', 'tightTileCullingRequested',
                'tightTileCullingEffective', 'radixSortBlocksPerWorkgroupRequested',
                'radixSortBlocksPerWorkgroupEffective', 'rasterSharedMemoryMode'):
        require(a[key] == b[key], 'Capture identity mismatch: ' + key)
    require(a['format'] == 'RGBA8_UNORM' and a['shDegree'] == 3 and a['sequenceSeconds'] == 0,
            'Capture representation/camera mismatch')
    require(a['excludedFromPerformance'] is True and b['excludedFromPerformance'] is True,
            'Capture mixed into performance')
    x, y = (left / 'capture_000.rgba').read_bytes(), (right / 'capture_000.rgba').read_bytes()
    require(len(x) == len(y) == int(a['width']) * int(a['height']) * 4 > 0,
            'Capture byte size mismatch')
    rgb = image_metrics(x, y, (0, 1, 2))
    passed = (rgb['MAE'] <= GATE['maeMaximum'] and
              (rgb['PSNRInfinite'] or rgb['PSNRdB'] >= GATE['psnrMinimumDB']) and
              rgb['maxAbsoluteError'] <= GATE['maximumAbsoluteError'])
    return {'numericStatus': 'PASS' if passed else 'FAIL', 'RGB': rgb,
            'RGBA': image_metrics(x, y, (0, 1, 2, 3)), 'alpha': image_metrics(x, y, (3,)),
            'changedRGBPixels': sum(any(x[i+c] != y[i+c] for c in range(3)) for i in range(0, len(x), 4)),
            'changedRGBAPixels': sum(x[i:i+4] != y[i:i+4] for i in range(0, len(x), 4)),
            'width': a['width'], 'height': a['height'], 'gate': GATE,
            'visualInspection': 'REQUIRED: inspect real images and full native FPS windows',
            'precisionLimitation': 'Linear RGBA8 readback; does not measure errors before output quantization'}


def analyze(manifest_path):
    manifest = read_json(manifest_path)
    require(manifest['status'] == 'RECORDED' and not manifest['errors'], 'Incomplete run manifest')
    require(manifest['qualityGate'] == GATE, 'Preregistered quality gate mismatch')
    runs = manifest['runs']
    require([(r['kind'], r['mode']) for r in runs] ==
            [('timing', 0), ('timing', 1), ('capture', 0), ('capture', 1)], 'Unexpected run order/count')
    for item in manifest['identity']:
        require(sha256(item['path']) == item['sha256'], 'Input changed: ' + item['path'])
    configs, baseline_identity, metrics, memory, uploads = [], None, {}, {}, {}
    configuration_hashes = {}
    for run in runs:
        directory = Path(run['directory']).resolve()
        require(run['exitCode'] == 0 and run['rawHashes'], 'Failed process or missing raw hashes')
        hashes = {Path(h['path']).resolve(): h['sha256'] for h in run['rawHashes']}
        required = ['runtime-config.json', 'metadata.json', 'frames.csv', 'stdout.log', 'stderr.log', 'gpu_counts.csv']
        required += ['gpu_samples.csv'] if run['kind'] == 'timing' else ['capture_000.json', 'capture_000.rgba']
        require(all(directory / name in hashes for name in required), 'Missing required raw hashes')
        for path, digest in hashes.items():
            require(path.is_relative_to(directory) and sha256(path) == digest, 'Changed/unsafe raw file: ' + str(path))
        for name in ('stdout.log', 'stderr.log'):
            log = (directory / name).read_text(encoding='utf-8-sig', errors='replace')
            require(not re.search(r'Validation Error|VUID-|\[ERROR\]|\[FATAL\]', log, re.I),
                    'Runtime/validation error in ' + str(directory / name))
        config = read_json(directory / 'runtime-config.json')
        require(config == run['config'] and config['shStorageMode'] == run['mode'], 'Runtime config mismatch')
        require(all(config.get(k) == v for k, v in FIXED.items()), 'Fixed comparison protocol mismatch')
        capture = run['kind'] == 'capture'
        require(config['validation'] == capture and config['captureOnly'] == capture and
                config['captureTimes'] == ([0] if capture else []), 'Timing/capture isolation mismatch')
        normalized = {k: v for k, v in config.items() if k not in
                      ('output', 'shStorageMode', 'validation', 'captureOnly', 'captureTimes')}
        configs.append(normalized)
        require(all(c == configs[0] for c in configs), 'Nonprecision configuration changed')
        meta = read_json(directory / 'metadata.json')
        details = meta['metadata']
        require(meta['status'] == 'RECORDED', 'Runtime recording incomplete')
        expected = {'shStorageModeRequested': str(run['mode']), 'shStorageModeEffective': str(run['mode']),
                    'rasterSharedMemoryMode': '1', 'tightTileCullingRequested': '2',
                    'tightTileCullingEffective': '2', 'radixSortBlocksPerWorkgroupRequested': '32',
                    'radixSortBlocksPerWorkgroupEffective': '32', 'displaySHDegree': '3', 'sourceSHDegree': '3'}
        require(all(details[k] == v for k, v in expected.items()) and not details['tightTileCullingFallback'],
                'Effective precision/SH/culling/sorting/Raster mismatch')
        keys = ['cameraSHA256', 'selectionSHA256', 'sourceSHA256', 'manifestSHA256', 'sequenceSHA256',
                'executableSHA256', 'device', 'driverVersion']
        keys += sorted(k for k in details if k.startswith('shader_'))
        identity = {k: details[k] for k in keys}
        require(all(identity.values()) and any(k.startswith('shader_') for k in keys), 'Missing identity')
        if baseline_identity is None:
            baseline_identity = identity
        require(identity == baseline_identity, 'Camera/selection/source/build/device identity mismatch')
        configuration_hash = details['comparisonConfigurationSHA256']
        require(configuration_hash, 'Missing configuration identity')
        configuration_hashes.setdefault(run['kind'], configuration_hash)
        require(configuration_hash == configuration_hashes[run['kind']],
                'Within-kind configuration identity mismatch: ' + run['kind'])
        require(details['executableSHA256'] == manifest['identity'][0]['sha256'], 'Executed binary differs from launcher identity')
        recorded_files = {Path(item['path']).name: item['sha256'] for item in manifest['identity']}
        for key in keys:
            if key.startswith('shader_'):
                require(recorded_files.get(key[7:]) == details[key], 'Executed shader differs from launcher identity: ' + key)
        if capture:
            continue
        require(meta['missingGPUFrames'] == 0 and meta['lateGPUSamples'] == 0 and
                details['missingCountFrames'] == '0', 'Missing/late GPU or count samples')
        all_rows = csv_rows(directory / 'frames.csv')
        require(meta['rows'] == len(all_rows), 'Frame row count mismatch')
        rows = [r for r in all_rows if r['sampled'] == '1']
        require(rows, 'No sampled frames')
        samples = {}
        for row in csv_rows(directory / 'gpu_samples.csv'):
            key = (row['frame'], row['stage'])
            require(key not in samples, 'Duplicate GPU query')
            samples[key] = float(row['gpu_ms'])
        for row in rows:
            require(all(row[k] == '0' for k in ('excluded', 'loading', 'degraded', 'overflow', 'failures')),
                    'Invalid sampled frame')
            require(row['gpu_available'] == '1' and row['count_available'] == '1', 'Missing sampled query/count')
            require(row['raster_shared_memory_mode'] == '1' and row['tight_tile_culling_requested'] == '2' and
                    row['tight_tile_culling_effective'] == '2' and row['radix_blocks_requested'] == '32' and
                    row['radix_blocks_effective'] == '32', 'Sample configuration mismatch')
            for stage, column in [('WholeFrame', 'gpu_ms'), ('Preprocess', 'preprocess_ms'), ('Render', 'render_ms')]:
                require((row['frame'], stage) in samples and
                        math.isclose(samples[row['frame'], stage], float(row[column]), rel_tol=1e-6, abs_tol=1e-6),
                        'Missing/mismatched joined GPU stage: ' + stage)
        result = {label: summary([float(r[column]) for r in rows]) for label, column in
                  [('Preprocess', 'preprocess_ms'), ('Raster', 'render_ms'), ('WholeFrame', 'gpu_ms'),
                   ('FrameInterval', 'frame_ms')]}
        for name, column in [('activeCount', 'active_count'), ('sortCount', 'sort_count')]:
            result[name + 'Range'] = [min(int(r[column]) for r in rows), max(int(r[column]) for r in rows)]
        result['FPS'] = 1000 / result['FrameInterval']['mean']
        metrics[str(run['mode'])] = result
        allocation = {key: numeric(details, key) for key in MEMORY_KEYS}
        count = int(details['sourceCount'])
        require(allocation['shLogicalBytes'] == count * (192 if run['mode'] == 0 else 96), 'SH3 byte layout mismatch')
        require(allocation['sourceLogicalBytes'] == count * 52 + allocation['shLogicalBytes'] and
                allocation['sourceReservedBytes'] >= allocation['sourceLogicalBytes'] and
                allocation['sourceBackingBytes'] >= allocation['sourceLogicalBytes'] and
                allocation['shReservedBytes'] >= allocation['shLogicalBytes'] and
                allocation['shBackingBytes'] >= allocation['shLogicalBytes'], 'Invalid physical/logical allocation counters')
        for key in ('allocator_pooled_reserved_bytes', 'allocator_pooled_used_bytes', 'allocator_dedicated_committed_bytes'):
            allocation[key + 'Range'] = [min(int(r[key]) for r in rows), max(int(r[key]) for r in rows)]
        allocation['driverProcessVRAMBytes'] = details['driverProcessVRAMBytes']
        memory[str(run['mode'])] = allocation
        uploads[str(run['mode'])] = {key: numeric(details, key, False) for key in
                                    ('shConversionMilliseconds', 'initialUploadMilliseconds')}
        uploads[str(run['mode'])]['scope'] = details['initialUploadTimingScope']
    require(metrics['0']['activeCountRange'] == metrics['1']['activeCountRange'], 'Active count mismatch')
    # Tiny sort-count fluctuations are pre-existing tight2 behavior; preserve their measured range.
    images = compare_images(*[Path(r['directory']) for r in runs if r['kind'] == 'capture'])
    changes = {label: (metrics['1'][label]['mean'] / metrics['0'][label]['mean'] - 1) * 100
               for label in ('Preprocess', 'Raster', 'WholeFrame', 'FrameInterval')}
    changes['FPS'] = (metrics['1']['FPS'] / metrics['0']['FPS'] - 1) * 100
    savings = {key: memory['0'][key] - memory['1'][key] for key in MEMORY_KEYS}
    return {'status': images['numericStatus'], 'scope': 'One short pair; no confidence interval or cross-device claim',
            'timingsMilliseconds': metrics, 'changePercent': changes, 'memoryBytes': memory,
            'memoryReductionBytes': savings, 'uploadAndConversion': uploads, 'images': images,
            'environment': baseline_identity, 'protocol': configs[0], 'inputIdentities': manifest['identity'],
            'visualAcceptance': 'Pending human/agent visual inspection of captures and native full-window FPS screenshots',
            'memoryScope': 'Actual source/SH RHI reservation and backing; allocator ranges engine-only; process/driver VRAM unavailable',
            'gpuTimingScope': 'WholeFrame excludes present/compositor latency; Raster/Preprocess overlap WholeFrame; do not sum'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.manifest)
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as exc:
        result = {'status': 'FAIL', 'errors': [str(exc)]}
    try:
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    except OSError as exc:
        parser.exit(2, str(exc) + '\n')
    print(result['status'])
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
