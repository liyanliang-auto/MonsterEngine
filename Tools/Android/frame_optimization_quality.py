#!/usr/bin/env python3
"""Freeze original-model references before candidates; compare registered poses.

Commands take --input JSON and --output JSON (exclusive create). compare also
requires --policy, a successful freeze report. See the round quality contract
for input schemas. Images are never resized, aligned, sharpened or fitted.
"""
import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import zipfile


MANIFEST_SHA256 = "007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816"
STATIONARY_FREEZE_SHA256 = "47d05b4b9491a34d0d0a98b6f7674a87712f2d3a55d5cea14a3bd6ec153161df"
THRESHOLDS = {"psnrDbMin": 45.0, "ssimMin": 0.995, "maeMax": 1 / 255}
ALGORITHM = {
    "implementation": "monster-frame-quality-v1",
    "normalization": "uint8 / 255, float64; no transfer conversion",
    "ssim": "11x11 uniform valid, population covariance, mean RGB and centers",
    "ssimWindow": 11, "C1": 0.0001, "C2": 0.0009,
    "psnr": "-10 log10(mean squared RGB error); null with identical=true if zero",
    "mae": "mean absolute RGB error",
    "outputPrecision": "IEEE754 float64 serialized JSON",
}
QUALITY_KEYS = {"shDegree", "sourcePrecision", "renderWidth", "renderHeight",
                "lodErrorThreshold", "resolutionScale", "presentationPolicy"}
ORBIT_FIELDS = ("cameraX", "cameraY", "cameraZ", "cameraYaw", "cameraPitch", "cameraDistance")
PRESENTATION_FIELDS = ("presentationWidth", "presentationHeight", "displayWidth", "displayHeight",
                       "presentationFormat", "presentationColorSpace", "presentationFormatSRGB",
                       "presentationDisplayScale", "presentationSharpeningStrength")
LIMITATIONS = [
    "Registered-pose comparison only; does not establish temporal path coverage, FPS or AC-01..AC-08.",
    "Loaded/completion metadata is evidence supplied by the capture owner; inspect its raw evidence.",
    "Quality readback/encoding replay is separate from tools-off performance capture.",
    "SHA256 detects changed policy bytes against the preregistered digest, not an external trusted timestamp.",
]


class EvidenceUnavailable(ValueError):
    """Required original capture or metric environment is unavailable."""


def _json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _hex(value):
    return isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value) is not None


def _object(value):
    return value if isinstance(value, dict) else {}


def _read(path, sources):
    path = Path(path).resolve(strict=True)
    if path.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("quality evidence bounded read exceeds 128 MiB: " + str(path))
    data = path.read_bytes()
    sources.append({"path": str(path), "bytes": len(data), "sha256": _sha(data)})
    return data


def _load(path, sources):
    def no_constants(value):
        raise ValueError("nonfinite JSON constant: " + value)
    value = json.loads(_read(path, sources), parse_constant=no_constants, object_pairs_hook=_unique_object)
    if not isinstance(value, dict):
        raise ValueError("expected JSON object")
    return value


def _unique_object(pairs):
    """Reject duplicate JSON fields rather than silently replacing evidence."""
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field: " + key)
        result[key] = value
    return result


def _document(entry, sources):
    value = json.loads(_identity(entry, sources), object_pairs_hook=_unique_object,
                       parse_constant=lambda v: (_ for _ in ()).throw(ValueError("nonfinite JSON: " + v)))
    if not isinstance(value, dict):
        raise ValueError("identity-verified JSON must contain an object")
    return value


def _identity(entry, sources):
    if not isinstance(entry, dict) or not _hex(entry.get("sha256")) or type(entry.get("bytes")) is not int:
        raise ValueError("file identity needs path, bytes and lowercase sha256")
    data = _read(entry["path"], sources)
    if len(data) != entry["bytes"] or _sha(data) != entry["sha256"]:
        raise ValueError("input identity changed: " + str(entry["path"]))
    return data


def _verify_large_identity(entry, sources):
    if not isinstance(entry, dict) or not _hex(entry.get("sha256")) or type(entry.get("bytes")) is not int:
        raise ValueError("invalid original asset identity")
    path = Path(entry["path"]).resolve(strict=True)
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    sources.append({"path": str(path), "bytes": size, "sha256": digest.hexdigest()})
    if size != entry["bytes"] or digest.hexdigest() != entry["sha256"]:
        raise ValueError("original asset bytes changed")


def _libraries():
    try:
        import numpy as np
        import PIL
        from PIL import Image
    except ImportError as error:
        raise EvidenceUnavailable("numpy and Pillow required; install an explicitly selected version before freezing: " + str(error)) from error
    return np, Image, {"numpy": np.__version__, "Pillow": PIL.__version__,
                       "python": sys.version.split()[0]}


def _settings(value):
    if not isinstance(value, dict) or not QUALITY_KEYS.issubset(value):
        raise ValueError("qualitySettings missing explicit quality invariants")
    if value["shDegree"] != 3 or value["sourcePrecision"] != "FP32" or value["resolutionScale"] != 1:
        raise ValueError("original SH3/FP32/effective resolution required")
    for key in ("renderWidth", "renderHeight"):
        if type(value[key]) is not int or not 11 <= value[key] <= 16384:
            raise ValueError("invalid render dimensions")
    if not isinstance(value["lodErrorThreshold"], (int, float)) or isinstance(value["lodErrorThreshold"], bool) or not math.isfinite(value["lodErrorThreshold"]) or value["lodErrorThreshold"] <= 0:
        raise ValueError("invalid LOD threshold")
    if not isinstance(value["presentationPolicy"], dict) or not value["presentationPolicy"]:
        raise ValueError("explicit presentation policy required")
    _json_bytes(value)


