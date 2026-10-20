"""Immutable preregistration and full-image Brush/MonsterEngine comparisons."""

import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re

import numpy as np
import PIL
from PIL import Image, ImageDraw


GATES = {"psnrMin": 30.0, "ssimMin": 0.95, "maeMax": 4/255,
         "centerErrorPixelsMax": 0.25, "cpuAbsTolerance": 1e-5,
         "cpuRelTolerance": 1e-5, "syntheticRgbAbsMax": 2/255}
HELDOUT_RESIZE = {
    "scope": "held-out context only; engine and Brush reference never resized",
    "implementation": "image-0.25.8-Triangle-u8-v1",
    "dimensions": "source/evaluation sizes from cameraSourceIdentity; fit max(target width,height) square; f64 positive half-up dimensions; no upscaling",
    "filter": "Triangle support 1; pixel-center coordinates; support scaled by max(1,source/target); clipped normalized weights",
    "arithmetic": "separable vertical then horizontal; sequential f32 weights and accumulation; float intermediate; final clamp[0,255] then half-away-from-zero u8 round",
    "decode": "Pillow registered version RGB/RGBA uint8; resize channels before existing fixed black composite; no transfer conversion"
}
METRICS = {"implementation": "brush_render_comparison-v1", "numpyVersion": np.__version__,
           "pillowVersion": PIL.__version__, "dataRange": 1,
           "domain": "full-image RGB, equally weighted channels and pixels",
           "psnr": "-10*log10(mean((a-b)^2));positive infinity iff finite MSE exactly zero",
           "mae": "mean(abs(a-b))",
           "ssim": {"window": "11x11 uniform valid (no padding)", "covariance": "population (divide by 121)",
                    "aggregation": "mean all valid centers and RGB channels", "C1": 0.01**2,
                    "C2": 0.03**2, "formula": "((2*muA*muB+C1)*(2*covAB+C2))/((muA^2+muB^2+C1)*(varA+varB+C2))"}}


def _bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n").encode("utf-8")


def _write(path, data):
    with Path(path).open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _hex(value):
    return isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value) is not None


def _finite(value):
    if isinstance(value, dict):
        return all(_finite(v) for v in value.values())
    if isinstance(value, list):
        return all(_finite(v) for v in value)
    if isinstance(value, float):
        return math.isfinite(value)
    return True


def _identity(entry):
    if not isinstance(entry, dict) or not _hex(entry.get("sha256")):
        raise ValueError("missing file identity")
    path = Path(entry["path"])
    if type(entry.get("bytes")) is not int or entry["bytes"] <= 0 or not path.is_file():
        raise ValueError("missing or invalid input file: "+str(path))
    data = path.read_bytes()
    if len(data) != entry["bytes"] or _hash(data) != entry["sha256"]:
        raise ValueError("file identity mismatch: "+str(path))
    return data


def _view_ids(views):
    if not isinstance(views, list) or len(views) != 6:
        raise ValueError("exactly six views required")
    ids = [v["id"] for v in views]
    if any(not isinstance(v, str) or not v or v != Path(v).name for v in ids) or len(set(ids)) != 6:
        raise ValueError("missing, unsafe or duplicate view id")
    frames = []
    for ident in ids:
        numbers = re.findall(r"\d+", ident)
        if len(numbers) != 1:
            raise ValueError("view id must identify one held-out frame")
        frames.append(int(numbers[0]))
    if set(frames) != {1, 9, 17, 25, 33, 41}:
        raise ValueError("unknown or missing held-out frames")
    return ids


