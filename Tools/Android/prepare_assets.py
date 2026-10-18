"""Stage only validated generated assets; publish the identity manifest last."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--shader-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.shader_dir / "shader-manifest.json").read_text(encoding="utf-8"))
    if manifest["schemaVersion"] != 1 or not manifest["shaders"]:
        raise ValueError("Missing supported shader manifest")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Each ABI can build concurrently. Independent scratch roots and atomic file
    # replacement prevent partially written APK assets in the shared variant root.
    with tempfile.TemporaryDirectory(prefix="monster-assets-", dir=args.output.parent) as scratch:
        staging = Path(scratch)
        (staging / "scenes").mkdir()
        subprocess.run([sys.executable, str(args.source_root / "Tools/Android/generate_builtin_scene.py"),
                        "--output", str(staging / "scenes/builtin.ply")], check=True)
        (staging / "resources/android").mkdir(parents=True)
        config = args.source_root / "resources/android/default-viewer.json"
        json.loads(config.read_text(encoding="utf-8"))
        shutil.copyfile(config, staging / "resources/android/default-viewer.json")
        shutil.copyfile(args.source_root / "resources/android/builtin-scene-license.txt",
                        staging / "resources/android/builtin-scene-license.txt")
        (staging / "Shaders/Splat").mkdir(parents=True)
        for shader in manifest["shaders"]:
            name = shader["path"]
            if Path(name).name != name or not name.endswith(".spv"):
                raise ValueError("Unsafe shader logical name")
            source = args.shader_dir / name
            if digest(source) != shader["sha256"]:
                raise ValueError(f"Shader identity mismatch: {name}")
            shutil.copyfile(source, staging / "Shaders/Splat" / name)
        shutil.copyfile(args.shader_dir / "shader-manifest.json", staging / "Shaders/Splat/shader-manifest.json")
        entries = [{"path": path.relative_to(staging).as_posix(), "bytes": path.stat().st_size,
                    "sha256": digest(path)} for path in sorted(staging.rglob("*")) if path.is_file()]
        document = {"schemaVersion": 1, "assets": entries}
        (staging / "asset-manifest.json").write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        args.output.mkdir(parents=True, exist_ok=True)
        for entry in entries + [{"path": "asset-manifest.json"}]:
            relative = entry["path"]
            target = args.output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            # Copy onto the destination filesystem before atomic replacement.
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
                temporary_path = Path(temporary.name)
                with (staging / relative).open("rb") as source:
                    shutil.copyfileobj(source, temporary)
            try:
                os.replace(temporary_path, target)
            finally:
                temporary_path.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Asset staging failed: {error}", file=sys.stderr)
        sys.exit(1)