def _encoding(value):
    if not isinstance(value, dict) or set(value) != {"format", "alpha", "rowOrigin", "colorSpace"}:
        raise ValueError("encoding must register format, alpha, rowOrigin, colorSpace")
    if value["format"] not in ("png-rgb8", "png-rgba8", "raw-rgba8"):
        raise ValueError("unsupported lossless image format")
    if value["alpha"] not in ("opaque", "straight-over-black", "premultiplied-over-black"):
        raise ValueError("unknown alpha handling")
    if value["format"] == "png-rgb8" and value["alpha"] != "opaque":
        raise ValueError("RGB must be registered opaque")
    if value["rowOrigin"] not in ("top_left", "bottom_left") or value["colorSpace"] not in ("linear-rgb", "srgb-encoded"):
        raise ValueError("unknown row orientation or color space")


def _camera(camera):
    if not isinstance(camera, dict) or not {"pathSha256", "sampleIndex"}.issubset(camera):
        raise ValueError("explicit camera path/sample identity required")
    if not _hex(camera["pathSha256"]) or type(camera["sampleIndex"]) is not int or camera["sampleIndex"] < 0:
        raise ValueError("invalid camera path/sample")
    if camera.get("representation", "matrices-v1") == "matrices-v1":
        for key in ("viewMatrix", "projectionMatrix"):
            if not isinstance(camera.get(key), list) or len(camera[key]) != 16 or any(type(v) not in (int, float) or not math.isfinite(v) for v in camera[key]):
                raise ValueError("invalid camera matrix")
        return
    if camera.get("representation") != "observed-orbit-v1":
        raise ValueError("unknown camera representation")
    if any(type(camera.get(key)) not in (int, float) or not math.isfinite(camera[key]) for key in ORBIT_FIELDS):
        raise EvidenceUnavailable("observed camera orbit fields unavailable")
    if camera["cameraDistance"] <= 0 or camera.get("externalCamera") is not False:
        raise EvidenceUnavailable("orbit identity requires positive distance and externalCamera=false; external orientation needs matrices")
    projection = camera.get("projection")
    if not isinstance(projection, dict) or any(type(projection.get(key)) not in (int, float) or not math.isfinite(projection[key]) for key in ("verticalFovDegrees", "nearPlane", "farPlane", "rollDegrees")):
        raise EvidenceUnavailable("frozen projection and roll values unavailable")
    if not 0 < projection["verticalFovDegrees"] < 180 or not 0 < projection["nearPlane"] < projection["farPlane"] or abs(projection["rollDegrees"]) > 180:
        raise ValueError("invalid orbit projection or roll")
    if not isinstance(projection.get("configIdentity"), dict):
        raise EvidenceUnavailable("projection/roll configuration evidence unavailable")


def _capture_package_members(capture, build):
    """Validate the capture protocol's exact two ELF, nineteen shaders and scene index."""
    natives, shaders = build.get("natives", []), build.get("shaders", [])
    if (not isinstance(natives, list) or not isinstance(shaders, list) or
            any(not isinstance(e, dict) or set(e) != {"path", "bytes", "sha256"} or
                not isinstance(e["path"], str) or type(e["bytes"]) is not int or e["bytes"] < 0 or
                not _hex(e["sha256"]) for e in natives + shaders)):
        raise ValueError("invalid build package member identity")
    manifest = {"path": "assets/scenes/bonsai-lod/scene.mgs", "bytes": 171607,
                "sha256": MANIFEST_SHA256}
    required = natives + shaders + [manifest]
    observed = capture.get("apkMembers")
    if (len(natives) != 2 or len(shaders) != 19 or
            {e.get("path") for e in natives} != {"lib/arm64-v8a/libmonster_android.so", "lib/x86_64/libmonster_android.so"} or
            any(not isinstance(e.get("path"), str) or not e["path"].endswith(".spv") for e in shaders) or
            len({e.get("path") for e in required}) != 22 or
            not isinstance(observed, list) or len(observed) != 22 or
            any(not isinstance(e, dict) for e in observed) or
            len({e.get("path") for e in observed}) != 22 or
            sorted(observed, key=lambda e: e.get("path", "")) != sorted(required, key=lambda e: e["path"])):
        raise ValueError("capture and build actual dual-ABI/19-SPIRV/original-manifest identities differ")
    return manifest


