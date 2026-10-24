#!/usr/bin/env python3
"""Run the established four-scenario harness with explicit off/diagnostic modes."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import zipfile
from profiling_tools import ROOT, checked, lock_data, run, sha256, write_json
from frame_optimization_quality import select_screenshot_frame, reference_readiness
from audit_frame_diagnostics import audit_lifecycle_capture

MANIFEST_HASH = "007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816"
SCENARIOS = ("zoom", "repeatzoom", "stationary", "rotation")


def capture(args):
    """Preserve bounded raw artifacts and command exits, including failed captures."""
    if not all(re.fullmatch(r"[A-Za-z0-9_.-]+", v) for v in (args.package, args.test_package)):
        raise ValueError("Invalid package name")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", args.session):
        raise ValueError("Invalid unique capture session")
    if args.output.exists():
        raise ValueError("Output already exists; never overwrite raw evidence")
    scenarios = args.scenarios.split(",")
    if not scenarios or len(set(scenarios)) != len(scenarios) or any(s not in SCENARIOS for s in scenarios):
        raise ValueError("Select distinct established scenario names")
    if "repeatzoom" in scenarios and ("zoom" not in scenarios or scenarios.index("zoom") > scenarios.index("repeatzoom")):
        raise ValueError("First and repeat zoom must share one owner session, in that order")
    if not 6 <= args.duration_seconds <= 60 or not 1 <= args.round_index <= 3:
        raise ValueError("Duration must be 6..60 seconds and round index 1..3")
    if args.purpose == "performance" and args.duration_seconds != 60:
        raise ValueError("Performance windows require 60 seconds; use diagnostic for shorter observations")
    if args.purpose == "quality" and args.mode != "off":
        raise ValueError("Quality replay requires profiling off")
    bounded_p0 = getattr(args, "bounded_p0", False)
    reference_wait = getattr(args, "reference_wait_seconds", 3)
    if not 0 <= reference_wait <= 15:
        raise ValueError("Reference readiness polling must be bounded to 0..15 seconds")
    if bounded_p0 and (args.purpose == "performance" or args.duration_seconds > 12):
        raise ValueError("Bounded P0 acquisition is diagnostic/quality only and at most 12 seconds per scenario")
    prior_audit = getattr(args, "prior_quality_audit", None)
    if args.purpose == "quality":
        if prior_audit is None or prior_audit.stat().st_size > 1024 * 1024:
            raise ValueError("Quality replay requires --prior-quality-audit from frame_optimization_quality.py audit")
        audit = json.loads(prior_audit.read_text(encoding="utf-8"))
        if audit.get("command") != "audit" or audit.get("status") != "pass" or not isinstance(audit.get("inventory"), list):
            raise ValueError("Prior quality inventory audit did not complete")
        for entry in audit.get("sourceFiles", []):
            path = Path(entry["path"])
            if path.stat().st_size != entry["bytes"] or sha256(path) != entry["sha256"]:
                raise ValueError("Prior quality audit source changed: " + str(path))
    instrumentation_timeout = 120 + len(scenarios) * (args.duration_seconds + 12 + (35 + 2 * reference_wait if args.purpose == "quality" else 0))
    # Preserve the established analyze_profile flat-input entry point for short
    # diagnostics. Longer captures use bounded per-scenario measurement inputs.
    raw_layout = "flat" if args.duration_seconds <= 12 and args.purpose == "diagnostic" else "scenario"
    if not args.manifest.resolve().is_relative_to((ROOT / "build/android-builtin-lod").resolve()):
        raise ValueError("Use the original build/android-builtin-lod manifest cache")
    if sha256(args.manifest) != MANIFEST_HASH:
        raise ValueError("Original manifest identity mismatch")
    with zipfile.ZipFile(args.apk) as apk:
        scene_entries = [i for i in apk.namelist() if i.endswith("/scene.mgs")]
        import hashlib
        if not scene_entries or not any(hashlib.sha256(apk.read(i)).hexdigest() == MANIFEST_HASH for i in scene_entries):
            raise ValueError("Actual APK does not contain original builtin scene manifest")
        apk_members = [{"path": i, "bytes": apk.getinfo(i).file_size, "sha256": hashlib.sha256(apk.read(i)).hexdigest()}
                       for i in apk.namelist() if i.endswith((".so", ".spv", "scene.mgs")) or "inventory" in i]
    args.output.mkdir(parents=True)
    commands = []
    prefix = [args.adb, "-s", args.serial]
    def adb(*parts, timeout=30, required=True):
        result = run(prefix + list(parts), timeout)
        commands.append(result)
        write_json(args.output / "commands.json", commands)
        if required and result.get("exitCode") != 0:
            raise RuntimeError(json.dumps(result))
        return result
    evidence = {"schemaVersion": 1, "mode": args.mode, "captureSession": args.session,
                "sourceHead": checked(["git", "rev-parse", "HEAD"]).strip(),
                "sourceStatus": checked(["git", "status", "--short"]),
                "sourceDiffSha256": None, "apk": str(args.apk.resolve()), "apkBytes": args.apk.stat().st_size, "apkSha256": sha256(args.apk),
                "apkMembers": apk_members, "manifestSha256": MANIFEST_HASH, "tools": lock_data(),
                "serial": args.serial, "package": args.package, "testPackage": args.test_package,
                "capturePurpose": args.purpose, "roundIndex": args.round_index, "selectedScenarios": scenarios,
                "requestedDurationNanoseconds": args.duration_seconds * 1000000000,
                "instrumentationTimeoutSeconds": instrumentation_timeout,
                "rawLayout": raw_layout,
                "boundedP0": bounded_p0, "referenceWaitSeconds": reference_wait,
                "acceptanceEligibleDuration": args.purpose == "performance" and args.duration_seconds == 60,
                "builtinDiagnosticsStillEnabled": ["native frame ring", "GPU timing readback", "cached diagnostics serialization", "harness snapshot/file export approximately every second"],
                "diagnosticAdditions": ["native profiling gate and atrace", "Perfetto system tracing", "host dumpsys meminfo polling"] if args.mode == "diagnostic" else [],
                "gpuMemory": {"status": "unavailable", "reason": "No validated per-process driver GPU counter; inspect preserved gpu-probe and native managed Vulkan allocation observations separately"},
                "physicalDisplayVerified": False, "completed": False}
    import hashlib
    evidence["sourceDiffSha256"] = hashlib.sha256(checked(["git", "diff", "--binary"]).encode()).hexdigest()
    if args.build_identity:
        evidence["buildIdentityFile"] = str(args.build_identity.resolve())
        evidence["buildIdentitySha256"] = sha256(args.build_identity)
    if prior_audit:
        evidence["priorQualityAudit"] = {"path": str(prior_audit.resolve()), "sha256": sha256(prior_audit)}
    write_json(args.output / "capture.json", evidence)
    stop = threading.Event()
    poller = None
    trace_pid = None
    trace_stopped = False
    remote_trace = "/data/misc/perfetto-traces/" + args.session + ".pftrace"
    def stop_trace():
        nonlocal trace_stopped
        if not trace_pid or trace_stopped:
            return
        trace_stopped = True
        evidence["traceStop"] = adb("shell", "kill", "-TERM", trace_pid, required=False)
        for _ in range(30):
            status = adb("shell", "kill", "-0", trace_pid, required=False)
            if status.get("exitCode") != 0:
                return
            time.sleep(.2)
        evidence["traceStopTimeout"] = True
    def cleanup_quality_exports(harness):
        """Verify pulled durable-copy receipts without assuming run-as support."""
        if args.purpose != "quality":
            return
        cleanup = evidence.setdefault("privateScreenshotCleanup", {"files": [], "status": "pending"})
        for path in sorted((harness / "quality").glob("*.observation.json")):
            observation = json.loads(path.read_text(encoding="utf-8"))
            if observation.get("status") != "captured":
                continue
            pose = path.name[:-len(".observation.json")]
            receipt_path = path.parent / (pose + ".private-copy-receipt.json")
            if not receipt_path.is_file() or receipt_path.stat().st_size > 16384:
                raise ValueError("Durable screenshot-copy receipt missing or oversized")
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            files = receipt.get("files", [])
            if receipt.get("schemaVersion") != 1 or receipt.get("durableCopyVerified") is not True or len(files) != 2:
                raise ValueError("Durable screenshot-copy receipt invalid")
            expected = {"quality/" + pose + ".png", "quality/" + pose + ".native.json"}
            if {entry.get("path") for entry in files} != expected:
                raise ValueError("Receipt references unexpected screenshot files")
            for entry in files:
                local = harness / entry["path"]
                if entry.get("sourceSha256") != entry.get("copySha256") or not local.is_file() or local.stat().st_size != entry.get("bytes") or sha256(local) != entry.get("copySha256"):
                    raise ValueError("Pulled screenshot/native sidecar differs from durable device receipt")
                cleanup["files"].append(dict(entry, pulledSha256Verified=True))
            if observation.get("privateExportFilesRemoved") is not True:
                raise ValueError("Private screenshot originals were not safely removed")
        cleanup["status"] = "durable_device_copy_and_host_hash_verified"
    try:
        evidence["device"] = adb("shell", "getprop")["stdout"]
        evidence["pageSize"] = adb("shell", "getconf", "PAGE_SIZE")["stdout"].strip()
        installed_path = adb("shell", "pm", "path", args.package)["stdout"].strip().splitlines()
        base = [line[len("package:"):] for line in installed_path if line.startswith("package:") and line.endswith("/base.apk")]
        if len(base) != 1:
            raise ValueError("Cannot uniquely identify installed base APK")
        installed_hash = adb("shell", "sha256sum", base[0])["stdout"].split()[0]
        evidence["installedApkSha256"] = installed_hash
        if installed_hash != evidence["apkSha256"]:
            raise ValueError("Installed APK does not match selected artifact")
        if args.mode == "diagnostic":
            config = Path(__file__).with_name("perfetto_viewer.pbtxt").read_text(encoding="utf-8").replace("@PACKAGE@", args.package)
            # Extend the declared bounded trace to cover readiness and the actual
            # selected windows. The existing 512 MiB file bound remains in force;
            # reaching it is unavailable evidence, never presumed full coverage.
            config = re.sub(r"(?m)^duration_ms:\s*\d+", "duration_ms: %d" % (instrumentation_timeout * 1000), config)
            evidence["perfettoDurationMilliseconds"] = instrumentation_timeout * 1000
            config_path = args.output / "perfetto-config.pbtxt"
            config_path.write_text(config, encoding="utf-8")
            remote_config = "/data/local/tmp/" + args.session + ".pbtxt"
            adb("push", str(config_path), remote_config)
            evidence["perfettoDeviceVersion"] = adb("shell", "perfetto", "--version", required=False)
            write_json(args.output / "gpu-probe.json", adb("shell", "dumpsys", "gpu", "--gpumem", required=False))
            started = adb("shell", "perfetto", "--background-wait", "--txt", "-c", remote_config, "-o", remote_trace, timeout=40)
            pids = re.findall(r"(?m)^\s*(\d+)\s*$", started["stdout"])
            if len(pids) != 1:
                raise ValueError("Perfetto did not return one numeric background PID")
            trace_pid = pids[0]
            def memory_polls():
                index = 0
                while not stop.is_set():
                    started_ns = time.monotonic_ns()
                    result = run(prefix + ["shell", "dumpsys", "meminfo", args.package], 15)
                    result.update({"hostStartMonotonicNs": started_ns, "hostEndMonotonicNs": time.monotonic_ns(),
                                   "clockDomain": "host_monotonic_unaligned_to_device"})
                    write_json(args.output / "memory" / ("sample-%04d.json" % index), result)
                    index += 1
                    stop.wait(args.memory_period)
            poller = threading.Thread(target=memory_polls, daemon=True)
            poller.start()
        command = ["shell", "am", "instrument", "-w", "-e", "captureSession", args.session,
                   "-e", "profilingMode", args.mode, "-e", "durationSeconds", str(args.duration_seconds),
                   "-e", "roundIndex", str(args.round_index), "-e", "capturePurpose", args.purpose,
                   "-e", "scenarios", args.scenarios, "-e", "rawLayout", raw_layout,
                   "-e", "boundedP0", str(bounded_p0).lower(),
                   "-e", "referenceWaitSeconds", str(reference_wait)]
        if args.settings_base64_file:
            command += ["-e", "settingsBase64", args.settings_base64_file.read_text(encoding="ascii").strip()]
        command += [args.test_package + "/com.monsterengine.viewer.ViewerMeasurementInstrumentation"]
        instrumentation = adb(*command, timeout=instrumentation_timeout)
        # Stop collection before transferring hundreds of MiB of JSON; keeping
        # tracing active during pull can overwrite the first scenario window.
        stop.set()
        stop_trace()
        # am instrument can return shell exit 0 while instrumentation itself failed.
        if "Capture completed:" not in instrumentation["stdout"] or "INSTRUMENTATION_FAILED" in instrumentation["stdout"]:
            raise ValueError("Harness did not report completed capture")
        remote = "/sdcard/Android/data/" + args.package + "/files/" + args.session
        adb("pull", remote, str(args.output / "harness"))
        cleanup_quality_exports(args.output / "harness")
        metadata = json.loads((args.output / "harness/metadata.json").read_text(encoding="utf-8"))
        if not metadata.get("captureCompleted") or metadata.get("profilingMode") != args.mode or metadata.get("profilingEnabled") is not (args.mode == "diagnostic"):
            raise ValueError("Harness mode/completion echo missing or mismatched; no silent profiling fallback")
        if metadata.get("profilingStateRestored") is not True or metadata.get("finalProfilingEnabled") != metadata.get("originalProfilingEnabled"):
            raise ValueError("Harness did not acknowledge original profiling state restoration")
        restored = metadata.get("settingsOriginalBytesRestored") is True
        fresh_settings = metadata.get("settingsOriginalAbsentRestored") is True
        if metadata.get("settingsLiveSemanticallyEqual") is not True or not (restored or fresh_settings):
            raise ValueError("Harness did not verify unchanged live settings and prior settings byte restoration")
        if [x["name"] for x in metadata["scenarios"]] != scenarios:
            raise ValueError("Selected scenario windows missing or reordered")
        for window in metadata["scenarios"]:
            if window.get("roundIndex") != args.round_index or window.get("capturePurpose") != args.purpose or not window.get("captureComplete"):
                raise ValueError("Window round, purpose or completion mismatch")
            if window.get("requestedDurationNanoseconds") != args.duration_seconds * 1000000000:
                raise ValueError("Window requested duration mismatch")
            if window.get("windowDurationNanoseconds") != window["endOwnerNanoseconds"] - window["startOwnerNanoseconds"]:
                raise ValueError("Window duration is not the device boundary difference")
            if window["windowDurationNanoseconds"] < args.duration_seconds * 1000000000:
                raise ValueError("Incomplete scenario duration")
            expected_raw = "raw/" + window["name"] if raw_layout == "scenario" else "raw"
            if window.get("rawDirectory") != expected_raw:
                raise ValueError("Harness raw layout mismatch")
            raw_dir = args.output / "harness" / expected_raw
            files = sorted(raw_dir.glob("*.json"))
            if not files or len(files) > 512 or sum(p.stat().st_size for p in files) > 256 * 1024 * 1024 or any(p.stat().st_size > 16 * 1024 * 1024 for p in files):
                raise ValueError("Scenario raw export violates existing bounded consumer contract")
        evidence["scenarioRawInputs"] = [{"scenario": w["name"], "input": str(args.output / "harness" / w["rawDirectory"])} for w in metadata["scenarios"]]
        lifecycle = audit_lifecycle_capture(args.output)
        lifecycle_path = args.output / "lifecycle-audit.json"
        if lifecycle_path.exists():
            raise ValueError("Refusing overwrite of lifecycle analysis")
        write_json(lifecycle_path, lifecycle)
        evidence["lifecycleAudit"] = {"path": str(lifecycle_path.resolve()), "sha256": sha256(lifecycle_path),
                                      "status": lifecycle["status"], "upstreamEvidenceStatus": lifecycle["upstreamEvidenceStatus"],
                                      "chainStatusCounts": lifecycle["chainStatusCounts"],
                                      "interpretation": "Analyzer execution only; missing chains/losses remain explicit and do not grant quality or optimization admission"}
        if lifecycle["status"] != "pass":
            raise ValueError("Conflicting native lifecycle identities; inspect lifecycle-audit.json")
        if args.purpose == "quality":
            observations = []
            for path in sorted((args.output / "harness/quality").glob("*.observation.json")):
                observation = json.loads(path.read_text(encoding="utf-8"))
                item = {"path": str(path), "status": observation.get("status"),
                                     "reason": observation.get("reason"),
                                     "physicalReadiness": observation.get("physicalReadiness"),
                                     "frameAssociation": observation.get("frameAssociation")}
                observations.append(item)
                try:
                    pose = path.name[:-len(".observation.json")]
                    native = json.loads(path.with_name(pose + ".native.json").read_text(encoding="utf-8"))
                    diagnostic = json.loads(path.with_name(pose + ".diagnostics.json").read_text(encoding="utf-8"))
                    result = observation.get("nativeResult", {})
                    if observation.get("status") != "captured" or result.get("status") != "completed" or result.get("gpuCompleted") is not True or result.get("metadata") != native:
                        raise ValueError("GPU-completed screenshot/sidecar mismatch")
                    frame = select_screenshot_frame(native, diagnostic["payload"]["diagnostics"]["frames"])
                    readiness = reference_readiness(frame, frame["effectiveConfig"]["qualityPixels"])
                    if observation.get("frameAssociation", {}).get("status") != "matched" or observation.get("physicalReadiness", {}).get("status") != readiness["status"]:
                        raise ValueError("Harness/host exact association or physical readiness disagreement")
                    item.update({"hostFrameAssociation": "matched", "hostPhysicalReadiness": readiness,
                                 "nativeBuildIdentity": frame.get("buildIdentity"),
                                 "externalBuildBindingRequired": frame.get("buildIdentity") is None})
                except (OSError, ValueError, KeyError, TypeError) as error:
                    item.update({"hostFrameAssociation": "blocked", "hostAssociationReason": str(error)})
            evidence["qualityAcquisition"] = {"qualityStatus": "blocked", "observations": observations,
                "exactAssociationsComplete": len(observations) == 2 * len(scenarios) and all(item.get("hostFrameAssociation") == "matched" for item in observations),
                "nextAction": "Inspect actual original PNG, same-frame readiness and identities; freeze fixed full-image/detail-ROI policy before any candidate. Missing PNG or readiness remains blocked."}
            if not evidence["qualityAcquisition"]["exactAssociationsComplete"]:
                raise ValueError("Quality capture lacks exact screenshot associations; inspect preserved blocked observations")
        evidence["completed"] = True
        if evidence.get("traceStopTimeout"):
            evidence["completed"] = False
    except (OSError, ValueError, KeyError, RuntimeError, zipfile.BadZipFile) as error:
        evidence["error"] = str(error)
        stop.set()
        stop_trace()
        # Preserve partial harness observations on failure as well.
        partial = adb("pull", "/sdcard/Android/data/" + args.package + "/files/" + args.session,
                      str(args.output / "harness-failed"), required=False)
        if partial.get("exitCode") == 0 and "privateScreenshotCleanup" not in evidence:
            try:
                cleanup_quality_exports(args.output / "harness-failed")
            except (OSError, ValueError, KeyError, RuntimeError) as cleanup_error:
                evidence["privateScreenshotCleanupError"] = str(cleanup_error)
    finally:
        stop.set()
        if poller:
            poller.join(timeout=17)
        if trace_pid:
            stop_trace()
            if evidence.get("traceStopTimeout"):
                evidence["completed"] = False
            pulled = adb("pull", remote_trace, str(args.output / "system.pftrace"), required=False)
            if pulled.get("exitCode") == 0:
                evidence["traceSha256"] = sha256(args.output / "system.pftrace")
                if (args.output / "system.pftrace").stat().st_size >= 536870912:
                    evidence["traceFileBoundReached"] = True
                    evidence["completed"] = False
            else:
                evidence["completed"] = False
        try:
            evidence["sourceHeadAfter"] = checked(["git", "rev-parse", "HEAD"]).strip()
            evidence["sourceDiffSha256After"] = hashlib.sha256(checked(["git", "diff", "--binary"]).encode()).hexdigest()
            evidence["apkSha256After"] = sha256(args.apk)
            if evidence["sourceHeadAfter"] != evidence["sourceHead"] or evidence["sourceDiffSha256After"] != evidence["sourceDiffSha256"] or evidence["apkSha256After"] != evidence["apkSha256"]:
                raise ValueError("Source/package identity changed during capture")
        except (OSError, ValueError, RuntimeError) as error:
            evidence["identityContinuityError"] = str(error)
            evidence["completed"] = False
        evidence["rawEvidence"] = [{"path": str(p.relative_to(args.output)), "bytes": p.stat().st_size, "sha256": sha256(p)}
                                   for folder in ("harness", "harness-failed") for p in sorted((args.output / folder).rglob("*")) if p.is_file()]
        write_json(args.output / "capture.json", evidence)
    return 0 if evidence["completed"] else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mode", required=True, choices=("off", "diagnostic"))
    parser.add_argument("--session", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--test-package", required=True)
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--build-identity", type=Path)
    parser.add_argument("--settings-base64-file", type=Path)
    parser.add_argument("--memory-period", type=float, default=2)
    parser.add_argument("--duration-seconds", type=int, default=12, help="6..60; performance requires 60, shorter runs are diagnostic only")
    parser.add_argument("--round-index", type=int, default=1, help="1..3; run separate opens/sessions for three independent rounds")
    parser.add_argument("--purpose", choices=("performance", "diagnostic", "quality"), default="diagnostic")
    parser.add_argument("--bounded-p0", action="store_true", help="Explicit short P0 diagnostic/reference probe; forbids performance and windows over 12 seconds")
    parser.add_argument("--reference-wait-seconds", type=int, default=3, help="0..15 seconds of native-readiness polling per quality image; elapsed time never proves loading")
    parser.add_argument("--prior-quality-audit", type=Path, help="Successful immutable inventory report required before requesting quality images")
    parser.add_argument("--scenarios", default=",".join(SCENARIOS), help="Comma-separated selected scenarios; zoom and repeatzoom share the owner")
    args = parser.parse_args()
    try:
        if not 1 <= args.memory_period <= 30:
            raise ValueError("Memory period must be 1..30 seconds")
        return capture(args)
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
