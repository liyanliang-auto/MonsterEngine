"""Write a bounded, asymmetric colored degree3 Gaussian for preview diagnostics."""
import argparse
import math
from pathlib import Path
import struct


def create(output):
    names = ['x', 'y', 'z', 'nx', 'ny', 'nz']
    names += [f'f_dc_{i}' for i in range(3)] + [f'f_rest_{i}' for i in range(45)]
    names += ['opacity', 'scale_0', 'scale_1', 'scale_2', 'rot_0', 'rot_1', 'rot_2', 'rot_3']
    records = []
    for index, (point, color) in enumerate([
            ((-.3,-.1,-.1), (.9,.15,.1)), ((.25,.2,.1), (.1,.85,.2)),
            ((.05,-.25,.05), (.15,.2,.9))]):
        values = list(point) + [0, 0, 0]
        values += [(v-.5)/.28209479177387814 for v in color]
        # Channel-major coefficient pattern is asymmetric; no image fit.
        values += [.6*math.sin(1.2*i+.8*c+index) for c in range(3) for i in range(15)]
        values += [4.0, math.log(.15), math.log(.12), math.log(.14), .9, .2, .1, -.3]
        records += values
    header = 'ply\nformat binary_little_endian 1.0\nelement vertex 3\n'
    header += ''.join('property float '+name+'\n' for name in names) + 'end_header\n'
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(header.encode('ascii') + struct.pack('<'+'f'*len(records), *records))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output')
    create(parser.parse_args().output)