def _validate(policy):
    if policy.get("schemaVersion") != 1 or type(policy.get("schemaVersion")) is not int:
        raise ValueError("unknown policy schema")
    required = {"schemaVersion", "runId", "views", "protectedInputsSHA256", "auditSHA256",
                "commonEncoding", "backgroundRGB", "rowOrigin", "sourceIdentities", "buildIdentity",
                "deviceIdentity", "fixtures", "gates", "metrics", "captureTimeoutSeconds", "composite",
                "quantization", "deviceBuildEvidenceRules", "changeReason", "heldOutResize"}
    if set(policy) != required or not _finite(policy):
        raise ValueError("unknown/missing policy fields or nonfinite values")
    if not isinstance(policy["runId"], str) or not policy["runId"].strip():
        raise ValueError("runId required")
    if not isinstance(policy["changeReason"], str) or not policy["changeReason"].strip():
        raise ValueError("explicit initial/changed-run reason required")
    if policy["gates"] != GATES or policy["metrics"] != METRICS or policy["captureTimeoutSeconds"] != 120:
        raise ValueError("binding gates/metrics/budget may not be changed")
    if policy["heldOutResize"] != HELDOUT_RESIZE:
        raise ValueError("unsupported held-out resize contract")
    if policy["commonEncoding"] not in ("trained-image-encoded-rgb", "linear-rgb"):
        raise ValueError("unsupported audited RGB encoding")
    if policy["backgroundRGB"] != [0.0, 0.0, 0.0] or policy["rowOrigin"] != "top_left":
        raise ValueError("only audited over-black top-left comparison supported")
    for key in ("protectedInputsSHA256", "auditSHA256"):
        if not _hex(policy[key]):
            raise ValueError("missing audited identity: "+key)
    for key in ("buildIdentity", "deviceIdentity", "sourceIdentities"):
        if not isinstance(policy[key], dict) or not policy[key]:
            raise ValueError("missing identity evidence: "+key)
    for entry in policy["sourceIdentities"].values():
        _identity(entry)
    build = policy["buildIdentity"]
    _identity(build["executable"])
    if not isinstance(build.get("shaders"), list) or not build["shaders"]:
        raise ValueError("shader identities required")
    for shader in build["shaders"]:
        _identity(shader)
    for key in ("gpu", "driver"):
        if not isinstance(policy["deviceIdentity"].get(key), str) or not policy["deviceIdentity"][key]:
            raise ValueError("actual GPU and driver required")
    _view_ids(policy["views"])
    for view in policy["views"]:
        for size in ("width", "height"):
            if type(view.get(size)) is not int or not 11 <= view[size] <= 16384:
                raise ValueError("invalid comparison dimensions")
        for key in ("reference", "heldOut"):
            _identity(view[key])
        for key in ("fx", "fy", "cx", "cy", "nearPlane", "farPlane", "viewMatrix", "cameraSourceIdentity"):
            if key not in view:
                raise ValueError("incomplete audited view")
    fixtures = policy["fixtures"]
    if fixtures.get("schemaVersion") != 1 or len(fixtures.get("fixtures", [])) != 8:
        raise ValueError("all eight preregistered synthetic views required")
    ids = set()
    for fixture in fixtures["fixtures"]:
        if fixture["id"] in ids:
            raise ValueError("duplicate synthetic fixture")
        ids.add(fixture["id"])
        data = _identity(fixture["fixtureIdentity"])
        expected = copy.deepcopy(fixture)
        expected.pop("fixtureIdentity")
        if json.loads(data) != expected:
            raise ValueError("synthetic fixture registration mismatch")
        _identity(fixture["sourcePLY"])
        _identity(fixture["analytic-linearReference"])
        _identity(fixture["analytic-displayReference"])


