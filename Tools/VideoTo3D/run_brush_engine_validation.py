"""Run preregistered Windows production Vulkan captures, never retrain Brush.

CLI: --config LOCAL.json. Paths in the local config must be absolute. Required
fields: schemaVersion=1, projectDirectory, cameraContract, protectedInputs, audit,
policyPath, policySHA256, sourcePLY, shaderDirectory, outputDirectory, tools,
buildEvidence. tools maps cmake/msbuild/glslc to explicit paths (or command names
resolved with shutil.which). The policy must already be durably registered using
brush_render_comparison.register_policy after the current build and before scores.

buildEvidence identifies a JSON file with schemaVersion=1, buildIdentity and
sourceIdentities exactly as registered, and commands [{argv:[...], exitCode:0,
log:{path,bytes,sha256}}]. At least one successful CMake --build or MSBuild
command is required. Logs and every registered artifact are rehashed. This tool
does not substitute its own syntax probe for a build, or certify visual review,
independent CPU/mutation tests, Android or product acceptance.
"""

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

import numpy as np

from brush_camera_contract import validate_camera_contract, verify_protected_inputs
from brush_render_comparison import compare_registered, load_policy
from brush_render_fixtures import evaluate_analytic_fixture


CAMERA_FIELDS = ("width", "height", "fx", "fy", "cx", "cy", "nearPlane",
                 "farPlane", "viewMatrix", "shDegree")
TIMEOUT_SECONDS = 120


