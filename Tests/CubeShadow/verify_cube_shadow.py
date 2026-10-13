"""Verify real Vulkan shadowing using resolved swapchain GPU readback."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image


def capture(exe, root, output, shadows):
    stem = 'shadow-on' if shadows else 'shadow-off'
    path = output / (stem + '.ppm')
    args = [str(exe), '--cube-showcase', '--cube-capture', str(path)]
    if not shadows:
        args.append('--cube-no-shadows')
    env = dict(os.environ)
    env.pop('MONSTER_SPLAT_DIAG', None)
    with (output / (stem + '.log')).open('w') as log:
        result = subprocess.run(args, cwd=root, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=60)
    assert result.returncode == 0, f'{stem}: demo exit {result.returncode}'
    assert path.exists(), f'{stem}: GPU capture must exist'
    runtime = (output / (stem + '.log')).read_text(errors='replace')
    assert 'VUID-' not in runtime, f'{stem}: Vulkan validation error'
    with Image.open(path) as image:
        assert image.format == 'PPM', f'{stem}: expected lossless real GPU readback'
        image.convert('RGB').save(output / (stem + '.png'))
        return np.asarray(image.convert('RGB'), dtype=np.float32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    on = capture(args.exe, args.root, args.output, True)
    off = capture(args.exe, args.root, args.output, False)
    assert on.shape == off.shape, 'fixed-camera captures must align'
    luminance = np.array([.2126, .7152, .0722])
    lum_on, lum_off = on @ luminance, off @ luminance
    darkening = lum_off - lum_on
    shadow = darkening > 15
    unchanged = float(np.mean(np.max(np.abs(on - off), axis=2) < 2))
    visible = lum_on > 20
    metrics = dict(width=on.shape[1], height=on.shape[0],
                   shadow_pixels=int(shadow.sum()), unchanged_fraction=unchanged,
                   mean_shadow_darkening=float(darkening[shadow].mean()) if shadow.any() else 0,
                   mean_visible_luminance=float(lum_on[visible].mean()),
                   bright_clipped_fraction=float(np.mean(np.any(on >= 250, axis=2))),
                   unexpectedly_brightened_pixels=int(np.sum(darkening < -2)))
    (args.output / 'metrics.json').write_text(json.dumps(metrics, indent=2))
    Image.fromarray(np.uint8(np.clip(np.abs(on - off) * 4, 0, 255))).save(args.output / 'difference.png')
    assert metrics['shadow_pixels'] > 500, 'shadow sampling must visibly darken a receiver region'
    assert unchanged > .70, 'camera and material must remain aligned for causal comparison'
    assert metrics['unexpectedly_brightened_pixels'] == 0, 'shadow toggle must only darken pixels'
    assert metrics['mean_visible_luminance'] > 65, 'lighting must remain readable'
    assert metrics['bright_clipped_fraction'] < .01, 'lighting must avoid clipping'
    print(json.dumps(metrics))
    print('PASS: resolved GPU capture, shadow causality, readability, clipping and Vulkan validation')


if __name__ == '__main__':
    main()
