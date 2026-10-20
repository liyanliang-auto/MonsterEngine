"""Actual Brush subprocess recovery driver. Inputs and failed runs are retained.

Commands use a dedicated JSON channel. No log parsing can authorize SafeExit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import struct
import time
import uuid

ACTIVE: list[subprocess.Popen] = []


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, indent=2, allow_nan=False)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def events(control: Path) -> list[dict]:
    path = control / "events.jsonl"
    if not path.exists():
        return []
    # Only a completed newline is an event. Producer flush precedes durable ACK.
    raw = path.read_bytes()
    return [json.loads(line) for line in raw.split(b"\n")[:-1] if line]


def command(control: Path, generation: str, request: str, operation: str, **fields) -> None:
    write_json(control / "commands" / (request + ".json"),
               {"command": operation, "requestId": request, "generation": generation, **fields})


def wait_event(process: subprocess.Popen, control: Path, name: str, timeout: float,
               request: str | None = None) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for event in events(control):
            if event.get("event") == name and (request is None or event.get("requestId") == request):
                return event
            if event.get("event") == "SaveFailed" and event.get("requestId") == request:
                raise RuntimeError(f"Checkpoint save failed: {event}")
        if process.poll() is not None:
            raise RuntimeError(f"Brush exited {process.returncode} before {name}; evidence in {control.parent}")
        time.sleep(0.05)
    raise TimeoutError(f"Timed out waiting for {name}; process {process.pid}")


def spawn(executable: Path, dataset: Path, run: Path, store: Path, arguments: list[str],
          restore: str | None = None, hold: str | None = None):
    run.mkdir(parents=True, exist_ok=False)
    control = run / "control"
    generation = uuid.uuid4().hex
    invocation = [str(executable), str(dataset), *arguments, "--checkpoint-root", str(store),
                  "--run-generation", generation, "--control-directory", str(control),
                  "--export-path", str(run / "output")]
    if restore is not None:
        invocation += ["--restore-checkpoint", restore]
    if hold is not None:
        invocation += ["--checkpoint-hold-point", hold]
    environment = dict(os.environ, WGPU_BACKEND="vulkan", RUST_LOG="info", RUST_BACKTRACE="1")
    write_json(run / "invocation.json", {"command": invocation, "executableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                                         "environment": {k: environment[k] for k in ("WGPU_BACKEND", "RUST_LOG", "RUST_BACKTRACE")}})
    stdout = (run / "stdout.log").open("wb")
    stderr = (run / "stderr.log").open("wb")
    try:
        process = subprocess.Popen(invocation, stdout=stdout, stderr=stderr, env=environment)
        process.brush_run_directory = run
        ACTIVE.append(process)
        write_json(run / "process.json", {"pid": process.pid})
    finally:
        stdout.close()
        stderr.close()
    return process, control, generation


def finish(process: subprocess.Popen, run: Path, timeout: float) -> int:
    try:
        result = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        result = process.wait()
        write_json(run / "exit.json", {"exitCode": result, "timedOut": True})
        raise
    write_json(run / "exit.json", {"exitCode": result, "timedOut": False})
    return result


def _unique_json_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise AssertionError(f"Duplicate checkpoint JSON field: {key}")
        result[key] = value
    return result


def _checkpoint_component(value: object) -> bool:
    reserved = {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(1, 10)],
                *[f"LPT{i}" for i in range(1, 10)]}
    return (isinstance(value, str) and 1 <= len(value) <= 180
            and re.fullmatch(r"[A-Za-z0-9_-]+", value) is not None
            and value.upper() not in reserved)


def load_checkpoint(store: Path, checkpoint: str) -> tuple[dict, dict[str, bytes]]:
    """Load validated finite f32le operands; this is not a runtime restore validator.

    Match the store's scalar/zero-extent shapes and checked u64 byte products.
    Validate the complete inventory before names can be collapsed into a mapping.
    """
    root = store / checkpoint
    try:
        manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"),
                              object_pairs_hook=_unique_json_object)
    except (OSError, ValueError) as error:
        raise AssertionError(f"Cannot read checkpoint manifest: {root}") from error
    inventory = manifest.get("tensors") if isinstance(manifest, dict) else None
    if not isinstance(inventory, list) or not 1 <= len(inventory) <= 65536:
        raise AssertionError("Missing or invalid tensor inventory")
    tensors = {}
    payloads = set()
    required = {"name", "payload", "shape", "dtype", "bytes", "sha256"}
    for item in inventory:
        if (not isinstance(item, dict) or not required <= item.keys()
                or item.keys() - (required | {"param_id"})):
            raise AssertionError("Invalid tensor descriptor fields")
        name, payload = item["name"], item["payload"]
        if (not _checkpoint_component(name) or not _checkpoint_component(payload)
                or name in tensors or payload in payloads
                or (item.get("param_id") is not None
                    and not _checkpoint_component(item["param_id"]))):
            raise AssertionError("Invalid or duplicate tensor name/payload/parameter ID")
        shape = item["shape"]
        if item["dtype"] != "f32le" or not isinstance(shape, list) or len(shape) > 8:
            raise AssertionError(f"Unsupported tensor dtype or shape: {name}")
        expected_bytes = 4  # An empty shape is one scalar, not an empty tensor.
        for dimension in shape:
            if type(dimension) is not int or not 0 <= dimension <= (1 << 64) - 1:
                raise AssertionError(f"Invalid tensor dimension: {name}")
            expected_bytes *= dimension
            if expected_bytes > (1 << 64) - 1:
                raise AssertionError(f"Tensor byte count overflow: {name}")
        if type(item["bytes"]) is not int or item["bytes"] != expected_bytes:
            raise AssertionError(f"Tensor shape/byte count mismatch: {name}")
        digest = item["sha256"]
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise AssertionError(f"Invalid tensor payload hash: {name}")
        try:
            data = (root / "payloads" / payload).read_bytes()
        except OSError as error:
            raise AssertionError(f"Cannot read tensor payload: {name}") from error
        if len(data) != expected_bytes or hashlib.sha256(data).hexdigest() != digest:
            raise AssertionError(f"Tensor payload length/hash mismatch: {name}")
        if any(not math.isfinite(value) for (value,) in struct.iter_unpack("<f", data)):
            raise AssertionError(f"Nonfinite tensor value: {name}")
        tensors[name] = data
        payloads.add(payload)
    return manifest, tensors


def compare_checkpoints(first_store: Path, first: str, second_store: Path, second: str,
                        absolute: float, relative: float, exact: bool = False) -> dict:
    for label, value in (("absolute", absolute), ("relative", relative)):
        try:
            valid = type(value) in (int, float) and math.isfinite(value) and value >= 0
        except (OverflowError, TypeError):
            valid = False
        if not valid:
            raise AssertionError(f"Invalid {label} tensor tolerance")
    left, left_tensors = load_checkpoint(first_store, first)
    right, right_tensors = load_checkpoint(second_store, second)
    for key in ("identity", "next_iteration"):
        if left[key] != right[key]:
            raise AssertionError(f"Checkpoint {key} mismatch")
    # Elapsed wall time and process request identity have no effect on training.
    a, b = left["metadata"], right["metadata"]
    for key in ("trainer", "sampler", "schema_version"):
        if a[key] != b[key]:
            raise AssertionError(f"Checkpoint metadata {key} mismatch")
    if left_tensors.keys() != right_tensors.keys():
        raise AssertionError("Tensor inventory mismatch")
    descriptors_a = {t["name"]: (t["shape"], t["dtype"], t.get("param_id")) for t in left["tensors"]}
    descriptors_b = {t["name"]: (t["shape"], t["dtype"], t.get("param_id")) for t in right["tensors"]}
    if descriptors_a != descriptors_b:
        raise AssertionError("Tensor shapes/parameter binding mismatch")
    maximum = 0.0
    for name, data in left_tensors.items():
        other = right_tensors[name]
        if len(data) != len(other) or (exact and data != other):
            raise AssertionError(f"Exact boundary tensor mismatch: {name}")
        for (x,), (y,) in zip(struct.iter_unpack("<f", data), struct.iter_unpack("<f", other)):
            error = abs(x - y)
            try:
                limit = absolute + relative * abs(x)
            except OverflowError as arithmetic_error:
                raise AssertionError(f"Tensor comparison arithmetic overflow: {name}") from arithmetic_error
            if not math.isfinite(error) or not math.isfinite(limit):
                raise AssertionError(f"Nonfinite tensor comparison arithmetic: {name}")
            maximum = max(maximum, error)
            if error > limit:
                raise AssertionError(f"Tensor tolerance exceeded {name}: {x} vs {y}")
    return {"passed": True, "exact": exact, "maximumAbsoluteTensorError": maximum}


def compare_outputs(reference: Path, resumed: Path, final: int, policy: dict) -> dict:
    from PIL import Image
    import numpy as np
    compare = policy["restoreComparison"]
    ref_images = sorted((reference / "output" / f"eval_{final}").glob("*.png"))
    actual_images = sorted((resumed / "output" / f"eval_{final}").glob("*.png"))
    if not ref_images or [p.name for p in ref_images] != [p.name for p in actual_images]:
        raise AssertionError("Missing or mismatched final reference render inventory")
    maximum = 0
    total = count = 0
    for left, right in zip(ref_images, actual_images):
        a = np.asarray(Image.open(left)).astype(np.int16)
        b = np.asarray(Image.open(right)).astype(np.int16)
        if a.shape != b.shape:
            raise AssertionError("Final render dimensions differ")
        difference = np.abs(a - b)
        maximum = max(maximum, int(difference.max()))
        total += int(difference.sum())
        count += difference.size
    mean = total / count
    limits = compare["finalRenderTolerance"]
    if maximum > limits["maximumPerChannelAbsolute8bit"] or mean > limits["meanAbsolute8bit"]:
        raise AssertionError(f"Final render tolerance exceeded: maximum={maximum}, mean={mean}")
    pattern = re.compile(r"Eval iter (\d+): PSNR ([0-9.eE+-]+), ssim ([0-9.eE+-]+)")
    def metric(run: Path):
        logs = "\n".join((run / name).read_text(encoding="utf-8", errors="replace")
                         for name in ("stdout.log", "stderr.log"))
        matches = [m for m in pattern.findall(logs) if int(m[0]) == final]
        if len(matches) != 1:
            raise AssertionError("Expected exactly one final metric observation")
        return float(matches[0][1]), float(matches[0][2])
    a, b = metric(reference), metric(resumed)
    psnr, ssim = abs(a[0] - b[0]), abs(a[1] - b[1])
    limits = compare["finalMetricTolerance"]
    if psnr > limits["psnrAbsoluteDb"] or ssim > limits["ssimAbsolute"]:
        raise AssertionError("Final metric tolerance exceeded")
    return {"passed": True, "maximum8bitError": maximum, "mean8bitError": mean, "psnrDifferenceDb": psnr, "ssimDifference": ssim}


def run_comparison(options, policy: dict) -> dict:
    root = options.output
    arguments = policy["arguments"]
    comparison = policy["restoreComparison"]
    final = comparison["finalCompletedIteration"]
    boundaries = [comparison["ordinaryCompletedIteration"], comparison["refinementCompletedIteration"]]
    reference_store = root / "reference-store"
    ref_run = root / "reference"
    process, control, generation = spawn(options.executable, options.dataset, ref_run, reference_store, arguments)
    for step in [*boundaries, final]:
        command(control, generation, f"save-{step:08d}", "Save", atNextIteration=step)
    saved = {step: wait_event(process, control, "Saved", options.timeout, f"save-{step:08d}") for step in [*boundaries, final]}
    if any(saved[step]["nextIteration"] != step for step in saved):
        raise AssertionError("Reference missed a predeclared checkpoint boundary")
    if finish(process, ref_run, options.timeout) != 0:
        raise RuntimeError("Reference training failed")
    results = []
    tolerance = comparison["subsequentTensorTolerance"]
    for step in boundaries:
        store = root / f"recovery-store-{step}"
        run = root / f"pause-{step}"
        process, control, generation = spawn(options.executable, options.dataset, run, store, arguments)
        command(control, generation, "pause", "PauseAndSave", atNextIteration=step)
        ack = wait_event(process, control, "Paused", options.timeout, "pause")
        if ack["nextIteration"] != step:
            raise AssertionError("Pause missed a predeclared checkpoint boundary")
        command(control, generation, "exit", "SafeExit", pauseRequestId="pause", checkpointId=ack["checkpointId"])
        wait_event(process, control, "SafeExit", options.timeout, "exit")
        if finish(process, run, options.timeout) != 0:
            raise RuntimeError("Safe exit process failed")
        boundary_result = compare_checkpoints(reference_store, saved[step]["checkpointId"], store, ack["checkpointId"], 0.0, 0.0, exact=True)
        resumed_run = root / f"resume-{step}"
        process, control, generation = spawn(options.executable, options.dataset, resumed_run, store, arguments, ack["checkpointId"])
        command(control, generation, "restored", "Save", atNextIteration=step)
        command(control, generation, "final", "Save", atNextIteration=final)
        restored = wait_event(process, control, "Saved", options.timeout, "restored")
        immediate = compare_checkpoints(store, ack["checkpointId"], store, restored["checkpointId"], 0.0, 0.0, exact=True)
        final_ack = wait_event(process, control, "Saved", options.timeout, "final")
        if finish(process, resumed_run, options.timeout) != 0:
            raise RuntimeError("Restored process failed")
        evolved = compare_checkpoints(reference_store, saved[final]["checkpointId"], store, final_ack["checkpointId"], tolerance["absolute"], tolerance["relative"])
        ref_samples = [e for e in events(root / "reference" / "control") if e.get("event") == "Sample" and e["iteration"] >= step]
        restored_samples = [e for e in events(control) if e.get("event") == "Sample"]
        for sample in ref_samples + restored_samples:
            sample.pop("generation", None)
        if ref_samples != restored_samples:
            raise AssertionError("Fresh-process sample/RNG evolution mismatch")
        outputs = compare_outputs(ref_run, resumed_run, final, policy)
        results.append({"boundary": step, "boundarySnapshot": boundary_result, "immediateRestore": immediate, "evolution": evolved, "rendersAndMetrics": outputs})
    return {"status": "STATE_COMPARISONS_PASSED", "comparisons": results,
            "remaining": ["LPIPS-enabled run", "real kill/corrupt/write/flush/publication fault matrix"]}


def run_smoke(options, policy: dict) -> dict:
    """Exercise real GPU pause, ACK, exit and immediate fresh-process restore."""
    store = options.output / "store"
    initial_run = options.output / "pause"
    process, control, generation = spawn(options.executable, options.dataset, initial_run, store, policy["arguments"])
    command(control, generation, "pause", "PauseAndSave", atNextIteration=options.boundary)
    saved = wait_event(process, control, "Paused", options.timeout, "pause")
    if saved["nextIteration"] != options.boundary:
        raise AssertionError("Requested smoke boundary was missed")
    command(control, generation, "exit", "SafeExit", pauseRequestId="pause", checkpointId=saved["checkpointId"])
    wait_event(process, control, "SafeExit", options.timeout, "exit")
    if finish(process, initial_run, options.timeout) != 0:
        raise RuntimeError("Initial safe exit failed")
    restored_run = options.output / "restored"
    process, control, generation = spawn(options.executable, options.dataset, restored_run, store, policy["arguments"], saved["checkpointId"])
    command(control, generation, "restored", "PauseAndSave", atNextIteration=saved["nextIteration"])
    restored = wait_event(process, control, "Paused", options.timeout, "restored")
    exact = compare_checkpoints(store, saved["checkpointId"], store, restored["checkpointId"], 0.0, 0.0, exact=True)
    command(control, generation, "resume", "Resume", pauseRequestId="restored", checkpointId=restored["checkpointId"])
    command(control, generation, "step", "PauseAndSave", atNextIteration=restored["nextIteration"] + 1)
    wait_event(process, control, "Resumed", options.timeout, "resume")
    stepped = wait_event(process, control, "Paused", options.timeout, "step")
    if stepped["nextIteration"] != saved["nextIteration"] + 1:
        raise AssertionError("Restored smoke did not pause after exactly one step")
    command(control, generation, "exit", "SafeExit", pauseRequestId="step", checkpointId=stepped["checkpointId"])
    wait_event(process, control, "SafeExit", options.timeout, "exit")
    if finish(process, restored_run, options.timeout) != 0:
        raise RuntimeError("Restored safe exit failed")
    return {"status": "REAL_GPU_RECOVERY_SMOKE_PASSED", "boundary": saved["nextIteration"],
            "immediateRestore": exact, "continuedTo": stepped["nextIteration"],
            "remaining": ["Full ordinary/refine-to-600 comparison", "real fault matrix", "LPIPS-enabled comparison"]}


def run_crash(options, policy: dict) -> dict:
    if not options.hold:
        raise ValueError("crash mode requires --hold")
    run = options.output / "crash"
    store = options.output / "store"
    process, control, generation = spawn(options.executable, options.dataset, run, store, policy["arguments"], hold=options.hold)
    command(control, generation, "crash-save", "PauseAndSave", atNextIteration=options.boundary)
    deadline = time.monotonic() + options.timeout
    while not (control / "hook-reached.json").exists():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError("Brush did not reach requested real store boundary")
        time.sleep(0.05)
    process.kill()
    code = finish(process, run, options.timeout)
    return {"status": "PROCESS_KILLED_AT_REAL_PUBLICATION_BOUNDARY", "boundary": options.hold,
            "exitCode": code, "store": str(store), "events": events(control),
            "remaining": "Explicit restore/recoverable scan and prior valid checkpoint preservation must be verified"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("compare", "crash", "smoke"), default="compare")
    parser.add_argument("--hold", choices=("BeforeWrite", "BeforePayloadFlush", "BeforePublish", "AfterPublish", "BeforeCurrentPublish", "AfterCurrentPublish"))
    parser.add_argument("--boundary", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=3600)
    options = parser.parse_args()
    for key in ("executable", "dataset", "policy", "output"):
        setattr(options, key, getattr(options, key).resolve())
    options.output.mkdir(parents=True, exist_ok=False)
    policy_bytes = options.policy.read_bytes()
    write_json(options.output / "policy-identity.json", {"sha256": hashlib.sha256(policy_bytes).hexdigest(), "path": str(options.policy)})
    try:
        policy = json.loads(policy_bytes)
        result = {"compare": run_comparison, "crash": run_crash, "smoke": run_smoke}[options.mode](options, policy)
        write_json(options.output / "result.json", result)
        return 0
    except Exception as error:
        for process in ACTIVE:
            killed = process.poll() is None
            if process.poll() is None:
                process.kill()
                process.wait(timeout=30)
            run = process.brush_run_directory
            if not (run / "exit.json").exists():
                write_json(run / "exit.json", {"exitCode": process.returncode,
                                                "killedAfterRunnerFailure": killed})
        write_json(options.output / "result.json", {"status": "FAILED", "reason": str(error)})
        raise


if __name__ == "__main__":
    raise SystemExit(main())