def register_policy(config, output_directory):
    """Validate and durably preregister exact bytes once; refuse all overwrites.

    Config supplies the audited identity/camera/color/device/build fields. Fixed
    gates and metric definitions cannot be overridden even with stricter values.
    """
    policy = copy.deepcopy(config)
    fixed = {"schemaVersion": 1, "gates": GATES, "metrics": METRICS, "captureTimeoutSeconds": 120,
             "heldOutResize": HELDOUT_RESIZE,
             "rowOrigin": "top_left", "backgroundRGB": [0.0, 0.0, 0.0],
             "composite": "native .rgba RGB already accumulated over fixed black, alpha not reapplied; image RGBA straight over fixed black; opaque RGB unchanged; no registration/crop/exposure fit",
             "quantization": "input uint8/255 float64; no decode/encode for trained-image-encoded-rgb; round-to-nearest uint8 artifacts",
             "deviceBuildEvidenceRules": "exact registered build/shader/source/input identities; actual GPU/driver exact match; real submission completion; native RGBA8 readback"}
    for key, value in fixed.items():
        if key in policy and policy[key] != value:
            raise ValueError("attempt to alter fixed policy field: "+key)
        policy[key] = copy.deepcopy(value)
    _validate(policy)
    root = Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=False)
    data = _bytes(policy)
    _write(root/"comparison-policy.json", data)
    digest = _hash(data)
    _write(root/"comparison-policy.sha256", (digest+"\n").encode("ascii"))
    return load_policy(str(root/"comparison-policy.json"), digest)


def load_policy(path, expected_sha256):
    """Require the durable policy, exact SHA256 sidecar and unchanged identities."""
    path = Path(path).resolve()
    data = path.read_bytes()
    if not _hex(expected_sha256) or _hash(data) != expected_sha256:
        raise ValueError("policy hash mismatch")
    if path.with_suffix(".sha256").read_text(encoding="ascii").strip() != expected_sha256:
        raise ValueError("policy sidecar mismatch")
    policy = json.loads(data)
    _validate(policy)
    return {**policy, "_registration": {"path": str(path), "sha256": expected_sha256}}


def _triangle_u8(array, width, height):
    """Port image 0.25.8 sample.rs; no Pillow resampling or intermediate u8."""
    source_height, source_width = array.shape[:2]
    if (source_width, source_height) == (width, height):
        return array
    current = array.astype(np.float32)
    for axis, size in ((0, height), (1, width)):
        length = current.shape[axis]
        ratio = np.float32(length) / np.float32(size)
        support = max(np.float32(1), ratio)
        shape = list(current.shape)
        shape[axis] = size
        output = np.empty(shape, dtype=np.float32)
        for index in range(size):
            center = (np.float32(index) + np.float32(.5)) * ratio
            left = min(max(math.floor(center-support), 0), length-1)
            right = min(max(math.ceil(center+support), left+1), length)
            center -= np.float32(.5)
            weights = []
            total = np.float32(0)
            for sample in range(left, right):
                distance = abs((np.float32(sample)-center)/support)
                weight = np.float32(1)-distance if distance < 1 else np.float32(0)
                weights.append(weight)
                total += weight
            value = np.zeros(shape[1:] if axis == 0 else (shape[0], shape[2]), dtype=np.float32)
            for sample, weight in enumerate(weights, left):
                value += np.take(current, sample, axis=axis) * (weight/total)
            if axis == 0:
                output[index] = value
            else:
                output[:, index] = value
        current = output
    # Rust f32::round is half away from zero, unlike numpy.rint.
    # Promote only after f32 filtering to avoid rounding 0.49999 + .5 early.
    return np.floor(np.clip(current, 0, 255).astype(np.float64)+.5).astype(np.uint8)