def _read(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key: " + key)
            result[key] = value
        return result
    result = json.loads(Path(path).read_text(encoding="utf-8-sig"),
                        object_pairs_hook=pairs,
                        parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON: " + value)))
    _require(isinstance(result, dict), "JSON document must be an object: " + str(path))
    return result


def _write(path, value):
    data = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    with Path(path).open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _identity(path):
    path = Path(path).resolve(strict=True)
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("file changed during hashing: " + str(path))
    return {"path": str(path), "bytes": after.st_size, "sha256": digest.hexdigest()}


def _check_identity(entry):
    actual = _identity(entry["path"])
    if any(actual[key] != entry.get(key) for key in ("bytes", "sha256")):
        raise ValueError("identity changed: " + entry["path"])
    return actual


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _near(actual, expected, label, round_float=False):
    """Match the source's single float conversion, not a relaxed quality gate."""
    if isinstance(expected, list):
        _require(isinstance(actual, list) and len(actual) == len(expected), label + " shape mismatch")
        for index, value in enumerate(expected):
            _near(actual[index], value, label + "[%d]" % index, round_float)
        return
    _require(type(actual) in (int, float) and math.isfinite(actual), label + " nonfinite/missing")
    target = _f32(expected) if round_float else expected
    # Native JSON uses precision 17; an emitted binary32 value must equal its
    # promoted value exactly. Intrinsics retained as double must also match.
    _require(actual == target, label + " differs from source-representable request")


def _resolve_tools(config):
    _require(set(config) == {"cmake", "msbuild", "glslc"}, "tools require cmake/msbuild/glslc")
    result = {}
    for name, value in config.items():
        _require(isinstance(value, str) and bool(value), "invalid tool path")
        path = Path(value) if Path(value).is_absolute() else Path(shutil.which(value) or "")
        _require(path.is_absolute() and path.is_file(), "cannot resolve real tool: " + name)
        result[name] = _identity(path)
    result["python"] = _identity(sys.executable)
    return result


def _build_bindings(config, policy, tools):
    evidence = _read(config["buildEvidence"])
    _require(evidence.get("schemaVersion") == 1, "unsupported build evidence")
    for key in ("buildIdentity", "sourceIdentities"):
        _require(evidence.get(key) == policy[key], "build evidence policy mismatch: " + key)
    commands = evidence.get("commands")
    _require(isinstance(commands, list) and bool(commands), "actual build command evidence required")
    built = False
    for command in commands:
        argv = command.get("argv")
        _require(isinstance(argv, list) and argv and all(isinstance(v, str) for v in argv), "invalid build argv")
        _require(type(command.get("exitCode")) is int and command["exitCode"] == 0, "build command failed")
        _check_identity(command["log"])
        invoked = Path(argv[0]).resolve()
        built |= ((invoked == Path(tools["cmake"]["path"]) and "--build" in argv)
                  or invoked == Path(tools["msbuild"]["path"]))
    _require(built, "no successful resolved CMake/MSBuild build command")
    shader_root = Path(config["shaderDirectory"]).resolve(strict=True)
    registered = {Path(item["path"]).resolve() for item in policy["buildIdentity"]["shaders"]}
    actual = set(shader_root.rglob("*.spv"))
    _require(actual == registered and bool(actual), "shader directory differs from registered complete SPIR-V set")
    for item in policy["buildIdentity"]["shaders"]:
        _check_identity(item)
    _check_identity(policy["buildIdentity"]["executable"])
    for item in policy["sourceIdentities"].values():
        _check_identity(item)
    return {"identity": _identity(config["buildEvidence"]), "commands": commands, "tools": tools}


def _execute(argv, cwd, env, directory):
    """Keep both byte streams and retire the Windows process tree on timeout."""
    start = time.monotonic()
    record = {"argv": argv, "cwd": str(cwd), "timeoutSeconds": TIMEOUT_SECONDS,
              "exitCode": None, "timedOut": False}
    process = None
    try:
        with (directory / "stdout.log").open("xb") as stdout, (directory / "stderr.log").open("xb") as stderr:
            process = subprocess.Popen(argv, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0)
            try:
                record["exitCode"] = process.wait(timeout=TIMEOUT_SECONDS)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                record["timedOut"] = time.monotonic() - start >= TIMEOUT_SECONDS
                if os.name == "nt":
                    taskkill = Path(os.environ["SystemRoot"]) / "System32" / "taskkill.exe"
                    killed = subprocess.run([str(taskkill), "/PID", str(process.pid), "/T", "/F"],
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
                    record["treeRetirementExitCode"] = killed.returncode
                    (directory / "taskkill.stdout.log").write_bytes(killed.stdout)
                    (directory / "taskkill.stderr.log").write_bytes(killed.stderr)
                if process.poll() is None:
                    process.kill()
                record["exitCode"] = process.wait(timeout=10)
                raise RuntimeError("capture timed out or interrupted; process retired")
        _require(record["exitCode"] == 0, "capture process exited " + str(record["exitCode"]))
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        record["elapsedSeconds"] = time.monotonic() - start
        _write(directory / "process.json", record)
    return record


def _validate_native(meta, summary, camera, policy):
    _require(summary.get("status") == "RECORDED" and summary.get("captures") == 1,
             "native final recorder is incomplete")
    fixed = {"schemaVersion": 1, "completionStatus": "complete", "format": "RGBA8_UNORM",
             "rowOrigin": "bottom_left", "encoding": policy["commonEncoding"],
             "displayTransfer": "identity", "mode": "A", "originalResidentData": True,
             "projectionColorCompression": False, "projectedFusion": False,
             "fullLeaf": False, "shStorageMode": 0, "shStorageDegree": -1,
             "cameraId": camera["id"], "policySHA256": policy["_registration"]["sha256"],
             "auditSHA256": policy["auditSHA256"], "protectedInputsSHA256": policy["protectedInputsSHA256"]}
    for key, expected in fixed.items():
        _require(meta.get(key) == expected, "native capture mismatch: " + key)
    _require(type(meta.get("submissionToken")) is int and meta["submissionToken"] > 0,
             "no completed real submission token")
    for key in ("width", "height", "shDegree"):
        _require(type(meta.get(key)) is int and meta[key] == camera[key], "native camera mismatch: " + key)
    _require(meta.get("sourceSHDegree") == str(camera["shDegree"]), "incomplete source SH")
    _near(meta.get("view"), camera["viewMatrix"], "view", True)
    _near(meta.get("requestedFocal"), [camera["fx"], camera["fy"]], "requestedFocal")
    _near(meta.get("focal"), [camera["fx"], camera["fy"]], "focal", True)
    _near(meta.get("principalPoint"), [camera["cx"], camera["cy"]], "principalPoint")
    for key in ("nearPlane", "farPlane"):
        _near(meta.get(key), camera[key], key, True)
    width, height = camera["width"], camera["height"]
    near, far = _f32(camera["nearPlane"]), _f32(camera["farPlane"])
    projection = [0.0] * 16
    projection[0], projection[5] = 2*camera["fx"]/width, 2*camera["fy"]/height
    projection[8], projection[9] = 1-(2*camera["cx"]+1)/width, (2*camera["cy"]+1)/height-1
    projection[10], projection[11], projection[14] = far/(near-far), -1, -(far*near)/(far-near)
    _near(meta.get("projection"), projection, "projection", True)
    view = [_f32(value) for value in camera["viewMatrix"]]
    eye = []
    for axis in range(3):
        value = 0.0
        for row in range(3):
            value -= view[axis*4+row]*view[12+row]
        eye.append(value)
    _near(meta.get("cameraPosition"), eye, "cameraPosition", True)
    device = policy["deviceIdentity"]
    _require(meta.get("device") == device["gpu"] and meta.get("driverVersion") == device["driver"],
             "actual GPU/driver differs from preregistration")
    optional = {"vendorID": "vendorID", "deviceID": "deviceID", "vulkanVersion": "vulkanVersion"}
    for registered, native in optional.items():
        if registered in device:
            _require(meta.get(native) == device[registered], "actual device identity mismatch: " + native)


def _capture(camera, ply, config, policy, directory):
    ident = camera["id"]
    _require(isinstance(ident, str) and ident not in ("", ".", "..")
             and not any(value in ident for value in ("/", "\\", ":")), "unsafe camera id")
    directory.mkdir(exist_ok=False)
    output = directory / "native"
    request = {key: copy.deepcopy(camera[key]) for key in CAMERA_FIELDS}
    request.update({"schemaVersion": 1, "cameraId": camera["id"], "sourcePLY": str(Path(ply).resolve()),
                    "outputDirectory": str(output), "policySHA256": policy["_registration"]["sha256"],
                    "auditSHA256": policy["auditSHA256"], "protectedInputsSHA256": policy["protectedInputsSHA256"]})
    _write(directory / "request.json", request)
    env = dict(os.environ, MONSTER_SPLAT_SHADER_DIR=str(Path(config["shaderDirectory"]).resolve()))
    executable = policy["buildIdentity"]["executable"]["path"]
    process = _execute([executable, "--splat-calibrated-capture", str(directory / "request.json")],
                       Path(config["projectDirectory"]), env, directory)
    meta = _read(output / "capture_000.json")
    summary = _read(output / "metadata.json")
    _validate_native(meta, summary, camera, policy)
    rgba = _identity(output / "capture_000.rgba")
    _require(rgba["bytes"] == camera["width"]*camera["height"]*4, "native readback length mismatch")
    pixels = np.fromfile(rgba["path"], dtype=np.uint8).reshape(camera["height"], camera["width"], 4)
    # Native RGB is already accumulated over black (premultiplied C). PPM is
    # emitted directly from the same readback; do not composite C by alpha twice.
    ppm = _identity(output / "capture_000.ppm")
    expected_ppm = ("P6\n%d %d\n255\n" % (camera["width"], camera["height"])).encode("ascii") + pixels[:, :, :3].tobytes()
    _require(Path(ppm["path"]).read_bytes() == expected_ppm, "PPM is not exact native readback RGB")
    _require(bool(np.any(pixels[:, :, :3])), "empty/all-black native capture")
    record = {**ppm, **{key: copy.deepcopy(camera[key]) for key in CAMERA_FIELDS}, "id": camera["id"],
              "rowOrigin": "bottom_left", "encoding": meta["encoding"], "completionStatus": "complete",
              "nativeMetadata": _identity(output / "capture_000.json"), "nativeSummary": _identity(output / "metadata.json"),
              "nativeRGBA": rgba, "sourcePLY": _identity(ply), "process": process}
    _write(directory / "validated-capture.json", record)
    return record, pixels[::-1, :, :3].astype(np.float64)/255


def _synthetic(fixture, record, pixels, policy):
    analytic = evaluate_analytic_fixture(fixture)
    saved = fixture["analytic"]
    gates = policy["gates"]
    cpu_equal = True
    for key in ("center", "scales", "opacity", "worldCovariance", "screenCovariance", "shBasis", "colorRGB"):
        a, b = np.asarray(analytic[key]), np.asarray(saved[key])
        cpu_equal &= bool(np.isfinite(a).all() and np.isfinite(b).all()
                          and np.all(np.abs(a-b) <= gates["cpuAbsTolerance"] + gates["cpuRelTolerance"]*np.abs(b)))
    meta = _read(record["nativeMetadata"]["path"])
    view = np.array(meta["view"]).reshape(4, 4, order="F")
    proj = np.array(meta["projection"]).reshape(4, 4, order="F")
    clip = proj @ view @ np.append(fixture["position"], 1)
    ndc = clip[:2]/clip[3]
    camera = fixture["camera"]
    center = [(ndc[0]+1)*camera["width"]/2-0.5,
              camera["height"]-1-((ndc[1]+1)*camera["height"]/2-0.5)]
    center_error = float(np.max(np.abs(np.array(center)-analytic["center"])))
    errors = []
    for sample in analytic["samples"]:
        observed = pixels[sample["y"], sample["x"]]
        expected = np.asarray(sample["rgb"])
        errors.append({"x": sample["x"], "y": sample["y"], "region": sample["region"],
                       "observedRGB": observed.tolist(), "referenceRGB": expected.tolist(),
                       "maxAbsError": float(np.max(np.abs(observed-expected)))})
    maximum = max(item["maxAbsError"] for item in errors)
    _require(math.isfinite(center_error) and math.isfinite(maximum), "nonfinite synthetic result")
    return {"id": fixture["id"], "passed": cpu_equal and center_error <= gates["centerErrorPixelsMax"]
            and maximum <= gates["syntheticRgbAbsMax"], "analyticReproduction": cpu_equal,
            "centerErrorPixels": center_error, "maxRgbAbsError": maximum, "samples": errors,
            "displayTransfer": "native identity; display-reference is independently registered, not applied to trained RGB",
            "independentCpuAndMutationVerification": "REQUIRED in global verification; analytic reproduction is not independent acceptance"}


def run_validation(config):
    """Consume immutable audited inputs and produce a new, honest run report."""
    keys = {"schemaVersion", "projectDirectory", "cameraContract", "protectedInputs", "audit",
            "policyPath", "policySHA256", "sourcePLY", "shaderDirectory", "outputDirectory", "tools", "buildEvidence"}
    _require(set(config) == keys and type(config["schemaVersion"]) is int and config["schemaVersion"] == 1,
             "unknown/missing validation config fields")
    for key in keys - {"schemaVersion", "policySHA256", "tools"}:
        _require(isinstance(config[key], str) and Path(config[key]).is_absolute(), "absolute local path required: " + key)
    root = Path(config["outputDirectory"]).resolve()
    root.mkdir(parents=True, exist_ok=False)
    report = {"schemaVersion": 1, "status": "FAILED", "passed": False, "synthetic": [], "views": [],
              "visualInspection": "REQUIRED; not performed by orchestrator",
              "independentCpuAndMutationVerification": "REQUIRED in global verification",
              "productAcceptance": "NOT_VERIFIED", "android": "NOT_VERIFIED"}
    inventory = None
    policy = None
    try:
        _write(root / "local-config.json", config)
        inventory = _read(config["protectedInputs"])
        report["protectedBefore"] = verify_protected_inputs(inventory)
        cameras = _read(config["cameraContract"])
        validate_camera_contract(cameras)
        policy = load_policy(config["policyPath"], config["policySHA256"])
        _require(cameras["views"] == policy["views"], "policy views differ from audited cameras")
        for field, path in (("protectedInputsSHA256", config["protectedInputs"]), ("auditSHA256", config["audit"])):
            digest = _identity(path)["sha256"]
            _require(digest == cameras[field] == policy[field], "audit identity mismatch: " + field)
        ply = _identity(config["sourcePLY"])
        _require(any(entry["sha256"] == ply["sha256"] and entry["bytes"] == ply["bytes"]
                     and Path(entry["path"]).resolve() == Path(ply["path"]) for entry in inventory["entries"]),
                 "source PLY is absent from protected inventory")
        report["policySHA256"] = config["policySHA256"]
        report["buildEvidence"] = _build_bindings(config, policy, _resolve_tools(config["tools"]))
        (root / "synthetic").mkdir()
        for fixture in policy["fixtures"]["fixtures"]:
            try:
                camera = {"id": fixture["id"], **fixture["camera"]}
                record, pixels = _capture(camera, fixture["sourcePLY"]["path"], config, policy, root / "synthetic" / fixture["id"])
                result = _synthetic(fixture, record, pixels, policy)
                result["capture"] = record
                report["synthetic"].append(result)
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                report["synthetic"].append({"id": fixture["id"], "passed": False, "error": str(error)})
        _write(root / "synthetic-report.json", report["synthetic"])
        _require(len(report["synthetic"]) == 8 and all(item["passed"] for item in report["synthetic"]),
                 "synthetic Vulkan gates failed; six-view scoring not executed")
        (root / "views").mkdir()
        failures = []
        for camera in cameras["views"]:
            try:
                record, _ = _capture(camera, config["sourcePLY"], config, policy, root / "views" / camera["id"])
                report["views"].append(record)
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                failures.append({"id": camera["id"], "error": str(error)})
        report["captureFailures"] = failures
        _require(not failures and len(report["views"]) == 6, "incomplete six-view captures; scoring not executed")
        manifest = {"schemaVersion": 1, "policySHA256": config["policySHA256"], "views": report["views"]}
        for key in ("auditSHA256", "protectedInputsSHA256", "buildIdentity", "deviceIdentity"):
            manifest[key] = copy.deepcopy(policy[key])
        # Revalidate all inputs/runtime artifacts before first score or difference.
        verify_protected_inputs(inventory)
        load_policy(config["policyPath"], config["policySHA256"])
        _write(root / "capture-manifest.json", manifest)
        report["comparison"] = compare_registered(policy, manifest, str(root / "comparison"))
        report["passed"] = report["comparison"]["passed"]
        report["status"] = "METRIC_GATES_PASSED_REVIEW_REQUIRED" if report["passed"] else "FAILED"
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        try:
            _require(inventory is not None, "protected inventory was not loaded")
            report["protectedAfter"] = verify_protected_inputs(inventory)
            if policy is not None:
                load_policy(config["policyPath"], config["policySHA256"])
                if "buildEvidence" in report:
                    _check_identity(report["buildEvidence"]["identity"])
        except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
            report["protectedAfter"] = {"unchanged": False, "error": str(error)}
            report["passed"], report["status"] = False, "FAILED"
        _write(root / "validation-report.json", report)
    return report


def main(argv=None):
    """Return nonzero for any incomplete capture, changed input or failed gate."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = run_validation(_read(args.config))
        print("[OK] metric gates passed; review still required" if report["passed"] else "[FAIL] validation failed; inspect validation-report.json")
        return 0 if report["passed"] else 1
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print("[FAIL] " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