def _external_build_binding(evidence, sources):
    """Verify real package/source receipts when native buildIdentity is unknown.

    This retains the null native field. The external receipt identifies the APK
    actually replace-installed, its ELF/Shader bytes and unchanged source inputs.
    """
    if not all(isinstance(evidence.get(key), dict) for key in ("captureReceipt", "buildReceipt", "screenshotImage")):
        raise EvidenceUnavailable("native build identity unavailable; verified external capture/package/source receipts required")
    capture = _document(evidence["captureReceipt"], sources)
    build = _document(evidence["buildReceipt"], sources)
    if capture.get("schemaVersion") != 1 or capture.get("completed") is not True or capture.get("capturePurpose") != "quality" or capture.get("mode") != "off":
        raise EvidenceUnavailable("external completed unchanged-original quality capture receipt unavailable")
    digest = capture.get("apkSha256")
    if not _hex(digest) or any(capture.get(key) != digest for key in ("installedApkSha256", "apkSha256After")) or capture.get("manifestSha256") != MANIFEST_SHA256:
        raise ValueError("external installed/capture APK identity differs")
    if not _hex(capture.get("sourceDiffSha256")) or capture.get("sourceDiffSha256After") != capture["sourceDiffSha256"] or not capture.get("sourceHead") or capture.get("sourceHeadAfter") != capture["sourceHead"]:
        raise EvidenceUnavailable("external source before/after capture continuity unavailable")
    apk = build.get("apk", {})
    if build.get("schemaVersion") != 1 or apk.get("sha256") != digest or apk.get("bytes") != capture.get("apkBytes"):
        raise ValueError("external build/capture APK binding differs")
    _verify_large_identity(apk, sources)
    command = _document(build["buildCommand"], sources)
    if command.get("exitCode") != 0 or not command.get("executable") or not command.get("arguments"):
        raise EvidenceUnavailable("actual successful package build command receipt unavailable")
    before = _document(build["sourceBefore"], sources)
    after = _document(build["sourceAfter"], sources)
    files = before.get("files")
    if (not isinstance(files, list) or not files or len(files) > 16384 or files != after.get("files") or
            len(files) != len({e.get("path") for e in files})):
        raise ValueError("external compiled source inventory changed across build")
    for entry in files:
        _verify_large_identity(entry, sources)
    with zipfile.ZipFile(apk["path"]) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError("duplicate APK member")
        for key, expected in (("natives", {"lib/arm64-v8a/libmonster_android.so", "lib/x86_64/libmonster_android.so"}), ("shaders", None)):
            entries = build.get(key, [])
            if key == "shaders":
                expected = {name for name in archive.namelist() if name.endswith(".spv")}
            if (not entries or len(entries) != len({e.get("path") for e in entries}) or
                    (key == "shaders" and len(entries) != 19) or
                    (expected is not None and {e.get("path") for e in entries} != expected)):
                raise EvidenceUnavailable("external actual dual-ABI ELF/Shader identities unavailable")
            for entry in entries:
                info = archive.getinfo(entry["path"])
                if info.file_size > 128 * 1024 * 1024 or info.file_size != entry["bytes"] or _sha(archive.read(info)) != entry["sha256"]:
                    raise ValueError("actual APK member identity differs")
        manifest = _capture_package_members(capture, build)
        manifest_names = [name for name in archive.namelist() if name.endswith("scene.mgs")]
        if manifest_names != [manifest["path"]] or archive.getinfo(manifest["path"]).file_size != manifest["bytes"] or _sha(archive.read(manifest["path"])) != manifest["sha256"]:
            raise ValueError("actual APK original manifest member identity differs")
    root = Path(evidence["captureReceipt"]["path"]).resolve().parent
    for key in ("diagnostics", "screenshotMetadata", "screenshotImage") + (("screenshotObservation",) if "screenshotObservation" in evidence else ()):
        entry = evidence[key]
        _identity(entry, sources)
        matches = [item for item in capture.get("rawEvidence", []) if
                   (root / item["path"]).resolve() == Path(entry["path"]).resolve() and
                   item.get("sha256") == entry["sha256"] and item.get("bytes") == entry["bytes"]]
        if len(matches) != 1:
            raise ValueError("external raw capture binding missing: " + key)
    return build


def select_screenshot_frame(screenshot, rows):
    """Select exact post-submit facts; a stale ring is not a prerequisite.

    A row for the same frame/scene/surface must agree even when another identity
    field differs. Resolved timing counters may differ. Never substitute latest.
    """
    joins = ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration", "pipelineGeneration")
    if not isinstance(screenshot, dict) or not isinstance(rows, list):
        raise ValueError("invalid screenshot or diagnostic ring")
    for key in joins:
        if type(screenshot.get(key)) is not int or screenshot[key] <= 0:
            raise ValueError("screenshot identity unavailable: " + key)
    if any(not isinstance(row, dict) for row in rows):
        raise ValueError("invalid diagnostic ring row")
    related = [row for row in rows if all(row.get(key) == screenshot[key]
               for key in ("frameId", "sceneGeneration", "surfaceGeneration"))]
    if len(related) > 1:
        raise ValueError("ambiguous same screenshot diagnostic frame")
    submitted = screenshot.get("viewerFrame")
    if submitted is not None and not isinstance(submitted, dict):
        raise ValueError("invalid post-submit viewerFrame")
    candidates = ([submitted] if submitted is not None else []) + related
    if not candidates:
        raise EvidenceUnavailable("exact screenshot frame missing from stale diagnostic ring")
    for row in candidates:
        measurement = _object(row.get("measurement"))
        for key in joins:
            actual = measurement.get(key) if key == "pipelineGeneration" else row.get(key)
            if type(actual) is not int or actual != screenshot[key]:
                raise ValueError("screenshot diagnostic identity mismatch: " + key)
        if row.get("schemaVersion") != 2 or measurement.get("measurementSchemaVersion") != 2 or _object(row.get("status")).get("ok") is not True:
            raise ValueError("observed diagnostic frame status/schema unsuccessful")
        for axis, field in (("width", "renderWidth"), ("height", "renderHeight")):
            if type(screenshot.get(axis)) is not int or screenshot[axis] <= 0 or row.get(field) != screenshot[axis] or measurement.get(field) != screenshot[axis]:
                raise ValueError("same-frame screenshot/render dimensions mismatch")
        for key in ORBIT_FIELDS:
            if type(measurement.get(key)) not in (int, float) or not math.isfinite(measurement[key]):
                raise ValueError("observed camera unavailable: " + key)
        if measurement.get("externalCamera") not in (False, 0):
            raise EvidenceUnavailable("external camera orientation unavailable")
        for key in ("contentHash", "configHash", "shaderIdentity"):
            actual = row.get(key) if key == "contentHash" else _object(row.get("effectiveConfig")).get(key)
            if not _hex(screenshot.get(key)) or actual != screenshot[key]:
                raise ValueError("screenshot diagnostic source/config/shader mismatch: " + key)
        if row.get("buildIdentity") != screenshot.get("buildIdentity"):
            raise ValueError("screenshot diagnostic build identity mismatch")
        if row.get("buildIdentity") is not None and not _hex(row["buildIdentity"]):
            raise ValueError("invalid native build identity")
    row = candidates[0]
    if len(candidates) == 2:
        other = candidates[1]
        for key in ORBIT_FIELDS + ("pipelineGeneration", "externalCamera"):
            if row["measurement"].get(key) != other["measurement"].get(key):
                raise ValueError("submitted screenshot/ring camera mismatch: " + key)
        for key in ("effectiveConfig",) + PRESENTATION_FIELDS:
            if row.get(key) != other.get(key):
                raise ValueError("submitted screenshot/ring config/presentation mismatch: " + key)
        left = _object(_object(row.get("diagnosticsP0")).get("resources"))
        right = _object(_object(other.get("diagnosticsP0")).get("resources"))
        for key in ("cut", "aggregateLimitBytes", "componentCharges", "reservedBytes", "peakBytes", "admission"):
            if _json_bytes(left.get(key)) != _json_bytes(right.get(key)):
                raise ValueError("submitted screenshot/ring cut/account conflict: " + key)
    return row


