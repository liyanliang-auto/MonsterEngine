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

MANIFEST_HASH = "007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816"


def capture(args):
    """Preserve bounded raw artifacts and command exits, including failed captures."""
    if not all(re.fullmatch(r"[A-Za-z0-9_.-]+", v) for v in (args.package, args.test_package)):
        raise ValueError("Invalid package name")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", args.session):
        raise ValueError("Invalid unique capture session")
    if args.output.exists():
        raise ValueError("Output already exists; never overwrite raw evidence")
    if not args.manifest.resolve().is_relative_to((ROOT / "build/android-builtin-lod").resolve()):
        raise ValueError("Use the original build/android-builtin-lod manifest cache")
    if sha256(args.manifest) != MANIFEST_HASH:
        raise ValueError("Original manifest identity mismatch")
    with zipfile.ZipFile(args.apk) as apk:
        scene_entries = [i for i in apk.namelist() if i.endswith("/scene.mgs")]
        import hashlib
        if not scene_entries or not any(hashlib.sha256(apk.read(i)).hexdigest() == MANIFEST_HASH for i in scene_entries):
            raise ValueError("Actual APK does not contain original builtin scene manifest")
        apk_members = [{"path": i, "sha256": hashlib.sha256(apk.read(i)).hexdigest()}
                       for i in apk.namelist() if i.endswith(".so") or "inventory" in i or i.endswith("scene.mgs")]
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
                "sourceDiffSha256": None, "apk": str(args.apk.resolve()), "apkSha256": sha256(args.apk),
                "apkMembers": apk_members, "manifestSha256": MANIFEST_HASH, "tools": lock_data(),
                "serial": args.serial, "package": args.package, "testPackage": args.test_package,
                "builtinDiagnosticsStillEnabled": ["native frame ring", "GPU timing readback", "cached diagnostics serialization", "harness snapshot/file export approximately every 500 ms"],
                "diagnosticAdditions": ["native profiling gate and atrace", "Perfetto system tracing", "host dumpsys meminfo polling"] if args.mode == "diagnostic" else [],
                "gpuMemory": {"status": "unavailable", "reason": "No validated per-process driver GPU counter; inspect preserved gpu-probe and native managed Vulkan allocation observations separately"},
                "physicalDisplayVerified": False, "completed": False}
    import hashlib
    evidence["sourceDiffSha256"] = hashlib.sha256(checked(["git", "diff", "--binary"]).encode()).hexdigest()
    if args.build_identity:
        evidence["buildIdentityFile"] = str(args.build_identity.resolve())
        evidence["buildIdentitySha256"] = sha256(args.build_identity)
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
                   "-e", "profilingMode", args.mode]
        if args.settings_base64_file:
            command += ["-e", "settingsBase64", args.settings_base64_file.read_text(encoding="ascii").strip()]
        command += [args.test_package + "/com.monsterengine.viewer.ViewerMeasurementInstrumentation"]
        instrumentation = adb(*command, timeout=240)
        # Stop collection before transferring hundreds of MiB of JSON; keeping
        # tracing active during pull can overwrite the first scenario window.
        stop.set()
        stop_trace()
        # am instrument can return shell exit 0 while instrumentation itself failed.
        if "Capture completed:" not in instrumentation["stdout"] or "INSTRUMENTATION_FAILED" in instrumentation["stdout"]:
            raise ValueError("Harness did not report completed capture")
        remote = "/sdcard/Android/data/" + args.package + "/files/" + args.session
        adb("pull", remote, str(args.output / "harness"))
        metadata = json.loads((args.output / "harness/metadata.json").read_text(encoding="utf-8"))
        if not metadata.get("captureCompleted") or metadata.get("profilingMode") != args.mode or metadata.get("profilingEnabled") is not (args.mode == "diagnostic"):
            raise ValueError("Harness mode/completion echo missing or mismatched; no silent profiling fallback")
        if metadata.get("finalProfilingEnabled") is not False:
            raise ValueError("Harness did not acknowledge final profiling disable")
        restored = metadata.get("settingsOriginalBytesRestored") is True
        fresh_settings = args.settings_base64_file is not None and "settingsOriginalBytesRestored" not in metadata
        if metadata.get("settingsLiveSemanticallyEqual") is not True or not (restored or fresh_settings):
            raise ValueError("Harness did not verify unchanged live settings and prior settings byte restoration")
        if [x["name"] for x in metadata["scenarios"]] != ["stationary", "rotation", "zoom", "repeatzoom"]:
            raise ValueError("Four established scenario windows required")
        evidence["completed"] = True
        if evidence.get("traceStopTimeout"):
            evidence["completed"] = False
    except (OSError, ValueError, KeyError, RuntimeError, zipfile.BadZipFile) as error:
        evidence["error"] = str(error)
        stop.set()
        stop_trace()
        # Preserve partial harness observations on failure as well.
        adb("pull", "/sdcard/Android/data/" + args.package + "/files/" + args.session,
            str(args.output / "harness-failed"), required=False)
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
            else:
                evidence["completed"] = False
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
