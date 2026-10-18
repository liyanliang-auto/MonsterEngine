"""Generate the original, redistributable MonsterEngine mobile 3DGS scene.

No model download, random state, image texture or third-party dataset is used.
The fixed SH0 scene is a colored vertical torus, an orange sphere and tiled floor.
PLY stores log standard deviations, opacity logits and a wxyz quaternion, exactly
as consumed by the shared PLY parser. See resources/android/builtin-scene-license.txt.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile


GENERATOR_VERSION = "MonsterEngine-ProceduralBuiltin-1"
GAUSSIAN_COUNT = 49152
SH_C0 = 0.28209479177387814
RECORD = struct.Struct("<14f")
PROPERTIES = (
    "x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
    "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3",
)
CAMERA = {
    "eye": [0.0, 1.2, 5.5], "target": [0.0, 0.0, 0.0], "up": [0.0, 1.0, 0.0],
    "verticalFovDegrees": 45.0, "nearPlane": 0.05, "farPlane": 100.0,
}


def shade(rgb, normal):
    """Bake a directional diffuse term into SH0, independent of camera direction."""
    light = (-0.4, 0.8, 0.6)
    length = math.sqrt(sum(value * value for value in light))
    diffuse = max(0.0, sum(a * b for a, b in zip(normal, light)) / length)
    return tuple(max(0.0, min(1.0, value * (0.35 + 0.65 * diffuse))) for value in rgb)


def gaussians():
    """Yield exactly 49,152 surface samples in stable component/row/column order."""
    # 256 major x 64 minor samples: a vertical torus around the world Z axis.
    for major in range(256):
        u = math.tau * (major + 0.5) / 256
        cu, su = math.cos(u), math.sin(u)
        color = tuple(0.55 + 0.40 * math.cos(u + phase)
                      for phase in (0.0, -math.tau / 3, math.tau / 3))
        for minor in range(64):
            v = math.tau * (minor + 0.5) / 64
            cv, sv = math.cos(v), math.sin(v)
            radius = 1.05 + 0.30 * cv
            position = (-0.40 + radius * cu, 0.12 + radius * su, 0.30 * sv)
            yield position, shade(color, (cu * cv, su * cv, sv)), 0.014, 0.96

    # 128 x 128 equal-area sphere samples avoid dense polar piles.
    for row in range(128):
        ny = 1 - 2 * (row + 0.5) / 128
        ring = math.sqrt(max(0.0, 1 - ny * ny))
        for column in range(128):
            angle = math.tau * (column + 0.5 + (row % 2) * 0.5) / 128
            nx, nz = ring * math.cos(angle), ring * math.sin(angle)
            position = (0.95 + 0.58 * nx, -0.12 + 0.58 * ny, -0.38 + 0.58 * nz)
            stripe = 0.5 + 0.5 * math.sin(12 * ny)
            color = (0.96, 0.32 + 0.25 * stripe, 0.10 + 0.08 * stripe)
            yield position, shade(color, (nx, ny, nz)), 0.009, 0.96

    # 128 x 128 floor samples: independent depth/occlusion and scale references.
    for row in range(128):
        z = -2.6 + 5.2 * (row + 0.5) / 128
        for column in range(128):
            x = -2.6 + 5.2 * (column + 0.5) / 128
            tile = (row // 16 + column // 16) % 2
            color = (0.32, 0.40, 0.50) if tile else (0.66, 0.70, 0.72)
            yield (x, -1.28, z), shade(color, (0, 1, 0)), 0.019, 0.90


def encode_record(position, color, scale, opacity):
    """Encode activated values into strict shared-loader 3DGS PLY semantics."""
    if not 0 < opacity < 1 or not math.isfinite(scale) or scale <= 0:
        raise ValueError("Invalid procedural Gaussian activation")
    dc = tuple((channel - 0.5) / SH_C0 for channel in color)
    log_scale = math.log(scale)
    values = (*position, *dc, math.log(opacity / (1 - opacity)),
              log_scale, log_scale, log_scale, 1.0, 0.0, 0.0, 0.0)
    if not all(math.isfinite(value) for value in values):
        raise ValueError("Nonfinite procedural Gaussian")
    # Quantization removes insignificant host libm differences before float32
    # packing. No timestamps, absolute paths or random IDs enter the asset.
    return RECORD.pack(*(round(value, 7) for value in values))


def header():
    """ASCII PLY header with fixed provenance, activation layout and camera."""
    lines = ["ply", "format binary_little_endian 1.0",
             "comment generator " + GENERATOR_VERSION,
             "comment original procedural geometry; no external dataset",
             "comment license resources/android/builtin-scene-license.txt",
             "comment SH degree 0; scales log_stddev; opacity logit; quaternion wxyz",
             "comment camera " + json.dumps(CAMERA, sort_keys=True, separators=(",", ":")),
             "element vertex " + str(GAUSSIAN_COUNT)]
    lines.extend("property float " + name for name in PROPERTIES)
    lines.append("end_header")
    return ("\n".join(lines) + "\n").encode("ascii")


def generate(output):
    """Stream and atomically publish the scene; return its measured content identity."""
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    digest = hashlib.sha256()
    count = 0
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent,
                                         prefix=".builtin-", delete=False) as stream:
            temporary_path = Path(stream.name)
            initial = header()
            stream.write(initial)
            digest.update(initial)
            for sample in gaussians():
                record = encode_record(*sample)
                stream.write(record)
                digest.update(record)
                count += 1
            if count != GAUSSIAN_COUNT:
                raise ValueError("Procedural Gaussian count changed unexpectedly")
            stream.flush()
            os.fsync(stream.fileno())
        size = temporary_path.stat().st_size
        if size != len(initial) + count * RECORD.size:
            raise OSError("Generated PLY has an unexpected byte length")
        os.replace(temporary_path, output)
        return {"generator": GENERATOR_VERSION, "gaussians": count, "shDegree": 0,
                "bytes": size, "sha256": digest.hexdigest()}
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main():
    """Build-time entry: --output matches Tools/Android/prepare_assets.py."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="Destination binary 3DGS PLY")
    args = parser.parse_args()
    print(json.dumps(generate(args.output), sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, OverflowError, struct.error) as error:
        print("Builtin scene generation failed: " + str(error), file=sys.stderr)
        sys.exit(1)