def _image(entry, width, height, default_encoding, background, context_source=None):
    data = _identity(entry)
    path = Path(entry["path"])
    if path.suffix.lower() == ".rgba":
        if len(data) != width*height*4:
            raise ValueError("native RGBA length mismatch")
        arr = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4).astype(np.float64)/255
        # splat_render.comp writes vec4(C, 1-T): C already includes opacity
        # accumulation over black, exactly as in the native companion PPM.
        arr = arr[:, :, :3]
    else:
        with Image.open(path) as image:
            if image.mode not in ("RGB", "RGBA"):
                raise ValueError("image dimensions or mode mismatch")
            pixels = np.asarray(image)
            if image.size != (width, height):
                if not isinstance(context_source, dict):
                    raise ValueError("image dimensions or mode mismatch")
                source = context_source.get("sourceSize")
                target = context_source.get("evaluationSize")
                maximum = max(width, height)
                ratio = min(maximum/image.width, maximum/image.height)
                fitted = [max(1, math.floor(image.width*ratio+.5)),
                          max(1, math.floor(image.height*ratio+.5))]
                if (source != list(image.size) or target != [width, height]
                        or fitted != target or ratio >= 1):
                    raise ValueError("held-out dimensions differ from audited source resize")
                pixels = _triangle_u8(pixels, width, height)
            arr = pixels.astype(np.float64)/255
    origin = entry.get("rowOrigin", "top_left")
    if origin == "bottom_left":
        arr = arr[::-1]
    elif origin != "top_left":
        raise ValueError("unknown image row origin")
    if entry.get("encoding", default_encoding) != default_encoding:
        raise ValueError("image encoding differs from audited common encoding")
    if arr.shape[2] == 4:
        arr = arr[:, :, :3]*arr[:, :, 3:4] + np.array(background)*(1-arr[:, :, 3:4])
    return arr


def _mean_window(array):
    # Summed area tables compute every valid uniform 11x11 window in float64.
    summed = np.pad(array, ((1, 0), (1, 0), (0, 0))).cumsum(0).cumsum(1)
    return (summed[11:, 11:]-summed[:-11, 11:]-summed[11:, :-11]+summed[:-11, :-11])/121


def rgb_metrics(a, b):
    """Compute registered full-image RGB metrics, rejecting nonfinite operands."""
    if a.shape != b.shape or a.ndim != 3 or a.shape[2] != 3 or min(a.shape[:2]) < 11:
        raise ValueError("metric image shape mismatch")
    if not np.isfinite(a).all() or not np.isfinite(b).all() or min(a.min(), b.min()) < 0 or max(a.max(), b.max()) > 1:
        raise ValueError("invalid metric operands")
    delta = a-b
    mse = float(np.mean(delta*delta))
    mae = float(np.mean(np.abs(delta)))
    ma, mb = _mean_window(a), _mean_window(b)
    va = np.maximum(0, _mean_window(a*a)-ma*ma)
    vb = np.maximum(0, _mean_window(b*b)-mb*mb)
    cov = _mean_window(a*b)-ma*mb
    c1, c2 = 0.01**2, 0.03**2
    ssim = float(np.mean(((2*ma*mb+c1)*(2*cov+c2))/((ma*ma+mb*mb+c1)*(va+vb+c2))))
    psnr = math.inf if mse == 0 else -10*math.log10(mse)
    if not all(math.isfinite(v) for v in (mse, mae, ssim)) or (not math.isfinite(psnr) and mse != 0):
        raise ValueError("nonfinite derived metric")
    return {"mse": mse, "mae": mae, "ssim": ssim,
            "psnr": "Infinity" if mse == 0 else psnr, "exactMatch": mse == 0}


def _save_image(path, array):
    image = Image.fromarray(np.rint(np.clip(array, 0, 1)*255).astype(np.uint8), "RGB")
    with Path(path).open("xb") as stream:
        image.save(stream, format="PNG")
        stream.flush()
        os.fsync(stream.fileno())


