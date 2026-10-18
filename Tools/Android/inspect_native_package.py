#!/usr/bin/env python3
"""Inspect real APK/AAB native payloads; static alignment is not runtime proof."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import sys
import zipfile

PAGE = 16384
MACHINES = {"arm64-v8a": 183, "x86_64": 62}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect_elf(data, abi):
    """Validate ELF64 LE bounds, machine and every PT_LOAD alignment contract."""
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("Expected ELF64 little endian version 1")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    _, kind, machine, version, _, phoff, _, _, ehsize, phsize, phnum, *_ = header
    if kind != 3 or version != 1 or machine != MACHINES[abi] or ehsize != 64:
        raise ValueError("ELF identity/type/ABI mismatch")
    if phsize != 56 or not 0 < phnum <= 4096 or phoff > len(data) or phnum * phsize > len(data) - phoff:
        raise ValueError("Invalid ELF program-header bounds")
    loads = []
    for index in range(phnum):
        ptype, flags, offset, address, _, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phsize)
        if ptype != 1:
            continue
        if filesz > memsz or offset > len(data) or filesz > len(data) - offset:
            raise ValueError("PT_LOAD file bounds invalid")
        aligned = align >= PAGE and align & (align - 1) == 0 and (address - offset) % PAGE == 0 and (address - offset) % align == 0
        loads.append({"offset": offset, "virtualAddress": address, "alignment": align, "flags": flags, "page16KiBCompatible": aligned})
    if not loads or not all(segment["page16KiBCompatible"] for segment in loads):
        raise ValueError("PT_LOAD does not satisfy 16 KiB alignment")
    return {"machine": machine, "loadSegments": loads, "elf16KiBCompatible": True}


def inspect_package(path, expected):
    """Inspect embedded native ELF and APK local-header payload offsets."""
    result = {"artifact": path.name, "kind": path.suffix.lower()[1:], "bytes": path.stat().st_size,
              "sha256": file_hash(path), "native": [], "assets": [], "errors": [],
              "runtime16KiBVerified": False, "runtimeReason": "Static inspection only; install/run on actual 16 KiB environment separately"}
    seen, abis = set(), set()
    apk = path.suffix.lower() == ".apk"
    with path.open("rb") as raw, zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            name = entry.filename
            parts = PurePosixPath(name).parts
            if name in seen or name.startswith("/") or ".." in parts or "\\" in name:
                result["errors"].append("Duplicate/unsafe ZIP entry: " + name)
                continue
            seen.add(name)
            if entry.is_dir():
                continue
            if (name.startswith("assets/") or "/assets/" in name) and entry.file_size <= 64 << 20:
                result["assets"].append({"path": name, "bytes": entry.file_size, "sha256": digest(archive.read(entry))})
            if not name.endswith(".so"):
                continue
            row = {"path": name, "bytes": entry.file_size, "compressed": entry.compress_type != zipfile.ZIP_STORED}
            result["native"].append(row)
            try:
                if len(parts) < 3 or parts[-3] != "lib" or parts[-2] not in MACHINES:
                    raise ValueError("Unexpected native library ABI path")
                abi = parts[-2]
                if entry.file_size > 512 << 20:
                    raise ValueError("Native library inspection size admission exceeded")
                data = archive.read(entry)
                row.update(inspect_elf(data, abi)); row["sha256"] = digest(data); row["abi"] = abi
                raw.seek(entry.header_offset)
                local = raw.read(30)
                if len(local) != 30 or local[:4] != b"PK\x03\x04":
                    raise ValueError("Invalid ZIP local header")
                name_bytes, extra_bytes = struct.unpack_from("<HH", local, 26)
                offset = entry.header_offset + 30 + name_bytes + extra_bytes
                row["zipDataOffset"] = offset
                row["apkZip16KiBAligned"] = not row["compressed"] and offset % PAGE == 0 if apk else None
                if apk and not row["apkZip16KiBAligned"]:
                    raise ValueError("APK native entry must be stored and 16 KiB ZIP aligned")
                abis.add(abi)
            except (ValueError, OSError, struct.error, zipfile.BadZipFile) as error:
                row["error"] = str(error); result["errors"].append(name + ": " + str(error))
    for abi in expected:
        if abi not in abis or not any(row.get("abi") == abi and row["path"].endswith("/libmonster_android.so") for row in result["native"]):
            result["errors"].append("Missing valid libmonster_android.so for " + abi)
    result["bundleGeneratedApkInspectionRequired"] = not apk
    result["passed"] = not result["errors"]
    return result


def file_hash(path):
    with path.open("rb") as stream:
        h = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
        return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--abis", nargs="+", choices=sorted(MACHINES), default=sorted(MACHINES))
    args = parser.parse_args()
    report = {"schemaVersion": 1, "pageBytes": PAGE, "packages": []}
    for path in args.packages:
        try:
            if path.suffix.lower() not in (".apk", ".aab"):
                raise ValueError("Expected APK or AAB")
            report["packages"].append(inspect_package(path, args.abis))
        except (ValueError, OSError, RuntimeError, zipfile.BadZipFile) as error:
            report["packages"].append({"artifact": path.name, "passed": False, "errors": [str(error)]})
    report["passed"] = all(item["passed"] for item in report["packages"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "packages": len(report["packages"])}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
