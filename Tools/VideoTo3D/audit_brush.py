"""Reproduce the pinned Brush source assessment. Never grants runtime admission."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile


def git(source, *args):
    """Read Git facts without a shell, with a bounded execution deadline."""
    result = subprocess.run(
        ["git", "-C", str(source), *args], capture_output=True, timeout=30,
        check=False,
    )
    if result.returncode:
        raise ValueError("git command failed: " + result.stderr.decode("utf-8", "replace"))
    return result.stdout


def audit(source, descriptor):
    """Validate immutable objects and every cited source anchor; return source facts."""
    if descriptor.get("schemaVersion") != 1:
        raise ValueError("unsupported descriptor schema")
    revision = descriptor["revision"]
    if git(source, "rev-parse", "HEAD").decode().strip() != revision:
        raise ValueError("checkout HEAD differs from pinned Brush revision")
    origin = git(source, "remote", "get-url", "origin").decode().strip()
    if origin.removesuffix(".git").rstrip("/") != descriptor["upstreamUrl"].rstrip("/"):
        raise ValueError("checkout origin differs from pinned official upstream")
    evidence = []
    for capability in descriptor["capabilities"].values():
        evidence.extend(capability["evidence"])
    for field in descriptor["requiredCheckpointState"]:
        evidence.extend(field["evidence"])
    evidence.extend(descriptor["cliProbe"]["evidence"])
    objects = {}
    for item in evidence:
        path = item["file"]
        relative = PurePosixPath(path)
        if relative.is_absolute() or ".." in relative.parts or "\\" in path or ":" in path:
            raise ValueError("unsafe evidence source path")
        expected_url = f'{descriptor["upstreamUrl"]}/blob/{revision}/{path}#L{item["line"]}'
        if item["url"] != expected_url:
            raise ValueError("evidence URL does not bind pinned file and line")
        if path not in objects:
            blob = git(source, "show", f"{revision}:{path}")
            working = (source / path).read_bytes()
            if working.replace(b"\r\n", b"\n") != blob.replace(b"\r\n", b"\n"):
                raise ValueError("audited working source differs from pinned Git object: " + path)
            objects[path] = blob
        lines = objects[path].decode("utf-8").splitlines()
        line = item["line"]
        if not isinstance(line, int) or line < 1 or line > len(lines):
            raise ValueError("invalid evidence line: " + path)
        if item["anchor"] not in lines[line - 1]:
            raise ValueError("source anchor mismatch: " + path)
    return {
        "revision": revision, "origin": origin,
        "workingTreeDirty": bool(git(source, "status", "--porcelain")),
        "files": [{"file": path, "gitBlobContentSha256": hashlib.sha256(blob).hexdigest()}
                  for path, blob in sorted(objects.items())],
        "anchorsChecked": len(evidence), "status": "SUPPORTED_BY_SOURCE",
    }


def publish(output, report):
    """Atomically replace the report after serialization; preserve old report on error."""
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                         prefix=output.name + ".", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(report, stream, indent=2, ensure_ascii=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main():
    """Audit an explicit existing checkout; report missing runtime independently."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--descriptor", type=Path,
                        default=Path(__file__).with_name("brush-admission.json"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        descriptor_bytes = args.descriptor.read_bytes()
        descriptor = json.loads(descriptor_bytes.decode("utf-8"))
        source = audit(args.source.resolve(strict=True), descriptor)
        discovery = {name: shutil.which(name) for name in ("brush", "brush-app", "cargo", "rustc")}
        runtime = {
            "status": "NOT_VERIFIED", "evidence": [], "toolDiscovery": discovery,
            "reason": ("No Brush executable found on PATH; no real runtime probe can run."
                       if not any(discovery[name] for name in ("brush", "brush-app")) else
                       "Discovered executable has no admitted hash/build binding; use Windows admission CLI with explicit path/hash."),
            "probeScope": "AVAILABILITY_ONLY", "trainingAttempted": False,
            "checkpointRestoreAttempted": False,
        }
        report = {
            "schemaVersion": 1,
            "auditedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "descriptorSha256": hashlib.sha256(descriptor_bytes).hexdigest(),
            "sourceAudit": source, "upstreamUrl": descriptor["upstreamUrl"],
            "revision": descriptor["revision"], "auditStatus": descriptor["auditStatus"],
            "capabilities": descriptor["capabilities"],
            "requiredCheckpointState": descriptor["requiredCheckpointState"],
            "runtimeVerification": runtime, "productAdmission": "BLOCKED",
            "nextActions": descriptor["nextActions"],
            "limitations": ["Source anchors and Git identity cannot prove runtime correctness.",
                            "Availability discovery never executes unpinned tools.",
                            "Safe pause and complete checkpoint restore remain ID16 hard gates."],
        }
        publish(args.output, report)
        print("Source audit recorded; product admission BLOCKED; runtime NOT_VERIFIED.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("Brush source audit failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
