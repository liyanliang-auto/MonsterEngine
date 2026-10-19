"""Validate final SPIR-V and record portable shader identities and requirements."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reflect(path):
    data = path.read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise ValueError(f"Invalid SPIR-V size: {path.name}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError(f"Invalid SPIR-V magic: {path.name}")
    caps, groups, types, constants, shared = set(), [], {}, {}, []
    position = 5
    while position < len(words):
        size, opcode = words[position] >> 16, words[position] & 65535
        if not size or position + size > len(words):
            raise ValueError(f"Invalid instruction in {path.name}")
        args = words[position + 1:position + size]
        if opcode == 17:
            caps.add(args[0])
        elif opcode == 16 and args[1] == 17:
            groups.append(list(args[2:5]))
        elif opcode == 331 and args[1] == 38:
            raise ValueError("LocalSizeId requires specialization-aware reflection")
        elif 20 <= opcode <= 32:
            types[args[0]] = (opcode, args[1:])
        elif opcode == 43:
            constants[args[1]] = args[2]
        elif opcode == 59 and args[2] == 4:
            shared.append(args[0])
        position += size

    def align(value, alignment):
        return (value + alignment - 1) // alignment * alignment

    def layout(identifier):
        opcode, args = types[identifier]
        if opcode == 20:
            return 4, 4
        if opcode in (21, 22):
            return args[0] // 8, args[0] // 8
        if opcode == 23:
            size, alignment = layout(args[0])
            return size * args[1], alignment * (4 if args[1] == 3 else args[1])
        if opcode in (24, 28):
            size, alignment = layout(args[0])
            count = args[1] if opcode == 24 else constants[args[1]]
            return align(size, alignment) * count, alignment
        if opcode == 30:
            size, alignment = 0, 1
            for member in args:
                member_size, member_alignment = layout(member)
                size = align(size, member_alignment) + member_size
                alignment = max(alignment, member_alignment)
            return align(size, alignment), alignment
        if opcode == 32:
            return layout(args[1])
        raise ValueError(f"Unsupported workgroup type {opcode} in {path.name}")

    total = 0
    for identifier in shared:
        size, alignment = layout(identifier)
        total = align(total, alignment) + size
    return {"spirvVersion": words[1], "capabilities": sorted(caps), "shaderInt64": 11 in caps,
            "shaderFloat64": 10 in caps, "shaderFloat16": 9 in caps,
            "subgroupCapabilityIds": sorted(caps.intersection(range(61, 69))),
            "localSizes": groups, "sharedMemoryBytesUpperBound": total,
            "sharedMemoryMethod": "SPIR-V workgroup type layout with natural vector alignment"}


def tool_identity(executable):
    result = subprocess.run([str(executable), "--version"], capture_output=True, text=True, check=True)
    return {"sha256": sha256(Path(executable)), "version": result.stdout.strip()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--shader-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    args = parser.parse_args()
    inputs = json.loads(args.inputs.read_text(encoding="utf-8"))
    shaders = []
    for entry in inputs:
        path = args.shader_dir / entry["output"]
        subprocess.run([str(args.validator), "--target-env", "vulkan1.1", str(path)], check=True)
        shaders.append({"path": path.name, "sha256": sha256(path), "bytes": path.stat().st_size,
                        "requirements": reflect(path), "sources": [
                            {"path": source["logical"], "sha256": sha256(Path(source["file"]))}
                            for source in entry["sources"]]})
    document = {"schemaVersion": 1, "targetEnvironment": "vulkan1.1",
                "compiler": tool_identity(args.compiler), "validator": tool_identity(args.validator),
                "shaders": shaders}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Shader manifest failed: {error}", file=sys.stderr)
        sys.exit(1)