def _observed_frame(meta, camera, sources):
    """Join an orbit to actual screenshot completion and exact diagnostic frame."""
    evidence = meta.get("frameObservation")
    if not isinstance(evidence, dict) or not {"diagnostics", "screenshotMetadata"}.issubset(evidence):
        raise EvidenceUnavailable("same-frame raw screenshot/diagnostic join unavailable")
    _identity(camera["projection"]["configIdentity"], sources)
    screenshot = _document(evidence["screenshotMetadata"], sources)
    diagnostics = _document(evidence["diagnostics"], sources)
    if "screenshotObservation" in evidence:
        observation = _document(evidence["screenshotObservation"], sources)
        result = observation.get("nativeResult")
        if observation.get("status") != "captured" or not isinstance(result, dict) or result.get("status") != "completed" or result.get("gpuCompleted") is not True or result.get("metadata") != screenshot:
            raise ValueError("completed screenshot observation does not match unchanged native sidecar")
        if "submissionToken" in result and result["submissionToken"] != meta["submissionId"]:
            raise ValueError("screenshot completion wrapper submission mismatch")
    elif screenshot.get("gpuCompleted") is not True:
        raise EvidenceUnavailable("GPU completion requires final native metadata or raw screenshot observation")
    if "gpuCompleted" in screenshot and screenshot["gpuCompleted"] is not True:
        raise ValueError("native screenshot metadata contradicts GPU completion")
    if screenshot.get("frameId") != meta["frameId"]:
        raise ValueError("screenshot frame identity mismatch")
    if "encoding" in meta and (screenshot.get("rowOrigin") != meta["encoding"]["rowOrigin"] or
                               screenshot.get("colorSpace") != "srgb_encoded_rgb" or
                               meta["encoding"]["colorSpace"] != "srgb-encoded"):
        raise ValueError("native screenshot orientation/color encoding differs")
    if "payload" in diagnostics:
        if "ok" in diagnostics and diagnostics["ok"] is not True:
            raise ValueError("native diagnostics snapshot failed")
        payload = diagnostics["payload"]
        if not isinstance(payload, dict):
            raise ValueError("invalid native diagnostic payload")
        diagnostics = payload.get("diagnostics", {})
    rows = diagnostics.get("frames")
    if diagnostics.get("schemaVersion") != 2 or not isinstance(rows, list):
        raise EvidenceUnavailable("native schema-2 diagnostic frame ring unavailable")
    row = select_screenshot_frame(screenshot, rows)
    if row.get("schemaVersion") != 2 or row.get("measurement", {}).get("measurementSchemaVersion") != 2:
        raise ValueError("observed frame measurement schema mismatch")
    if type(row.get("submissionToken")) is not int or row["submissionToken"] <= 0 or row["submissionToken"] != meta["submissionId"]:
        raise ValueError("observed diagnostic submission token mismatch")
    if not isinstance(row.get("status"), dict) or row["status"].get("ok") is not True:
        raise ValueError("observed diagnostic frame status unsuccessful")
    measurement = row.get("measurement", {})
    for key in ORBIT_FIELDS:
        if type(measurement.get(key)) not in (int, float) or measurement[key] != camera[key]:
            raise ValueError("observed same-frame orbit mismatch: " + key)
    if measurement.get("externalCamera") not in (False, 0):
        raise ValueError("observed frame uses external camera with unavailable orientation")
    if screenshot.get("width") != meta["width"] or screenshot.get("height") != meta["height"] or row.get("renderWidth") != meta["width"] or row.get("renderHeight") != meta["height"]:
        raise ValueError("same-frame screenshot/render dimensions mismatch")
    for key in ("contentHash", "buildIdentity"):
        if key == "buildIdentity" and screenshot.get(key) is None and row.get(key) is None:
            build = _external_build_binding(evidence, sources)
            if meta.get("sourceIdentity") != build.get("sourceBefore") or meta.get("shaderIdentity") != evidence["buildReceipt"]:
                raise ValueError("capture metadata source/Shader receipt differs from actual build")
            continue
        if not _hex(screenshot.get(key)) or not _hex(row.get(key)):
            raise EvidenceUnavailable("screenshot/diagnostic identity unavailable: " + key)
        if screenshot[key] != row.get(key):
            raise ValueError("screenshot/diagnostic identity mismatch: " + key)
    effective = row.get("effectiveConfig")
    if not isinstance(effective, dict) or any(not _hex(screenshot.get(key)) or screenshot[key] != effective.get(key) for key in ("configHash", "shaderIdentity")):
        raise ValueError("screenshot/diagnostic effective config/shader identity mismatch")
    if "manifestSha256" in meta and row.get("contentHash") != meta["manifestSha256"]:
        raise ValueError("screenshot content differs from original manifest")
    return row