def compare_registered(policy, capture_manifest, output_directory):
    """Bind all six actual captures before any score/difference artifact exists.

    Manifest views are identity entries plus id/width/height/rowOrigin/encoding/
    completionStatus. Completion must equal 'complete'; top-level bindings must
    exactly equal the registration, including actual runtime device evidence.
    """
    registration = policy.get("_registration", {})
    durable = load_policy(registration["path"], registration["sha256"])
    if durable != policy:
        raise ValueError("in-memory policy changed after preregistration")
    manifest = capture_manifest
    if manifest.get("schemaVersion") != 1 or manifest.get("policySHA256") != registration["sha256"] or not _finite(manifest):
        raise ValueError("capture schema/policy mismatch")
    for key in ("auditSHA256", "protectedInputsSHA256", "buildIdentity", "deviceIdentity"):
        if manifest.get(key) != policy[key]:
            raise ValueError("capture binding mismatch: "+key)
    if set(_view_ids(manifest.get("views"))) != set(_view_ids(policy["views"])):
        raise ValueError("capture view set mismatch")
    actual = {v["id"]: v for v in manifest["views"]}
    images = []
    # Validate/read every view before computing even the first metric.
    for view in policy["views"]:
        capture = actual[view["id"]]
        if capture.get("completionStatus") != "complete" or capture.get("width") != view["width"] or capture.get("height") != view["height"]:
            raise ValueError("incomplete capture or size mismatch")
        for key in ("rowOrigin", "encoding"):
            if key not in capture:
                raise ValueError("capture must explicitly declare "+key)
        for key in ("fx", "fy", "cx", "cy", "nearPlane", "farPlane", "viewMatrix", "shDegree"):
            if capture.get(key) != view.get(key):
                raise ValueError("capture calibrated camera mismatch: "+key)
        images.append((view["id"], *[_image(e, view["width"], view["height"], policy["commonEncoding"], policy["backgroundRGB"],
                                            view["cameraSourceIdentity"] if index == 2 else None)
                                    for index, e in enumerate((capture, view["reference"], view["heldOut"]))]))
    root = Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=False)
    results = []
    thumbnails = []
    for ident, engine, reference, heldout in images:
        metrics = rgb_metrics(engine, reference)
        passed = ((metrics["exactMatch"] or metrics["psnr"] >= GATES["psnrMin"])
                  and metrics["ssim"] >= GATES["ssimMin"] and metrics["mae"] <= GATES["maeMax"])
        artifacts = {}
        for name, array in (("engine", engine), ("reference", reference), ("held-out", heldout),
                            ("difference", np.abs(engine-reference)), ("difference-x8", np.abs(engine-reference)*8)):
            path = root/(ident+"-"+name+".png")
            _save_image(path, array)
            artifacts[name] = {"path": str(path), "sha256": _hash(path.read_bytes()), "bytes": path.stat().st_size}
        results.append({"id": ident, "passed": bool(passed), "engineVsBrush": metrics,
                        "engineVsHeldOut": rgb_metrics(engine, heldout), "brushVsHeldOut": rgb_metrics(reference, heldout),
                        "artifacts": artifacts})
        row = Image.new("RGB", (4*256, 212), "white")
        draw = ImageDraw.Draw(row)
        for index, name in enumerate(("engine", "reference", "held-out", "difference-x8")):
            with Image.open(artifacts[name]["path"]) as img:
                img.thumbnail((256, 192))
                row.paste(img, (index*256, 20))
            draw.text((index*256+3, 3), ident+" "+name, fill="black")
        thumbnails.append(row)
    sheet = Image.new("RGB", (1024, 212*6), "white")
    for index, row in enumerate(thumbnails):
        sheet.paste(row, (0, index*212))
    with (root/"contact-sheet.png").open("xb") as stream:
        sheet.save(stream, format="PNG")
        stream.flush()
        os.fsync(stream.fileno())
    aggregate = {key: float(np.mean([r["engineVsBrush"][key] for r in results])) for key in ("mae", "mse", "ssim")}
    aggregate["psnrFromMeanMSE"] = "Infinity" if aggregate["mse"] == 0 else -10*math.log10(aggregate["mse"])
    report = {"schemaVersion": 1, "policySHA256": registration["sha256"],
              "passed": all(r["passed"] for r in results), "views": results, "aggregate": aggregate,
              "visualInspection": "REQUIRED; contact sheet generated, not machine-certified",
              "contactSheet": str(root/"contact-sheet.png")}
    _write(root/"comparison-report.json", _bytes(report))
    return report
