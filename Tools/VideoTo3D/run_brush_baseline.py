"""Run an immutable, unmodified Brush GPU baseline; preserve every attempt.

Acceptance is runtime reference evidence, never complete-restore acceptance.
Uses only the Python standard library and refuses existing evidence/output.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time


def sha256(path):
    """Hash a regular file incrementally."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    """Create and synchronize evidence without replacing prior evidence."""
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def inventory(root):
    """Inventory all files and reject links that could escape the input root."""
    result = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("symlink in inventory: " + str(path))
        if path.is_file():
            result.append({"path": path.relative_to(root).as_posix(),
                           "bytes": path.stat().st_size, "sha256": sha256(path)})
    return result


def validate_ply(path, max_splats):
    """Verify exported raw Gaussian properties, count, length and finite values."""
    with path.open("rb") as stream:
        header = []
        while True:
            line = stream.readline(4096)
            if not line or len(header) > 200:
                raise ValueError("invalid PLY header")
            header.append(line.decode("ascii").strip())
            if line.strip() == b"end_header":
                break
        count = int(next(v.split()[2] for v in header if v.startswith("element vertex ")))
        props = [v.split()[2] for v in header if v.startswith("property float ")]
        required = {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                    "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}
        if not required.issubset(props) or not 0 < count <= max_splats:
            raise ValueError("incomplete or out-of-budget Gaussian PLY")
        if "format binary_little_endian 1.0" not in header:
            raise ValueError("unsupported baseline PLY format")
        payload = stream.read()
        if len(payload) != count * len(props) * 4:
            raise ValueError("PLY payload length mismatch")
        if not all(math.isfinite(v[0]) for v in struct.iter_unpack("<f", payload)):
            raise ValueError("nonfinite exported Gaussian state")
    return {"gaussians": count, "floatProperties": props, "allFinite": True}


def run(args):
    """Validate identities, execute one uninterrupted run and gate its references."""
    policy_bytes = args.policy.read_bytes()
    policy = json.loads(policy_bytes)
    build = json.loads(args.build_identity.read_text(encoding="utf-8-sig"))
    scene = json.loads(args.scene_identity.read_text(encoding="utf-8-sig"))
    source = Path(build["source"])
    exe = Path(build["executable"]["path"])
    dataset = Path(scene["datasetPath"])
    git = build["git"]["path"]
    revision = subprocess.check_output([git, "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output([git, "-C", str(source), "status", "--porcelain"], text=True)
    if dirty or revision != policy["revision"] or sha256(exe) != policy["executableSha256"]:
        raise ValueError("unmodified source/binary binding failed")
    if not scene["finalAdmission"]["passed"] or inventory(dataset) != scene["datasetInventory"]:
        raise ValueError("prepared input changed or geometry admission failed")
    args.output.mkdir(parents=True, exist_ok=False)
    write_json(args.output / "policy-frozen.json", policy)
    output = args.output / "exports"
    command = [str(exe), str(dataset)] + policy["arguments"] + ["--export-path", str(output.resolve())]
    env = os.environ.copy()
    env.update(policy["environment"])
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    attempt = {"schemaVersion": 1, "status": "RUNNING", "startedAtUtc": started,
               "command": command, "cwd": str(source), "environment": policy["environment"],
               "policySha256": hashlib.sha256(policy_bytes).hexdigest(),
               "buildIdentitySha256": sha256(args.build_identity),
               "sceneIdentitySha256": sha256(args.scene_identity),
               "runnerSha256": sha256(Path(__file__)), "revision": revision,
               "executableSha256": sha256(exe), "uninterrupted": True}
    write_json(args.output / "invocation.json", attempt)
    clock = time.monotonic()
    try:
        with (args.output / "stdout.log").open("xb") as stdout, (args.output / "stderr.log").open("xb") as stderr:
            completed = subprocess.run(command, cwd=source, env=env, stdout=stdout, stderr=stderr,
                                       timeout=policy["wallClockTimeoutSeconds"], check=False)
        attempt["exitCode"] = completed.returncode
        if completed.returncode:
            raise ValueError("trainer returned nonzero; inspect preserved logs")
        logs = (args.output / "stdout.log").read_text(encoding="utf-8", errors="replace")
        logs += (args.output / "stderr.log").read_text(encoding="utf-8", errors="replace")
        if "Done training!" not in logs or "Using seed 42" not in logs:
            raise ValueError("missing real training completion/seed evidence")
        device_lines = [line for line in logs.splitlines() if "Using adapter AdapterInfo" in line]
        if len(device_lines) != 1 or any(v not in device_lines[0] for v in policy["requiredDeviceTokens"]):
            raise ValueError("actual training device/backend identity mismatch")
        if re.search(r"\b(?:WARN|ERROR)\b|panicked at|Failed evaluation|failed to export", logs):
            raise ValueError("runtime warning/error requires investigation before acceptance")
        evaluations = [{"iteration": int(i), "psnr": float(p), "ssim": float(s)} for i, p, s in
                       re.findall(r"Eval iter (\d+): PSNR ([\d.eE+\-]+), ssim ([\d.eE+\-]+)", logs)]
        expected = policy["expectedEvalIterations"]
        if [e["iteration"] for e in evaluations] != expected:
            raise ValueError("missing or duplicate evaluation boundaries")
        if not all(math.isfinite(e[k]) for e in evaluations for k in ("psnr", "ssim")):
            raise ValueError("nonfinite reference metrics")
        final = evaluations[-1]
        quality = policy["baselineQuality"]
        if final["psnr"] < quality["minimumFinalPsnr"] or final["ssim"] < quality["minimumFinalSsim"]:
            raise ValueError("reference quality below predeclared floor")
        if final["psnr"] - evaluations[0]["psnr"] < quality["minimumPsnrImprovement"]:
            raise ValueError("training did not meet predeclared improvement")
        refinements = [(int(i), int(n)) for i, n in re.findall(r"Refine iter (\d+), (\d+) splats", logs)]
        if [v[0] for v in refinements] != policy["expectedRefineCompletedIterations"]:
            raise ValueError("missing actual refine execution")
        plys = []
        for iteration in expected:
            path = output / ("export_" + str(iteration).zfill(3) + ".ply")
            plys.append({"iteration": iteration, "path": str(path),
                         **validate_ply(path, policy["maxSplats"])})
            renders = list((output / ("eval_" + str(iteration))).glob("*"))
            if len(renders) != policy["expectedEvalViews"] or any(v.stat().st_size < 1000 for v in renders):
                raise ValueError("missing actual heldout reference renders")
        if inventory(dataset) != scene["datasetInventory"] or args.policy.read_bytes() != policy_bytes:
            raise ValueError("input/policy changed during training")
        attempt.update(status="BASELINE_ACCEPTED", actualDeviceEvidence=device_lines,
                       evaluations=evaluations, refinements=refinements, plyValidation=plys,
                       outputInventory=inventory(output), productAdmission="BLOCKED",
                       completeRestoreAcceptance=False,
                       referencePurpose="Unmodified runtime/quality reference; matched patched builds required for restore equivalence")
        if args.accepted.exists():
            raise ValueError("refusing to overwrite baseline acceptance")
    except (OSError, ValueError, subprocess.SubprocessError, KeyError, StopIteration) as error:
        attempt.update(status="FAILED_PRESERVED", error=str(error))
        raise
    finally:
        attempt["elapsedSeconds"] = time.monotonic() - clock
        attempt["finishedAtUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_json(args.output / "result.json", attempt)
    write_json(args.accepted, attempt)
    print(json.dumps({"status": attempt["status"], "evaluations": evaluations,
                      "accepted": str(args.accepted)}))


def main():
    """Parse explicit evidence paths; never discover an arbitrary executable."""
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("policy", "build-identity", "scene-identity", "output", "accepted"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, subprocess.SubprocessError, KeyError, StopIteration) as error:
        print("Baseline failed; existing evidence preserved: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
