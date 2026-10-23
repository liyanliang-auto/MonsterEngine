"""Generate a NEW legacy-renderer same-camera reference and diagnostic mutants.

This tool never modifies the source PLY, current project, or production code.
Camera values come from the GPU frame's exact std140 UBO, not image fitting.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

import numpy as np
from PIL import Image


def identity(path):
    path = Path(path)
    data = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def run(captures, source, executable, shaders):
    captures, source = Path(captures), Path(source)
    workspace = captures / 'legacy-reference'
    workspace.mkdir(exist_ok=False)
    metadata = json.loads((captures / 'initial.json').read_text())
    uniform_hex = metadata['frame']['viewerFrame']['cameraProvenance']['uniformsHex']
    values = struct.unpack('<40f4i', bytes.fromhex(uniform_hex))
    width, height, degree = values[40:43]
    near = values[30] / values[26]
    far = values[30] / (values[26] + 1)
    request = dict(schemaVersion=1, cameraId='studio-new-host-same-camera', width=width, height=height,
                   fx=values[36], fy=values[37], cx=(width-1)/2, cy=(height-1)/2,
                   nearPlane=near, farPlane=far, viewMatrix=list(values[:16]), shDegree=degree,
                   policySHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   auditSHA256=hashlib.sha256(bytes.fromhex(uniform_hex)).hexdigest(),
                   protectedInputsSHA256=identity(source)['sha256'])
    # Freeze all comparison tolerances BEFORE making any reference output.
    policy = dict(maeMax=2/255, p99Max=12/255, mutantMAEMin=5/255,
                  convention='legacy raw bottom_left -> top_left via exact row flip, identity RGB transfer',
                  source=identity(source), camera=request, executable=identity(executable),
                  shaders=[identity(p) for p in sorted(Path(shaders).glob('*.spv'))])
    (workspace / 'preregistered-policy.json').write_text(json.dumps(policy, indent=2))
    raw = source.read_bytes()
    offset = raw.index(b'end_header\n') + len(b'end_header\n')
    header = raw[:offset].decode('ascii')
    if 'format binary_little_endian 1.0' not in header:
        raise ValueError('this diagnostic mutation writer requires LE float PLY')
    properties = [line.split()[2] for line in header.splitlines() if line.startswith('property float ')]
    count = int(next(line.split()[2] for line in header.splitlines() if line.startswith('element vertex ')))
    rows = np.frombuffer(raw[offset:], dtype='<f4').reshape(count, len(properties))
    names = {name: i for i, name in enumerate(properties)}
    outputs = {}
    commands = []
    for label in ['reference', 'mirrorX', 'mirrorY', 'swapRB', 'wrongSHLayout']:
        location = workspace / label
        location.mkdir()
        mutant = rows.copy()
        if label == 'mirrorX':
            mutant[:, names['x']] *= -1
        elif label == 'mirrorY':
            mutant[:, names['y']] *= -1
        elif label == 'swapRB':
            a, b = names['f_dc_0'], names['f_dc_2']
            mutant[:, [a, b]] = mutant[:, [b, a]]
            for coefficient in range(15):
                a, b = names[f'f_rest_{coefficient}'], names[f'f_rest_{coefficient+30}']
                mutant[:, [a, b]] = mutant[:, [b, a]]
        elif label == 'wrongSHLayout':
            indices = [names[f'f_rest_{i}'] for i in range(45)]
            mutant[:, indices] = mutant[:, indices].reshape(count, 3, 15).transpose(0, 2, 1).reshape(count, 45)
        ply = source
        if label != 'reference':
            ply = location / 'diagnostic-mutant.ply'
            ply.write_bytes(raw[:offset] + mutant.astype('<f4').tobytes())
        current = dict(request, sourcePLY=str(ply.resolve()), outputDirectory=str((location / 'native').resolve()))
        request_file = location / 'request.json'
        request_file.write_text(json.dumps(current, indent=2))
        command = [str(Path(executable).resolve()), '--splat-calibrated-capture', str(request_file.resolve())]
        with (location / 'process.log').open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120,
                                    env=dict(os.environ, MONSTER_SPLAT_SHADER_DIR=str(Path(shaders).resolve())))
        commands.append(dict(command=command, exitCode=result.returncode, request=identity(request_file)))
        (workspace / 'commands.json').write_text(json.dumps(commands, indent=2))
        if result.returncode:
            raise RuntimeError(label + ' calibrated GPU reference failed')
        pixels = np.asarray(Image.open(location / 'native/capture_000.ppm'))[::-1]
        output = location / 'top-left.png'
        Image.fromarray(pixels).save(output)
        outputs[label] = str(output.resolve())
    pixels = np.asarray(Image.open(outputs['reference']), dtype=float)/255
    display = np.where(pixels <= .0031308, 12.92*pixels, 1.055*pixels**(1/2.4)-.055)
    transfer = workspace / 'wrong-transfer.png'
    Image.fromarray(np.uint8(np.clip(np.rint(display*255), 0, 255))).save(transfer)
    outputs['wrongColorTransfer'] = str(transfer.resolve())
    contract = dict(sourceSHA256=identity(source)['sha256'], uniformsHex=uniform_hex,
                    effectiveSHDegree=degree, image=outputs.pop('reference'), mutants=outputs,
                    preregisteredPolicy=identity(workspace/'preregistered-policy.json'),
                    note='New legacy FSplatPLYLoader -> pipeline mode A independent of Studio host and session upload routing')
    (captures / 'reference-contract.json').write_text(json.dumps(contract, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ['captures', 'source', 'executable', 'shaders']:
        parser.add_argument('--' + name, required=True)
    run(**vars(parser.parse_args()))
