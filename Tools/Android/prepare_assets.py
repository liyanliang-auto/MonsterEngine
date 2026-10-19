"""Stage only validated generated assets; publish the identity manifest last."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def scene_policy(source_root):
    return json.loads((source_root / "resources/android/default-viewer.json").read_text(encoding="utf-8"))["builtinScene"]


def checked_files(root, entries):
    names = [entry["path"] for entry in entries]
    if "scene.mgs" not in names or len(names) != len(set(names)):
        raise ValueError("LOD package requires one manifest and unique file names")
    for entry in entries:
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts or "\\" in entry["path"] or ":" in entry["path"]:
            raise ValueError("Unsafe LOD asset name")
        path = root / relative
        if not path.is_file() or path.is_symlink() or path.resolve().is_relative_to(root.resolve()) is False:
            raise ValueError("LOD asset must be a confined ordinary file")
        if path.stat().st_size != entry["bytes"] or entry["bytes"] > 64 * 1024 * 1024 or digest(path) != entry["sha256"]:
            raise ValueError(f"LOD asset identity/admission mismatch: {relative}")
        yield relative, path


def validate_lod_identity(scene, identity, validation):
    # Read only the stable v3 identity prefix; the native CLI validates all
    # format semantics, checksums, counts and page payloads independently.
    with (scene / "scene.mgs").open("rb") as stream:
        if struct.unpack("<III", stream.read(12))[:2] != (0x4D53474D, 3):
            raise ValueError("Expected native MobileLOD manifest v3")
        fields = []
        for _ in range(5):
            length = struct.unpack("<I", stream.read(4))[0]
            if length > 65536:
                raise ValueError("LOD identity string exceeds limit")
            fields.append(stream.read(length).decode("utf-8"))
    source_hash, builder_version, builder_hash, _, canonical_config = fields
    effective = json.loads(canonical_config)
    if (source_hash != identity["source"]["sha256"] or builder_version != "MonsterEngine-MobileLOD-1" or
            builder_hash != identity["builderSHA256"] or
            any(effective.get(key) != value for key, value in identity["config"].items()) or
            digest(scene / "scene.mgs") != validation["contentSHA256"]):
        raise ValueError("Native LOD source/tool/config identity mismatch")


def exact_source(source_root, expected):
    source = source_root / expected["path"]
    if source.stat().st_size != expected["bytes"] or digest(source) != expected["sha256"]:
        raise ValueError("Exact builtin source identity mismatch; input was preserved")
    with source.open("rb") as stream:
        header = stream.read(65536).split(b"end_header", 1)[0]
    if f"element vertex {expected['records']}".encode() not in header.splitlines():
        raise ValueError("Exact builtin source record count mismatch")
    return source


def prepare_lod(args):
    policy = scene_policy(args.source_root)
    expected = policy["source"]
    source = exact_source(args.source_root, expected)
    if not args.builder or not args.builder.is_file():
        raise ValueError("Supply --builder / SPLAT_MOBILE_LOD_BUILDER as the built host SplatMobileLODBuilder executable")
    builder = args.builder.resolve()
    config = json.dumps(policy["buildConfig"], sort_keys=True, separators=(",", ":"))
    identity = {"source": expected, "config": policy["buildConfig"], "builderSHA256": digest(builder)}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    scene = args.output / key / "scene"
    config_path = args.output / key / "config.json"
    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(config + "\n", encoding="utf-8")
    # Existing durable output is reused only for this exact source/config/tool key;
    # the native validator still reads every manifest/page on every invocation.
    if not scene.exists():
        subprocess.run([str(builder), "--input", str(source), "--job-directory", str(config_path.parent / "journal"),
                        "--output", str(scene), "--config", str(config_path)], check=True)
    result = subprocess.run([str(builder), "--validate-scene", str(scene)], check=True, capture_output=True, text=True)
    validation = json.loads(result.stdout)
    if not validation["ok"] or validation["sourceRecords"] != expected["records"]:
        raise ValueError("Native bounded LOD source count validation failed")
    validate_lod_identity(scene, identity, validation)
    entries = [{"path": path.relative_to(scene).as_posix(), "bytes": path.stat().st_size, "sha256": digest(path)}
               for path in sorted(scene.rglob("*")) if path.is_file() and
               (path.relative_to(scene).as_posix() == "scene.mgs" or
                (path.relative_to(scene).parts[0] == "pages" and path.suffix == ".mgsp"))]
    list(checked_files(scene, entries))
    if "scene.mgs" not in {entry["path"] for entry in entries}:
        raise ValueError("Native builder did not publish scene.mgs")
    document = {"schemaVersion": 1, **identity, "sceneDirectory": f"{key}/scene", "validation": validation, "files": entries}
    temporary = args.output / "builtin-lod.json.tmp"
    temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, args.output / "builtin-lod.json")
    print(json.dumps({"ok": True, "sourceRecords": validation["sourceRecords"], "pageCount": validation["pageCount"],
                      "contentSHA256": validation["contentSHA256"]}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--shader-dir", type=Path)
    parser.add_argument("--prepare-lod", action="store_true")
    parser.add_argument("--builder", type=Path)
    parser.add_argument("--lod-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.prepare_lod:
        prepare_lod(args)
        return
    if not args.shader_dir:
        raise ValueError("--shader-dir is required for APK asset staging")
    lod_dir = args.lod_dir or args.source_root / "build/android-builtin-lod"
    lod = json.loads((lod_dir / "builtin-lod.json").read_text(encoding="utf-8"))
    policy = scene_policy(args.source_root)
    if lod["schemaVersion"] != 1 or lod["source"] != policy["source"] or lod["config"] != policy["buildConfig"]:
        raise ValueError("Stale builtin LOD; run build_android.ps1 or --prepare-lod with the host builder")
    exact_source(args.source_root, policy["source"])
    scene = lod_dir / lod["sceneDirectory"]
    if not scene.resolve().is_relative_to(lod_dir.resolve()):
        raise ValueError("LOD scene directory escapes generated root")
    if not lod["validation"]["ok"] or lod["validation"]["sourceRecords"] != policy["source"]["records"]:
        raise ValueError("Missing successful exact-source native LOD validation")
    validate_lod_identity(scene, lod, lod["validation"])
    manifest = json.loads((args.shader_dir / "shader-manifest.json").read_text(encoding="utf-8"))
    if manifest["schemaVersion"] != 1 or not manifest["shaders"]:
        raise ValueError("Missing supported shader manifest")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Each ABI can build concurrently. Independent scratch roots and atomic file
    # replacement prevent partially written APK assets in the shared variant root.
    with tempfile.TemporaryDirectory(prefix="monster-assets-", dir=args.output.parent) as scratch:
        staging = Path(scratch)
        (staging / "scenes").mkdir()
        # Older host indexes also inventoried the builder's durable publication
        # marker. It belongs to the host transaction, never to the runtime scene.
        runtime_files = [entry for entry in lod["files"] if entry["path"] != ".monster-publication"]
        for relative, source in checked_files(scene, runtime_files):
            target = staging / "scenes/bonsai-lod" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        (staging / "scenes/builtin-lod.json").write_text(
            json.dumps({**lod, "files": runtime_files}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
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
        # Only this generated asset tree is owned here. Remove artifacts left by
        # previous asset layouts (notably the oversized raw PLY) before publishing.
        desired = {entry["path"] for entry in entries} | {"asset-manifest.json"}
        for stale in args.output.rglob("*"):
            if stale.is_file() and stale.relative_to(args.output).as_posix() not in desired:
                stale.unlink()
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
    except (OSError, ValueError, KeyError, struct.error, subprocess.SubprocessError) as error:
        print(f"Asset staging failed: {error}", file=sys.stderr)
        sys.exit(1)