def reference_readiness(row, threshold=0.1):
    """Prove physical consumption of the requested cut, independently of pixels.

    qualitySatisfied deliberately does not participate: establishing an original
    reference precedes the independent candidate pixel comparison.
    """
    reasons = []
    if type(threshold) not in (int, float) or not math.isfinite(threshold) or threshold <= 0:
        raise ValueError("readiness requires a finite positive requested detail threshold")
    p0 = row.get("diagnosticsP0")
    if not isinstance(p0, dict) or p0.get("schemaVersion") != 1:
        return {"status": "blocked", "reasons": ["native_detail_admission_extension_unavailable"]}
    identity = _object(p0.get("identity"))
    for key in ("frameId", "submissionToken", "sceneGeneration", "surfaceGeneration", "configGeneration", "pipelineGeneration"):
        expected = _object(row.get("measurement")).get(key) if key == "pipelineGeneration" else row.get(key)
        if type(expected) is not int or expected <= 0 or identity.get(key) != expected:
            reasons.append("native_identity_mismatch_" + key)
    if _object(row.get("status")).get("ok") is not True or row.get("renderWidth") != 1876 or row.get("renderHeight") != 1105:
        reasons.append("successful_original_size_submission_unavailable")
    resources = _object(p0.get("resources"))
    if resources.get("status") != "observed" or resources.get("aggregateLimitBytes") != 805306368:
        reasons.append("native_resources_or_unchanged_cap_unavailable")
    admission = _object(resources.get("admission"))
    if admission.get("status") != "admitted":
        reasons.append("native_admission_" + str(admission.get("status", "unavailable")))
    cut = _object(resources.get("cut"))
    if cut.get("rootReady") is not True or cut.get("loading") is not False or cut.get("requestPages") != 0:
        reasons.append("necessary_detail_loading_incomplete_or_unknown")
    error = cut.get("maxSelectedProxyErrorPixels")
    if type(error) not in (int, float) or not math.isfinite(error) or error < 0 or error > threshold:
        reasons.append("requested_detail_error_not_satisfied")
    pages, used = cut.get("selectedPageCount"), cut.get("usedSelectedPages")
    if type(pages) is not int or pages <= 0 or type(used) is not int or used != pages:
        reasons.append("selected_cut_successful_consumption_unavailable")
    if cut.get("consumptionSubmissionToken") != row.get("submissionToken") or type(cut.get("consumptionSubmissionToken")) is not int:
        reasons.append("selected_cut_consumption_token_unavailable")
    if cut.get("transitionAdmitted") is not True:
        reasons.append("necessary_transition_admission_unavailable")
    if type(cut.get("activeCount")) is not int or cut["activeCount"] <= 0:
        reasons.append("consumed_active_detail_unavailable")
    for key in ("selectionVersion", "residencyVersion"):
        if type(cut.get(key)) is not int or cut[key] <= 0:
            reasons.append("cut_identity_unavailable_" + key)
    return {"status": "ready" if not reasons else "blocked", "reasons": reasons,
            "identity": identity, "cut": cut, "admission": admission,
            "scope": "Physical same-submission detail readiness only; pixel quality remains unverified."}


def audit_originals(config, sources):
    """Inventory actual old image/sidecar bytes; a successful audit is no quality PASS."""
    if config.get("schemaVersion") != 1:
        raise ValueError("audit requires schemaVersion 1")
    directory = Path(config["qualityDirectory"]).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("qualityDirectory must be an existing directory")
    paths = []
    for path in directory.iterdir():
        if path.is_file() and (path.suffix == ".png" or path.name.endswith((".native.json", ".observation.json", ".diagnostics.json", ".private-copy-receipt.json"))):
            paths.append(path)
        if len(paths) > 512:
            break
    paths.sort()
    if len(paths) > 512:
        raise ValueError("quality audit exceeds 512-file bound")
    inventory, observations, images, native = [], [], [], []
    total = 0
    for path in paths:
        limit = 128 * 1024 * 1024 if path.suffix == ".png" else 16 * 1024 * 1024
        if path.stat().st_size > limit:
            raise ValueError("quality audit file bound exceeded: " + path.name)
        total += path.stat().st_size
        if total > 512 * 1024 * 1024:
            raise ValueError("quality audit exceeds 512 MiB total bound")
        data = _read(path, sources)
        inventory.append(dict(sources[-1]))
        if path.suffix == ".png":
            _, Image, _ = _libraries()
            with Image.open(io.BytesIO(data)) as image:
                if image.format != "PNG" or image.width * image.height > 16384 * 16384:
                    raise ValueError("invalid or oversized original PNG")
                image.verify()
                images.append({"path": str(path), "width": image.width, "height": image.height, "mode": image.mode})
        elif path.name.endswith(".native.json"):
            value = json.loads(data)
            if not isinstance(value, dict):
                raise ValueError("native sidecar must be a JSON object")
            frame = value.get("viewerFrame")
            native.append({"path": str(path), "frameId": value.get("frameId"),
                           "submissionToken": value.get("submissionToken"),
                           "physicalReadiness": reference_readiness(frame) if isinstance(frame, dict) else
                           {"status": "blocked", "reasons": ["same_submission_viewer_frame_unavailable"]}})
        if path.name.endswith(".observation.json"):
            value = json.loads(data)
            if not isinstance(value, dict):
                raise ValueError("observation sidecar must be a JSON object")
            pose = path.name[:-len(".observation.json")]
            observations.append({"poseId": pose, "status": value.get("status", "unknown"),
                                 "reason": value.get("reason"),
                                 "pngExists": (directory / (pose + ".png")).is_file(),
                                 "nativeSidecarExists": (directory / (pose + ".native.json")).is_file()})
    counts = {"png": sum(p.suffix == ".png" for p in paths),
              "nativeSidecar": sum(p.name.endswith(".native.json") for p in paths),
              "observation": len(observations)}
    return {"qualityDirectory": str(directory), "inventory": inventory, "counts": counts,
            "observations": observations, "images": images, "native": native, "qualityStatus": "blocked",
            "reasons": ["Original same-frame physical loading proof and inspected fixed ROI registration are not established by a file inventory."],
            "nextActions": ["Repair a fixed private exports parent if screenshot export failed.",
                            "Use bounded unchanged-original quality replay; inspect native admission and successful selected-cut consumption.",
                            "Inspect actual captured image before defining piano/bicycle ROI coordinates; freeze before any candidate."]}


