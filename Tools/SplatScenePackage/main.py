"""Bounded schema-1 .monsterlod interchange; native validator is the semantic authority."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile

MAX_ARCHIVE = 0xffffffff
MAX_EXPANDED = 16 << 30
MAX_ENTRY = 256 << 20
MAX_METADATA = 16 << 20
MAX_ENTRIES = 65534
DISK_RESERVE = 256 << 20


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def safe_name(name):
    require(isinstance(name, str) and 0 < len(name) <= 4096, "Invalid package name length")
    require(all(32 <= ord(c) <= 126 for c in name) and "\\" not in name and ":" not in name,
            "Package names must be portable ASCII")
    require(all(part and part not in (".", "..") and not part.endswith((".", " "))
                for part in name.split("/")), "Unsafe package path")
    return name


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        while data := source.read(65536):
            result.update(data)
    return result.hexdigest()


def disk_admission(path, next_bytes):
    require(shutil.disk_usage(path).free >= DISK_RESERVE + next_bytes, "Insufficient disk headroom")


def native_validate(path, validator):
    require(validator.is_file(), "Native scene validator executable missing")
    subprocess.run([str(validator.resolve()), "--validate-scene", str(path.resolve())], check=True)


def inspect(path):
    """Admit central directory before ZipFile allocates its decoded entry objects."""
    size = path.stat().st_size
    require(22 <= size <= MAX_ARCHIVE, "Unsupported package size")
    with path.open("rb") as source:
        begin = max(0, size - 65557)
        source.seek(begin)
        tail = source.read()
        end = None
        for at in range(len(tail) - 22, -1, -1):
            if tail[at:at + 4] == b"PK\x05\x06" and at + 22 + struct.unpack_from("<H", tail, at + 20)[0] == len(tail):
                end = at
                break
        require(end is not None, "Missing ZIP end record")
        _, disk, central_disk, count_disk, count, central_bytes, offset, _ = struct.unpack_from("<4s4H2IH", tail, end)
        require(disk == central_disk == 0 and count == count_disk and 0 < count <= MAX_ENTRIES,
                "Multidisk/ZIP64 or entry limit unsupported")
        require(central_bytes <= MAX_METADATA and offset + central_bytes == begin + end,
                "Invalid or excessive central directory")
    return count


def inventory(zip_file, expected_count):
    entries = zip_file.infolist()
    require(len(entries) == expected_count, "Central entry count mismatch")
    seen, metadata, expanded = set(), 0, 0
    for entry in entries:
        name = safe_name(entry.filename)
        require(name.casefold() not in ("descriptor.json", "delete-request.json"), "Reserved scene-store entry")
        require(name.casefold() not in seen, "Duplicate normalized entry")
        seen.add(name.casefold())
        require(entry.flag_bits & 0xf7f1 == 0 and entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                "Encrypted/unsupported compression")
        mode = (entry.external_attr >> 16) & 0xf000
        require(mode in (0, stat.S_IFREG) and not entry.external_attr & 0x10,
                "Links, directories and special files forbidden")
        require(entry.extract_version <= 20 and 0 <= entry.file_size <= MAX_ENTRY and
                entry.compress_size < 0xffffffff and entry.file_size < 0xffffffff,
                "ZIP64/entry size unsupported")
        metadata += 512 + len(name) * 4
        expanded += entry.file_size
        require(metadata <= MAX_METADATA and expanded <= MAX_EXPANDED, "Package metadata/expanded admission exceeded")
    require("package.json" in {e.filename for e in entries} and "scene.mgs" in {e.filename for e in entries},
            "Missing package descriptor/manifest")
    return entries


def descriptor_validate(directory, entries):
    descriptor_path = directory / "package.json"
    require(0 < descriptor_path.stat().st_size <= MAX_METADATA, "Package descriptor too large")
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate JSON key")
            result[key] = value
        return result
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    require(descriptor["schemaVersion"] == 1 and descriptor["format"] == "monsterlod" and descriptor["manifest"] == "scene.mgs",
            "Unsupported package schema")
    files = descriptor["files"]
    require(isinstance(files, list) and len(files) == len(entries) - 1, "Package inventory count mismatch")
    names = set()
    for item in files:
        name = safe_name(item["path"])
        require(name != "package.json" and name not in names, "Duplicate inventory path")
        names.add(name)
        target = directory / name
        require(type(item["bytes"]) is int and 0 <= item["bytes"] <= MAX_ENTRY and target.stat().st_size == item["bytes"],
                "Inventory byte count mismatch")
        require(isinstance(item["sha256"], str) and len(item["sha256"]) == 64 and
                all(c in "0123456789abcdef" for c in item["sha256"]) and digest(target) == item["sha256"],
                "Package hash mismatch")
    require(names == {e.filename for e in entries if e.filename != "package.json"}, "Inventory coverage mismatch")


def extract(archive, destination, validator):
    count = inspect(archive)
    with zipfile.ZipFile(archive) as zipped:
        entries = inventory(zipped, count)
        actual = 0
        for entry in entries:
            target = destination / entry.filename
            target.parent.mkdir(parents=True, exist_ok=True)
            copied = 0
            # ZipFile checks local-header consistency and CRC at EOF. Actual byte
            # limits additionally protect against forged uncompressed metadata.
            with zipped.open(entry) as source, target.open("xb") as output:
                while data := source.read(65536):
                    copied += len(data)
                    actual += len(data)
                    require(copied <= entry.file_size and copied <= MAX_ENTRY and actual <= MAX_EXPANDED,
                            "Actual expansion admission exceeded")
                    disk_admission(destination, len(data))
                    output.write(data)
                output.flush()
                os.fsync(output.fileno())
            require(copied == entry.file_size, "Short ZIP entry")
        descriptor_validate(destination, entries)
    native_validate(destination, validator)


def source_files(directory):
    result, seen, stack, metadata, total = [], set(), [directory], 0, 0
    while stack:
        parent = stack.pop()
        with os.scandir(parent) as items:
            for entry in items:
                require(not entry.is_symlink(), "Source links forbidden")
                if entry.is_dir(follow_symlinks=False):
                    stack.append(Path(entry.path))
                    require(len(stack) <= MAX_ENTRIES, "Directory count exceeded")
                else:
                    require(entry.is_file(follow_symlinks=False), "Source special file forbidden")
                    path = Path(entry.path)
                    name = path.relative_to(directory).as_posix()
                    if name in ("descriptor.json", "delete-request.json", "package.json"):
                        continue
                    safe_name(name)
                    require(name.casefold() not in seen, "Duplicate portable source name")
                    seen.add(name.casefold())
                    size = entry.stat(follow_symlinks=False).st_size
                    metadata += 512 + len(name) * 4
                    total += size
                    require(size <= MAX_ENTRY and metadata <= MAX_METADATA and total <= MAX_EXPANDED and len(result) < MAX_ENTRIES - 1,
                            "Source package admission exceeded")
                    result.append((name, path, size))
    require(any(name == "scene.mgs" for name, _, _ in result), "Source has no scene.mgs")
    return sorted(result)


def sync_directory(path):
    if hasattr(os, "O_DIRECTORY"):
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def pack(directory, output, validator, stored):
    require(directory.is_dir() and not directory.is_symlink() and not output.exists(), "Invalid source/output")
    native_validate(directory, validator)
    files = source_files(directory)
    document = {"schemaVersion": 1, "format": "monsterlod", "manifest": "scene.mgs", "files": [
        {"path": name, "bytes": size, "sha256": digest(path)} for name, path, size in files]}
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
    require(len(encoded) <= MAX_METADATA, "Inventory JSON admission exceeded")
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix="monsterlod-", suffix=".part", dir=output.parent)
    os.close(fd)
    temporary = Path(temporary_name)
    try:
        method = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
        with zipfile.ZipFile(temporary, "w", compression=method, allowZip64=False) as zipped:
            zipped.writestr("package.json", encoded)
            for index, (name, path, size) in enumerate(files):
                disk_admission(output.parent, min(size, 65536))
                info = zipfile.ZipInfo(name)
                info.compress_type = method
                info.file_size = size
                info.external_attr = (stat.S_IFREG | 0o600) << 16
                with path.open("rb") as source, zipped.open(info, "w") as target:
                    copied = 0
                    copied_hash = hashlib.sha256()
                    while data := source.read(65536):
                        copied += len(data)
                        require(copied <= size, "Source changed during packaging")
                        disk_admission(output.parent, len(data))
                        target.write(data)
                        copied_hash.update(data)
                        require(temporary.stat().st_size <= MAX_ARCHIVE, "Package size admission exceeded")
                    require(copied == size, "Source changed during packaging")
                    require(copied_hash.hexdigest() == document["files"][index]["sha256"], "Source content changed during packaging")
        require(temporary.stat().st_size <= MAX_ARCHIVE, "Final package size admission exceeded")
        inspect(temporary)
        with temporary.open("r+b") as source:
            os.fsync(source.fileno())
        os.rename(temporary, output)
        sync_directory(output.parent)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("pack", "unpack", "validate"):
        command = commands.add_parser(name)
        command.add_argument("source", type=Path)
        if name != "validate":
            command.add_argument("output", type=Path)
        command.add_argument("--validator", type=Path, required=True,
                             help="SplatMobileLODBuilder executable supporting --validate-scene DIRECTORY")
        if name == "pack":
            command.add_argument("--stored", action="store_true")
    args = parser.parse_args()
    if args.command == "pack":
        pack(args.source, args.output, args.validator, args.stored)
    elif args.command == "unpack":
        require(not args.output.exists(), "Output already exists")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="monsterlod-", dir=args.output.parent) as temporary:
            staging = Path(temporary) / "scene"
            staging.mkdir()
            extract(args.source, staging, args.validator)
            sync_directory(staging)
            os.rename(staging, args.output)
            sync_directory(args.output.parent)
    else:
        with tempfile.TemporaryDirectory(prefix="monsterlod-verify-") as temporary:
            extract(args.source, Path(temporary), args.validator)
    print(json.dumps({"schemaVersion": 1, "ok": True, "operation": args.command}))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, zipfile.BadZipFile, zipfile.LargeZipFile) as error:
        print(f"Scene package failed: {error}", file=sys.stderr)
        sys.exit(1)