def _metadata(meta, policy, pose, sources, reference, image_identity=None):
    required = {"schemaVersion", "modelSha256", "manifestSha256", "qualitySettings", "cameraIdentity",
                "poseId", "width", "height", "encoding", "successfulSubmission", "capturePurpose",
                "frameId", "submissionId", "sourceIdentity", "configIdentity", "shaderIdentity",
                "observerCost", "rawEvidence", "originalModel", "adequatelyLoaded"}
    if not required.issubset(meta) or meta["schemaVersion"] != 1:
        raise ValueError("incomplete capture metadata")
    for key in ("modelSha256", "manifestSha256", "qualitySettings", "encoding"):
        if meta[key] != policy[key]:
            raise ValueError("capture quality identity mismatch: " + key)
    if meta["cameraIdentity"] != pose["cameraIdentity"] or meta["poseId"] != pose["id"]:
        raise ValueError("camera/pose identity mismatch")
    if meta["width"] != policy["qualitySettings"]["renderWidth"] or meta["height"] != policy["qualitySettings"]["renderHeight"]:
        raise ValueError("capture dimensions differ")
    if meta["successfulSubmission"] is not True or meta["originalModel"] is not True or meta["capturePurpose"] != "quality-replay":
        raise EvidenceUnavailable("real original-model successful quality replay required")
    if reference and meta["adequatelyLoaded"] is not True:
        raise EvidenceUnavailable("original reference is not adequately loaded")
    for key in ("frameId", "submissionId"):
        if type(meta[key]) is not int or meta[key] <= 0:
            raise ValueError("missing successful frame/submission identity")
    if not isinstance(meta["observerCost"], dict) or not meta["observerCost"]:
        raise ValueError("readback/encoding observer cost disclosure required")
    if not isinstance(meta["rawEvidence"], list) or not meta["rawEvidence"]:
        raise EvidenceUnavailable("raw loaded/submission evidence unavailable")
    for key in ("sourceIdentity", "configIdentity", "shaderIdentity"):
        _identity(meta[key], sources)
    for entry in meta["rawEvidence"]:
        _identity(entry, sources)
    if pose["cameraIdentity"].get("representation") == "observed-orbit-v1":
        if image_identity is not None and _object(meta.get("frameObservation")).get("screenshotImage") != image_identity:
            raise ValueError("registered image differs from screenshot evidence identity")
        row = _observed_frame(meta, pose["cameraIdentity"], sources)
        binding = _object(pose.get("nativeQualityBinding"))
        expected = {"contentHash": row.get("contentHash"),
                    "configHash": row.get("effectiveConfig", {}).get("configHash"),
                    "shaderIdentity": row.get("effectiveConfig", {}).get("shaderIdentity"),
                    "presentation": {key: row.get(key) for key in PRESENTATION_FIELDS}}
        if binding != expected:
            raise ValueError("native source/config/Shader/presentation differs from frozen original binding")
        effective = row.get("effectiveConfig", {})
        if effective.get("displaySHDegree") != 3 or effective.get("shStorageMode") != 0 or effective.get("shStorageDegree") not in (-1, 3) or effective.get("projectedFusion") is not False or effective.get("projectionColorCompression") is not False:
            raise ValueError("native original SH3/FP32/fusion/color invariants differ")
        if effective.get("qualityPixels") != policy["qualitySettings"]["lodErrorThreshold"]:
            raise ValueError("native LOD detail threshold differs from frozen original")
        if reference:
            readiness = reference_readiness(row, policy["qualitySettings"]["lodErrorThreshold"])
            if readiness["status"] != "ready":
                raise EvidenceUnavailable("original physical readiness blocked: " + ",".join(readiness["reasons"]))
    elif reference:
        raise EvidenceUnavailable("original reference requires native same-frame observed camera and detail/admission evidence")


def _image(entry, policy, sources):
    np, Image, _ = _libraries()
    data = _identity(entry, sources)
    width = policy["qualitySettings"]["renderWidth"]
    height = policy["qualitySettings"]["renderHeight"]
    enc = policy["encoding"]
    if enc["format"] == "raw-rgba8":
        if len(data) != width * height * 4:
            raise ValueError("raw RGBA byte count mismatch")
        array = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
    else:
        with Image.open(io.BytesIO(data)) as image:
            expected = "RGB" if enc["format"] == "png-rgb8" else "RGBA"
            if image.format != "PNG" or image.mode != expected or image.size != (width, height):
                raise ValueError("PNG mode/dimensions mismatch; conversion forbidden")
            array = np.asarray(image)
    array = array.astype(np.float64) / 255.0
    if enc["rowOrigin"] == "bottom_left":
        array = array[::-1]
    rgb = array[:, :, :3]
    if array.shape[2] == 4:
        alpha = array[:, :, 3:4]
        if enc["alpha"] == "opaque" and not np.all(alpha == 1):
            raise ValueError("nonopaque alpha in opaque policy")
        if enc["alpha"] == "straight-over-black":
            rgb = rgb * alpha
    return rgb


def _validate_policy(policy):
    if policy.get("schemaVersion") != 1 or policy.get("manifestSha256") != MANIFEST_SHA256 or not _hex(policy.get("modelSha256")):
        raise ValueError("wrong schema or original model/manifest identity")
    for key, digest_key in (("modelIdentity", "modelSha256"), ("manifestIdentity", "manifestSha256")):
        if not isinstance(policy.get(key), dict) or policy[key].get("sha256") != policy[digest_key]:
            raise EvidenceUnavailable("original asset identity unavailable: " + key)
    _settings(policy.get("qualitySettings"))
    _encoding(policy.get("encoding"))
    if policy.get("thresholds") != THRESHOLDS or policy.get("algorithm") != ALGORITHM:
        raise ValueError("thresholds/metric implementation cannot change")
    poses = policy.get("poses")
    if not isinstance(poses, list) or not poses:
        raise EvidenceUnavailable("no original same-pose references")
    ids = set()
    for pose in poses:
        if not isinstance(pose, dict) or not isinstance(pose.get("reference"), dict) or not isinstance(pose.get("metadata"), dict):
            raise EvidenceUnavailable("original reference image/metadata unavailable")
        if not isinstance(pose.get("id"), str) or not pose["id"] or pose["id"] in ids:
            raise ValueError("empty/duplicate pose id")
        ids.add(pose["id"])
        camera = pose.get("cameraIdentity")
        _camera(camera)
        binding = _object(pose.get("nativeQualityBinding"))
        if any(not _hex(binding.get(key)) for key in ("contentHash", "configHash", "shaderIdentity")) or binding.get("contentHash") != policy["manifestSha256"]:
            raise EvidenceUnavailable("original native content/config/Shader binding unavailable")
        if set(_object(binding.get("presentation"))) != set(PRESENTATION_FIELDS) or any(binding["presentation"][key] is None for key in PRESENTATION_FIELDS):
            raise EvidenceUnavailable("original native presentation binding incomplete")
        inspection = _object(pose.get("roiInspection"))
        if inspection.get("imageSha256") != pose["reference"].get("sha256") or inspection.get("inspectedBeforeCandidate") is not True:
            raise EvidenceUnavailable("actual reference image inspection must precede fixed ROI registration")
        features = _object(inspection.get("features"))
        if any(features.get(key) not in ("present", "absent") for key in ("piano_keys", "bicycle_thin_structures")):
            raise EvidenceUnavailable("piano keys and bicycle thin structures must be inspected, not assigned invented coordinates")
        rois = pose.get("rois")
        if not isinstance(rois, list) or not rois:
            raise EvidenceUnavailable("fixed detail ROI evidence unavailable")
        names = set()
        for roi in rois:
            if not isinstance(roi.get("id"), str) or not roi["id"] or roi["id"] in names:
                raise ValueError("empty/duplicate ROI id")
            names.add(roi["id"])
            bounds = roi.get("xywh")
            if not isinstance(bounds, list) or len(bounds) != 4 or any(type(v) is not int for v in bounds):
                raise ValueError("ROI needs integer xywh")
            x, y, w, h = bounds
            if x < 0 or y < 0 or min(w, h) < 11 or x + w > policy["qualitySettings"]["renderWidth"] or y + h > policy["qualitySettings"]["renderHeight"]:
                raise ValueError("ROI outside image or smaller than SSIM window")
        for feature, presence in features.items():
            if presence == "present" and not any(roi.get("feature") == feature for roi in rois):
                raise EvidenceUnavailable("inspected visible feature lacks fixed ROI: " + feature)


def freeze(config, sources):
    """Validate and bind original references, metadata, metrics and ROI policy."""
    policy = dict(config)
    for key, value in (("thresholds", THRESHOLDS), ("algorithm", ALGORITHM)):
        if key in policy and policy[key] != value:
            raise ValueError("attempt to override fixed " + key)
        policy[key] = value
    _validate_policy(policy)
    for key in ("modelIdentity", "manifestIdentity"):
        _verify_large_identity(policy[key], sources)
    _, _, versions = _libraries()
    policy["toolVersions"] = versions
    for pose in policy["poses"]:
        meta = json.loads(_identity(pose["metadata"], sources))
        _metadata(meta, policy, pose, sources, True, pose["reference"])
        _image(pose["reference"], policy, sources)
    return {"policy": policy, "policySha256": _sha(_json_bytes(policy)),
            "registration": "Freeze report digest must be recorded before candidate capture and supplied unchanged."}


def _metrics(a, b):
    np, _, _ = _libraries()
    def mean11(value):
        integral = np.pad(value, ((1, 0), (1, 0), (0, 0))).cumsum(0).cumsum(1)
        return (integral[11:, 11:] - integral[:-11, 11:] - integral[11:, :-11] + integral[:-11, :-11]) / 121
    delta = a - b
    mse = float(np.mean(delta * delta))
    mae = float(np.mean(np.abs(delta)))
    ma, mb = mean11(a), mean11(b)
    va, vb = np.maximum(0, mean11(a * a) - ma * ma), np.maximum(0, mean11(b * b) - mb * mb)
    cov = mean11(a * b) - ma * mb
    ssim = float(np.mean(((2 * ma * mb + 0.0001) * (2 * cov + 0.0009)) /
                         ((ma * ma + mb * mb + 0.0001) * (va + vb + 0.0009))))
    psnr = -10 * math.log10(mse) if mse else None
    return {"psnrDb": psnr, "identical": mse == 0, "ssim": ssim, "mae": mae,
            "pass": (psnr is None or psnr >= 45) and ssim >= 0.995 and mae <= 1 / 255}


def compare(frozen, candidate, sources, immutable_reference=False):
    """Check frozen identities and every full image and registered detail ROI."""
    if frozen.get("status") != "pass":
        raise EvidenceUnavailable("policy registration did not pass")
    policy = frozen["policy"]
    _validate_policy(policy)
    for key in ("modelIdentity", "manifestIdentity"):
        _verify_large_identity(policy[key], sources)
    digest = _sha(_json_bytes(policy))
    if frozen.get("policySha256") != digest or candidate.get("registeredPolicySha256") != digest:
        raise ValueError("mutable/unregistered policy digest")
    _, _, versions = _libraries()
    if versions != policy.get("toolVersions"):
        raise EvidenceUnavailable("metric tool versions differ from preregistration")
    captures = candidate.get("poses")
    if not isinstance(captures, list) or len(captures) != len(policy["poses"]):
        raise EvidenceUnavailable("missing registered candidate poses")
    by_id = {entry["id"]: entry for entry in captures}
    if len(by_id) != len(captures) or set(by_id) != {p["id"] for p in policy["poses"]}:
        raise ValueError("duplicate/unregistered candidate pose")
    results = []
    for pose in policy["poses"]:
        reference_meta = _document(pose["metadata"], sources)
        if immutable_reference:
            # The pinned successful freeze already checked original source and
            # package bytes. Their working paths may since have been replaced.
            # Recheck archived raw evidence, never substitute today's APK/source.
            for item in reference_meta["rawEvidence"] + [reference_meta[k] for k in
                    ("sourceIdentity", "configIdentity", "shaderIdentity")]:
                _identity(item, sources)
            archived = reference_meta["frameObservation"]
            capture = _document(archived["captureReceipt"], sources)
            build = _document(archived["buildReceipt"], sources)
            before = _document(build["sourceBefore"], sources)
            after = _document(build["sourceAfter"], sources)
            command = _document(build["buildCommand"], sources)
            if (before.get("files") != after.get("files") or command.get("exitCode") != 0 or
                    _object(build.get("apk")).get("sha256") != capture.get("apkSha256") or
                    reference_meta["sourceIdentity"] != build["sourceBefore"]):
                raise ValueError("archived reference source/package receipt conflicts")
            _capture_package_members(capture, build)
            root = Path(archived["captureReceipt"]["path"]).resolve().parent
            for name in ("diagnostics", "screenshotMetadata", "screenshotImage", "screenshotObservation"):
                expected = archived[name]
                matches = [item for item in capture.get("rawEvidence", []) if
                    (root / item["path"]).resolve() == Path(expected["path"]).resolve() and
                    item.get("bytes") == expected["bytes"] and item.get("sha256") == expected["sha256"]]
                if len(matches) != 1:
                    raise ValueError("archived capture raw identity mismatch: " + name)
            sidecar = _document(archived["screenshotMetadata"], sources)
            raw = _document(archived["diagnostics"], sources)
            payload = raw.get("payload", raw)
            row = select_screenshot_frame(sidecar, payload.get("diagnostics", payload)["frames"])
            if row.get("frameId") != 1240 or row.get("submissionToken") != 1242 or reference_readiness(
                    row, policy["qualitySettings"]["lodErrorThreshold"])["status"] != "ready":
                raise ValueError("immutable reference successful frame/readiness differs")
        else:
            _metadata(reference_meta, policy, pose, sources, True, pose["reference"])
        entry = by_id[pose["id"]]
        meta = _document(entry["metadata"], sources)
        if (Path(entry["image"]["path"]).resolve() == Path(pose["reference"]["path"]).resolve() or
                (meta.get("frameId"), meta.get("submissionId"),
                 _object(meta.get("frameObservation")).get("captureReceipt")) ==
                (reference_meta.get("frameId"), reference_meta.get("submissionId"),
                 _object(reference_meta.get("frameObservation")).get("captureReceipt"))):
            raise ValueError("candidate must be a separately captured frame, not the frozen reference")
        _metadata(meta, policy, pose, sources, False, entry["image"])
        if immutable_reference:
            candidate_sidecar = _document(meta["frameObservation"]["screenshotMetadata"], sources)
            if candidate_sidecar.get("buildIdentity") is not None:
                build = _external_build_binding(meta["frameObservation"], sources)
                if meta.get("sourceIdentity") != build.get("sourceBefore") or meta.get("shaderIdentity") != meta["frameObservation"]["buildReceipt"]:
                    raise ValueError("candidate external source/Shader binding differs")
        a = _image(pose["reference"], policy, sources)
        b = _image(entry["image"], policy, sources)
        row = {"poseId": pose["id"], "fullImage": _metrics(a, b), "rois": []}
        for roi in pose["rois"]:
            x, y, w, h = roi["xywh"]
            row["rois"].append({"id": roi["id"], **_metrics(a[y:y+h, x:x+w], b[y:y+h, x:x+w])})
        row["pass"] = row["fullImage"]["pass"] and all(r["pass"] for r in row["rois"])
        results.append(row)
    return {"policySha256": digest, "poses": results,
            "status": "pass" if all(row["pass"] for row in results) else "fail"}


def comparison_report(candidate_entry, policy_entry):
    """Recompute the complete deterministic report from immutable raw inputs.

    This round supports only the hash-pinned stationary reference. Both the CLI
    and budget consumer use this function; no supplied metric is trusted.
    """
    sources = []
    report = {"schemaVersion": 1, "status": "pass", "reasons": [], "sourceFiles": sources,
              "limitations": LIMITATIONS, "command": "compare"}
    candidate = _document(candidate_entry, sources)
    frozen = _document(policy_entry, sources)
    if policy_entry["sha256"] != STATIONARY_FREEZE_SHA256:
        raise EvidenceUnavailable("unsupported freeze: only immutable stationary-end v2 is registered")
    if [p.get("id") for p in frozen["policy"]["poses"]] != ["stationary-end"]:
        raise ValueError("stationary freeze pose differs")
    report["inputBinding"] = {"candidate": candidate_entry, "freeze": policy_entry,
                              "consumerSha256": _sha(Path(__file__).read_bytes())}
    report["referenceArchiveScope"] = {
        "status": "exact_successful_freeze_attested_raw_evidence_reverified",
        "freezeSha256": STATIONARY_FREEZE_SHA256,
        "historicalSourceAndApk": "freeze attested; mutable original build/source paths are not reopened or relabeled as current bytes",
        "scope": "stationary-end frame1240/token1242 only"}
    report.update(compare(frozen, candidate, sources, immutable_reference=True))
    if report["status"] == "fail":
        report["reasons"].append("one or more full-image or fixed-ROI thresholds failed")
    return report


def main(argv=None):
    """Write a non-overwriting evidence report; never turn missing proof into pass."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("audit", "freeze", "compare"))
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--policy-sha256", help="externally recorded SHA256 of the freeze report bytes")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if args.command == "compare" and (args.policy is None or not _hex(args.policy_sha256)):
        parser.error("compare requires --policy and --policy-sha256 recorded before candidate capture")
    sources = []
    report = {"schemaVersion": 1, "status": "pass", "reasons": [], "sourceFiles": sources,
              "limitations": LIMITATIONS, "command": args.command}
    try:
        config = _load(args.input, sources)
        if args.command == "audit":
            report.update(audit_originals(config, sources))
        elif args.command == "freeze":
            report.update(freeze(config, sources))
        else:
            frozen = _load(args.policy, sources)
            if sources[-1]["sha256"] != args.policy_sha256:
                raise ValueError("frozen report bytes changed against external preregistration digest")
            if args.policy_sha256 == STATIONARY_FREEZE_SHA256:
                candidate_entry, policy_entry = dict(sources[0]), dict(sources[-1])
                report = comparison_report(candidate_entry, policy_entry)
            else:
                report.update(compare(frozen, config, sources))
        if report["status"] == "fail" and not report["reasons"]:
            report["reasons"].append("one or more full-image or fixed-ROI thresholds failed")
    except (EvidenceUnavailable, FileNotFoundError) as error:
        report.update(status="blocked", reasons=[str(error)])
    except (ValueError, KeyError, TypeError, OSError, OverflowError, zipfile.BadZipFile) as error:
        report.update(status="fail", reasons=[str(error)])
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(_json_bytes(report))
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        print("cannot create output (overwrite forbidden): " + str(error), file=sys.stderr)
        return 2
    print(json.dumps({"status": report["status"], "output": str(args.output), "reasons": report["reasons"]}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
