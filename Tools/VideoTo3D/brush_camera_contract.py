"""Extract source-audited Brush/COLMAP cameras without modifying training inputs.

This adapter deliberately accepts only the audited Brush source revision below.
Config paths are local, absolute paths. CLI output is a new immutable directory;
no image differences or quality metrics are computed here.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys


COORDINATE_CONTRACT = "monster-calibrated-top-left-pixel-centers-v1"
FRAME_IDS = tuple("frame_%05d" % i for i in (1, 9, 17, 25, 33, 41))
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
AUDITED_SOURCES = {
    "brush-dataset/src/formats/colmap.rs": "78d3be0e7b11ec7552ba10de047fb3e53b06ca0124a217be3fadfa5ef1479e36",
    "brush-dataset/src/lib.rs": "cd4eb03fc920377497fa6558b060adc8dc66f08a02e31a782813958d36e91945",
    "brush-dataset/src/scene.rs": "927dc66b981096a0e1d496f9439534892684fe31cb3ac13597fe79fb8bb3f3a7",
    "brush-dataset/src/config.rs": "ec5668d80142298137ee3da0a2943cfdc64518a4f55a64a9bec95ccab700d9af",
    "brush-render/src/camera.rs": "873a769f67deeb2b3c61a227d18f1e4324610dc35613118704acae1f3e3c69c6",
    "brush-render/src/render.rs": "e4b5373f1e1d1db2abebf89c12ec5784f8620693da4639dd6457d15aa45c3af3",
    "brush-render/src/shaders/project_visible.wgsl": "483ece27d1731a4722913ed72c8c9b567a3e5eef4aef0611c4c5d1401efeb7c9",
    "brush-render/src/shaders/project_forward.wgsl": "6f7b98c599c021469d7a00bf207021882bdac98f58e7ed7b3639b49dc21f74e4",
    "brush-render/src/shaders/helpers.wgsl": "bfb6edae5300fd3a4d749b044c07765303c39b9abdf0ebeacd3d6e6bcbfb0b1c",
    "brush-render/src/shaders/rasterize.wgsl": "b83fd19a7e9a18e284c8d1d12c2764e8f311434585e41857d1854ae34e124caf",
    "brush-serde/src/export.rs": "2dfe5cfeaa2dc41498d8c4e0581e2d7e53283ae32003175224a9d4de5b7addb5",
    "brush-train/src/eval.rs": "ab459267e4f1dc79bc9d7fc67c16b0511bdbb26d630db97bb93b63161b813ef4",
    "brush-process/src/eval_export.rs": "189690f7defc0e048c3e107f6bc518b3046523b0e7665d87ab32b6013a6b66c7",
    "brush-process/src/train_stream.rs": "daaa61b15953bd9a0efbc96673b83f0c73181015ec44fa2c554d3665a93f966b",
}


def _json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False) + "\n").encode("utf-8")


def _hash(path):
    digest = hashlib.sha256()
    try:
        with Path(path).open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise RuntimeError("cannot hash input: " + str(path)) from error
    return digest.hexdigest()


def _path(value):
    if not isinstance(value, (str, Path)) or not str(value):
        raise ValueError("input path must be a nonempty string or Path")
    path = Path(value)
    if not path.is_absolute():
        raise ValueError("input paths must be absolute: " + str(path))
    if any(part.is_symlink() or part.is_junction() for part in (path, *path.parents)):
        raise ValueError("input links/junctions are unsupported: " + str(path))
    try:
        return path.resolve(strict=True)
    except OSError as error:
        raise RuntimeError("missing input: " + str(path)) from error


def _identity(path, role=None):
    path = _path(path)
    if not path.is_file():
        raise ValueError("input is not a regular file: " + str(path))
    before = path.stat()
    result = {"path": str(path), "bytes": before.st_size, "sha256": _hash(path)}
    after = path.stat()
    if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns:
        raise ValueError("input changed during hashing: " + str(path))
    return {"role": role, **result} if role else result


def _keys(value, required, optional=()):
    if not isinstance(value, dict) or set(value) - set(required) - set(optional) or set(required) - set(value):
        raise ValueError("unknown or missing schema fields; expected " + ",".join(required))


def _finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def _sha(value):
    return isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value) is not None


def _rigid(matrix):
    if not isinstance(matrix, list) or len(matrix) != 16 or not all(_finite(x) for x in matrix):
        raise ValueError("pose must contain 16 finite column-major values")
    if any(abs(matrix[k] - v) > 1e-5 for k, v in ((3, 0), (7, 0), (11, 0), (15, 1))):
        raise ValueError("pose must be affine")
    rows = [[matrix[c * 4 + r] for c in range(3)] for r in range(3)]
    if any(abs(sum(rows[i][k] * rows[j][k] for k in range(3)) - (i == j)) > 1e-4
           for i in range(3) for j in range(3)):
        raise ValueError("nonrigid camera pose")
    a, b, c = rows
    det = a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
    if abs(det - 1) > 1e-4:
        raise ValueError("reflected or singular camera pose")


def validate_camera_contract(contract: dict) -> None:
    """Reject unknown schemas, incomplete held-out sets and invalid camera values."""
    _keys(contract, ("schemaVersion", "coordinateContract", "protectedInputsSHA256", "auditSHA256", "normalization", "views"))
    if type(contract["schemaVersion"]) is not int or contract["schemaVersion"] != 1 or contract["coordinateContract"] != COORDINATE_CONTRACT:
        raise ValueError("unsupported calibrated camera schema")
    if not all(_sha(contract[key]) for key in ("protectedInputsSHA256", "auditSHA256")):
        raise ValueError("invalid audit/input hash")
    normalization = contract["normalization"]
    _keys(normalization, ("sourceToExport", "exportToSource", "reason"))
    # This source-audited adapter supports precisely the unnormalized COLMAP path.
    if any(not isinstance(normalization[k], list) or len(normalization[k]) != 16 or
           not all(_finite(v) for v in normalization[k]) or normalization[k] != IDENTITY
           for k in ("sourceToExport", "exportToSource")) or not isinstance(normalization["reason"], str) or not normalization["reason"]:
        raise ValueError("unsupported source/export normalization")
    views = contract["views"]
    if not isinstance(views, list) or len(views) != 6 or tuple(v.get("id") for v in views if isinstance(v, dict)) != FRAME_IDS:
        raise ValueError("expected all six sorted held-out frames exactly once")
    for view in views:
        _keys(view, ("id", "width", "height", "fx", "fy", "cx", "cy", "nearPlane", "farPlane", "viewMatrix", "shDegree", "reference", "heldOut", "cameraSourceIdentity"))
        if any(type(view[k]) is not int or not 0 < view[k] <= 16384 for k in ("width", "height")):
            raise ValueError("invalid camera dimensions")
        if not all(_finite(view[k]) for k in ("fx", "fy", "cx", "cy", "nearPlane", "farPlane")):
            raise ValueError("nonfinite intrinsics or clip planes")
        if min(view["fx"], view["fy"], view["nearPlane"]) <= 0 or view["farPlane"] <= view["nearPlane"]:
            raise ValueError("invalid focal lengths or clip planes")
        if type(view["shDegree"]) is not int or not 0 <= view["shDegree"] <= 3:
            raise ValueError("unsupported SH degree")
        _rigid(view["viewMatrix"])
        for field in ("reference", "heldOut"):
            entry = view[field]
            _keys(entry, ("path", "bytes", "sha256"))
            if not isinstance(entry["path"], str) or not Path(entry["path"]).is_absolute() or type(entry["bytes"]) is not int or entry["bytes"] <= 0 or not _sha(entry["sha256"]):
                raise ValueError("invalid image identity")
        source = view["cameraSourceIdentity"]
        _keys(source, ("camerasSHA256", "imagesSHA256", "cameraId", "imageId", "imageName", "model", "distortion", "sourceSize", "evaluationSize", "sourceIntrinsics", "brushResizedPrincipalPoint", "pixelCenterConversion"))
        if source["model"] not in ("PINHOLE", "SIMPLE_PINHOLE") or source["distortion"] != []:
            raise ValueError("unsupported distortion")
        if not _sha(source["camerasSHA256"]) or not _sha(source["imagesSHA256"]):
            raise ValueError("invalid camera source identity")
        if any(type(source[k]) is not int or source[k] <= 0 for k in ("cameraId", "imageId")) or source["imageName"] != view["id"] + ".jpg":
            raise ValueError("invalid source image identifiers")
        if source["evaluationSize"] != [view["width"], view["height"]] or not isinstance(source["sourceSize"], list) or len(source["sourceSize"]) != 2 or any(type(v) is not int or not 0 < v <= 16384 for v in source["sourceSize"]):
            raise ValueError("invalid source/evaluation dimensions")
        intrinsics = source["sourceIntrinsics"]
        if not isinstance(intrinsics, list) or len(intrinsics) != 4 or not all(_finite(v) for v in intrinsics) or min(intrinsics[:2]) <= 0:
            raise ValueError("invalid source intrinsics")
        sx, sy = view["width"]/source["sourceSize"][0], view["height"]/source["sourceSize"][1]
        expected = [intrinsics[0]*sx, intrinsics[1]*sy, intrinsics[2]*sx-0.5, intrinsics[3]*sy-0.5]
        if any(abs(view[k]-v) > 1e-8 for k, v in zip(("fx", "fy", "cx", "cy"), expected)) or source["brushResizedPrincipalPoint"] != [intrinsics[2]*sx, intrinsics[3]*sy] or source["pixelCenterConversion"] != "integer-center principal = Brush edge-origin principal - 0.5":
            raise ValueError("camera resize/pixel-center formula mismatch")


def verify_protected_inputs(inventory: dict) -> dict:
    """Rehash every immutable input; raise on any missing or changed input."""
    _keys(inventory, ("schemaVersion", "entries"))
    if inventory["schemaVersion"] != 1 or not isinstance(inventory["entries"], list) or not inventory["entries"]:
        raise ValueError("unsupported or empty protected inventory")
    seen = set()
    checked = []
    for expected in inventory["entries"]:
        _keys(expected, ("role", "path", "bytes", "sha256"))
        if not isinstance(expected["role"], str) or not expected["role"] or not _sha(expected["sha256"]) or type(expected["bytes"]) is not int or expected["bytes"] < 0:
            raise ValueError("invalid protected identity")
        key = (expected["role"], expected["path"])
        if key in seen:
            raise ValueError("duplicate protected input")
        seen.add(key)
        actual = _identity(expected["path"], expected["role"])
        if actual != expected:
            raise ValueError("protected input changed: " + expected["path"])
        checked.append(actual)
    return {"schemaVersion": 1, "unchanged": True, "checkedCount": len(checked), "entries": checked}


def _read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        raise RuntimeError("cannot read JSON: " + str(path)) from error


def _unpack(stream, fmt):
    size = struct.calcsize("<" + fmt)
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("truncated COLMAP binary")
    return struct.unpack("<" + fmt, data)


def _colmap(camera_path, image_path):
    cameras, images = {}, []
    try:
        with camera_path.open("rb") as stream:
            for _ in range(_unpack(stream, "Q")[0]):
                cid, model, width, height = _unpack(stream, "iiQQ")
                if model not in (0, 1):
                    raise ValueError("unsupported distorted COLMAP model: " + str(model))
                params = _unpack(stream, "ddd" if model == 0 else "dddd")
                fx, fy, cx, cy = (params[0], params[0], params[1], params[2]) if model == 0 else params
                if cid in cameras or not 0 < width <= 16384 or not 0 < height <= 16384 or not all(_finite(x) for x in params) or min(fx, fy) <= 0:
                    raise ValueError("invalid or duplicate COLMAP camera")
                cameras[cid] = (width, height, fx, fy, cx, cy, "SIMPLE_PINHOLE" if model == 0 else "PINHOLE")
            if stream.read(1):
                raise ValueError("trailing camera data")
        with image_path.open("rb") as stream:
            count = _unpack(stream, "Q")[0]
            if not 0 < count <= 1000000:
                raise ValueError("invalid COLMAP image count")
            for _ in range(count):
                values = _unpack(stream, "idddddddi")
                name = bytearray()
                while True:
                    char = stream.read(1)
                    if not char or len(name) > 4096:
                        raise ValueError("unterminated COLMAP image name")
                    if char == b"\0":
                        break
                    name.extend(char)
                name = name.decode("utf-8")
                if Path(name).name != name or "/" in name or "\\" in name:
                    raise ValueError("unsupported COLMAP image subpath")
                points = _unpack(stream, "Q")[0]
                if points > 100000000 or stream.tell() + points*24 > image_path.stat().st_size:
                    raise ValueError("truncated COLMAP observations")
                stream.seek(points * 24, 1)
                images.append({"id": values[0], "q": values[1:5], "t": values[5:8], "camera": values[8], "name": name})
            if stream.read(1):
                raise ValueError("trailing image data")
    except (OSError, UnicodeError, struct.error) as error:
        raise RuntimeError("cannot parse COLMAP input") from error
    if len({v["name"] for v in images}) != len(images) or len({v["id"] for v in images}) != len(images):
        raise ValueError("duplicate COLMAP image")
    return cameras, sorted(images, key=lambda v: v["name"])


def _view_matrix(q, t):
    if not all(_finite(x) for x in (*q, *t)) or abs(sum(x*x for x in q)-1) > 1e-5:
        raise ValueError("nonfinite/nonunit COLMAP pose")
    w, x, y, z = q
    rows = [[1-2*y*y-2*z*z, 2*x*y-2*w*z, 2*x*z+2*w*y, t[0]],
            [-(2*x*y+2*w*z), -(1-2*x*x-2*z*z), -(2*y*z-2*w*x), -t[1]],
            [-(2*x*z-2*w*y), -(2*y*z+2*w*x), -(1-2*x*x-2*y*y), -t[2]],
            [0, 0, 0, 1]]
    matrix = [rows[r][c] for c in range(4) for r in range(4)]
    _rigid(matrix)
    return matrix


def _image_size(path):
    try:
        from PIL import Image
        with Image.open(path) as image:
            size = image.size
            if image.mode != "RGB":
                raise ValueError("reference/held-out inputs must be opaque RGB")
            image.verify()
            return size
    except (OSError, ImportError) as error:
        raise RuntimeError("cannot inspect image dimensions: " + str(path)) from error


def _ply_contract(path):
    try:
        with path.open("rb") as stream:
            lines = []
            for _ in range(256):
                line = stream.readline(4096).decode("ascii").strip()
                lines.append(line)
                if line == "end_header":
                    break
            else:
                raise ValueError("unbounded PLY header")
            if lines[0] != "ply" or "format binary_little_endian 1.0" not in lines:
                raise ValueError("expected exported binary little-endian PLY")
            elements = [line for line in lines if line.startswith("element ")]
            if len(elements) != 1 or not elements[0].startswith("element vertex "):
                raise ValueError("unsupported PLY elements")
            count = int(elements[0].split()[2])
            properties = [line.split()[2] for line in lines if line.startswith("property float ")]
            if len(properties) != sum(line.startswith("property ") for line in lines) or len(set(properties)) != len(properties):
                raise ValueError("unsupported PLY property types/duplicates")
            rest = [p for p in properties if p.startswith("f_rest_")]
            degree = {0: 0, 9: 1, 24: 2, 45: 3}.get(len(rest))
            required = {"x", "y", "z", "opacity", *["scale_%d" % k for k in range(3)], *["rot_%d" % k for k in range(4)], *["f_dc_%d" % k for k in range(3)]}
            if degree is None or set(properties) != required | {"f_rest_%d" % k for k in range(len(rest))} or count <= 0:
                raise ValueError("incomplete Gaussian properties")
            if "comment SH degree: %d" % degree not in lines or path.stat().st_size - stream.tell() != count * len(properties) * 4:
                raise ValueError("PLY degree/payload mismatch")
            row = struct.Struct("<" + "f" * len(properties))
            for _ in range(count):
                values = row.unpack(stream.read(row.size))
                data = dict(zip(properties, values))
                if not all(math.isfinite(v) for v in values) or any(not math.isfinite(math.exp(data["scale_%d" % k])) for k in range(3)) or sum(data["rot_%d" % k]**2 for k in range(4)) <= 1e-20:
                    raise ValueError("invalid Gaussian activation parameters")
    except (OSError, UnicodeError, struct.error, OverflowError) as error:
        raise RuntimeError("cannot inspect exported PLY") from error
    return {"count": count, "shDegree": degree, "propertiesInFileOrder": properties, "allFinite": True}


def _audit_sources(fork):
    entries = []
    anchors = []
    for relative, expected in AUDITED_SOURCES.items():
        path = fork / "crates" / relative
        entry = _identity(path, "brush-source")
        if entry["sha256"] != expected:
            raise ValueError("Brush source differs from audited formulas: " + relative)
        entries.append(entry)
        text = path.read_text(encoding="utf-8")
        needles = {
            "brush-dataset/src/formats/colmap.rs": ["sort_by_key", "world_to_cam.inverse()", "i % eval_period", "flat_map(|(_, p)| p.xyz.to_array())"],
            "brush-dataset/src/lib.rs": ["pub fn estimate_up", "Vec3::new(-transform"],
            "brush-dataset/src/scene.rs": ["let nw", "FilterType::Triangle", "sample.to_rgb32f()"],
            "brush-render/src/camera.rs": ["self.center_uv.x * img_size.x", "0.5 * (pixels as f64)"],
            "brush-render/src/shaders/project_visible.wgsl": ["normalize(mean - uniforms.camera_position.xyz)", "var color =", "let fTmp0C"],
            "brush-render/src/shaders/project_forward.wgsl": ["mean_c.z < 0.01 || mean_c.z > 1e10", "let mean2d", "if opac < 1.0 / 255.0"],
            "brush-render/src/shaders/rasterize.wgsl": ["let pixel_coord", "let alpha =", "pix_out +=", "let final_color"],
            "brush-render/src/shaders/helpers.wgsl": ["const COV_BLUR", "let w = quat.x", "return M * transpose(M)"],
            "brush-serde/src/export.rs": [".permute([0, 2, 1])", "let rest_coeffs =", "x: means[i * 3]", "with_normed_rotations"],
            "brush-train/src/eval.rs": ["Vec3::ZERO", "let render_rgb = (render_rgb * 255.0).round()"],
            "brush-process/src/eval_export.rs": ["img.into_rgb8()"],
        }.get(relative, [])
        for needle in needles:
            line = next((i for i, value in enumerate(text.splitlines(), 1) if needle in value), None)
            if line is None:
                raise ValueError("missing source audit anchor: " + needle)
            anchors.append({"source": relative, "line": line, "anchor": needle, "sha256": expected})
    return entries, anchors


def _build_bundle(config):
    _keys(config, ("schemaVersion", "dataset", "fork", "referenceDirectory", "buildIdentity", "runtimeEvidence", "nearPlane", "farPlane"))
    if type(config["schemaVersion"]) is not int or config["schemaVersion"] != 1:
        raise ValueError("unsupported input configuration")
    if not all(_finite(config[k]) for k in ("nearPlane", "farPlane")) or config["nearPlane"] != 0.01 or config["farPlane"] != 1e10:
        raise ValueError("this Brush source audit requires clip planes 0.01 and 1e10")
    dataset, fork, reference = (_path(config[k]) for k in ("dataset", "fork", "referenceDirectory"))
    invocation_path = reference / "invocation.json"
    invocation = _read_json(invocation_path)
    command = invocation.get("command", [])
    if not isinstance(command, list) or len(command) < 2 or _path(command[1]) != dataset:
        raise ValueError("reference invocation dataset mismatch")
    def option(name, default=None):
        if command.count(name) > 1:
            raise ValueError("duplicate invocation argument: " + name)
        return command[command.index(name)+1] if name in command else default
    if option("--total-steps") != "600" or option("--eval-split-every") != "8" or any(name in command for name in ("--max-frames", "--subsample-frames")):
        raise ValueError("reference is not the complete default600 held-out split")
    maximum = int(option("--max-resolution", "1920"))
    if maximum <= 0:
        raise ValueError("invalid Brush max resolution")
    entries, anchors = _audit_sources(fork)
    runtime = _identity(command[0], "brush-runtime")
    if invocation.get("executableSha256") != runtime["sha256"]:
        raise ValueError("Brush runtime differs from actual reference invocation")
    build_path = _path(config["buildIdentity"])
    build = _read_json(build_path)
    if build.get("build", {}).get("exitCode") != 0 or build.get("binary", {}).get("sha256") != runtime["sha256"] or _path(build["build"]["cwd"]) != fork:
        raise ValueError("Brush build/runtime evidence mismatch")
    build_files = {v["path"]: v["workingBytesSha256"] for v in build["source"]["files"]}
    for relative in AUDITED_SOURCES:
        key = "crates/" + relative
        if key in build_files and build_files[key] != AUDITED_SOURCES[relative]:
            raise ValueError("audited source differs from build evidence: " + key)
    for relative, expected in build_files.items():
        relative_path = Path(relative)
        if relative_path.is_absolute() or ".." in relative_path.parts:
            raise ValueError("invalid source path in build evidence")
        entry = _identity(fork / relative_path, "brush-source")
        if entry["sha256"] != expected:
            raise ValueError("current Brush source differs from successful build: " + relative)
        if not any(item["role"] == entry["role"] and item["path"] == entry["path"] for item in entries):
            entries.append(entry)
    log_path = build_path.parent / build["build"]["log"]
    build_log = _identity(log_path, "brush-build-log")
    if build_log["sha256"] != build["build"]["logSha256"]:
        raise ValueError("Brush successful build log changed")
    runtime_evidence = _read_json(config["runtimeEvidence"])
    if runtime_evidence.get("runtime", {}).get("sha256") != runtime["sha256"] or runtime_evidence.get("compileVerification", {}).get("exitCode") != 0 or runtime_evidence["compileVerification"].get("verifiedLogSha256") != build_log["sha256"]:
        raise ValueError("Brush retained runtime verification evidence mismatch")
    entries.extend([runtime, build_log, _identity(build_path, "brush-build-evidence"), _identity(invocation_path, "brush-invocation"), _identity(config["runtimeEvidence"], "brush-runtime-evidence")])
    for relative in ("Cargo.toml", "Cargo.lock"):
        entries.append(_identity(fork / relative, "brush-build-config"))
    camera_path, image_path = dataset / "sparse" / "cameras.bin", dataset / "sparse" / "images.bin"
    camera_identity, image_identity = _identity(camera_path, "cameras"), _identity(image_path, "images")
    entries.extend((camera_identity, image_identity))
    cameras, images = _colmap(camera_path, image_path)
    selected = images[::8]
    if tuple(Path(v["name"]).stem for v in selected) != FRAME_IDS:
        raise ValueError("actual sorted Brush split differs from required held-out set")
    ply_path = reference / "output" / "export_600.ply"
    entries.append(_identity(ply_path, "reference-ply"))
    ply = _ply_contract(ply_path)
    views = []
    for image in selected:
        if image["camera"] not in cameras:
            raise ValueError("image references missing camera")
        width, height, fx, fy, cx, cy, model = cameras[image["camera"]]
        original_path = dataset / "images" / image["name"]
        rendered_path = reference / "output" / "eval_600" / (Path(image["name"]).stem + ".png")
        original, rendered = _identity(original_path), _identity(rendered_path)
        if _image_size(original_path) != (width, height):
            raise ValueError("source image dimensions differ from camera")
        ratio = min(1.0, maximum / width, maximum / height)
        size = (max(1, math.floor(width*ratio+0.5)), max(1, math.floor(height*ratio+0.5)))
        if _image_size(rendered_path) != size:
            raise ValueError("reference dimensions differ from Brush resize formula")
        sx, sy = size[0]/width, size[1]/height
        view = {"id": Path(image["name"]).stem, "width": size[0], "height": size[1],
                "fx": fx*sx, "fy": fy*sy, "cx": cx*sx-0.5, "cy": cy*sy-0.5,
                "nearPlane": config["nearPlane"], "farPlane": config["farPlane"],
                "viewMatrix": _view_matrix(image["q"], image["t"]), "shDegree": ply["shDegree"],
                "reference": rendered, "heldOut": original,
                "cameraSourceIdentity": {"camerasSHA256": camera_identity["sha256"], "imagesSHA256": image_identity["sha256"],
                    "cameraId": image["camera"], "imageId": image["id"], "imageName": image["name"], "model": model, "distortion": [],
                    "sourceSize": [width, height], "evaluationSize": list(size), "sourceIntrinsics": [fx, fy, cx, cy],
                    "brushResizedPrincipalPoint": [cx*sx, cy*sy], "pixelCenterConversion": "integer-center principal = Brush edge-origin principal - 0.5"}}
        entries.extend(({"role": "held-out-original", **original}, {"role": "reference-render", **rendered}))
        views.append(view)
    inventory = {"schemaVersion": 1, "entries": entries}
    audit = {"schemaVersion": 1, "scope": "source audit and input inspection only; no rendering acceptance", "sourceAnchors": anchors, "ply": ply,
             "normalization": "identity: COLMAP init xyz and camera world frame are used directly; export reads means directly; estimate_up returns a UI up vector and does not transform data",
             "pose": "COLMAP W2C R(wxyz),t; engine W2V=diag(1,-1,-1,1)*W2C; column-major storage; camera center=-transpose(R)*t",
             "resize": "sort images by name, enumerate i%8==0; image crate Triangle resize longest side to maxResolution, positive dimensions rounded half-up; fx'=fx*W'/W, fy'=fy*H'/H, Brush cx'=cx*W'/W,cy'=cy*H'/H",
             "pixelCenters": "Brush projects into edge-origin coordinates and evaluates at index+0.5; engine integer-center principal is cxBrush-0.5,cyBrush-0.5. No fitted offset or registration.",
             "clipPlanes": {"near": 0.01, "far": 1e10, "source": "project_forward.wgsl rejects mean_c.z <0.01 or >1e10; adapter requires identical bounds"},
             "activations": {"scale": "exp(PLY scale_i)", "opacity": "1/(1+exp(-PLY opacity))", "quaternion": "PLY rot_0..3=w,x,y,z; normalize; covariance=R*diag(exp(scale)^2)*transpose(R)"},
             "sh": {"layout": "PLY f_rest_[channel*(K-1)+(basis-1)] => RGB triplet per basis; K=(degree+1)^2; use property names, never lexical property order", "direction": "normalize(mean-cameraPosition) in unchanged world frame", "offsetClamp": "RGB=max(0,0.5+sum(SH_basis*coefficient)) per splat before compositing; no upper per-splat clamp",
                    "basis0": "0.2820947917738781", "basis1": "0.48860251190292*(-y,z,-x)",
                    "basis2": "(1.092548430592079*x*y,-1.092548430592079*y*z,0.3153915652525201*(3*z*z-1),-1.092548430592079*x*z,0.5462742152960395*(x*x-y*y))",
                    "basis3": "(-0.5900435899266435*y*(3*x*x-y*y),2.890611442640554*x*y*z,-0.4570457994644658*y*(5*z*z-1),0.3731763325901154*z*(5*z*z-3),-0.4570457994644658*x*(5*z*z-1),1.445305721320277*z*(x*x-y*y),-0.5900435899266435*x*(x*x-3*y*y))"},
             "filter": "cov2D=J*R*cov3D*transpose(R)*transpose(J)+0.3*I; Jacobian xy/z clipped to [-0.15*size-center,1.15*size-center]/focal; alpha=min(0.999,sigmoid(opacity)*exp(-0.5*d^T*inverse(cov2D)*d)); skip alpha<1/255; stop before contribution when nextT<=1e-4",
             "color": {"encoding": "trained-image-encoded-rgb", "transfer": "identity: JPEG RGB bytes/255 become float training samples; no sRGB decoding or encoding in eval", "backgroundRGB": [0, 0, 0], "exposure": "identity", "composite": "sum(T*alpha*max(SH+0.5,0))+T*black", "evalQuantization": "rendered=(floatRGB*255).round()/255; Rgb32FImage.into_rgb8 saturates to [0,255]; PNG stores opaque RGB top-left rows"},
             "maxResolution": maximum, "split": [v["name"] for v in selected], "geometricRegistration": "none"}
    contract = {"schemaVersion": 1, "coordinateContract": COORDINATE_CONTRACT,
                "protectedInputsSHA256": hashlib.sha256(_json_bytes(inventory)).hexdigest(), "auditSHA256": hashlib.sha256(_json_bytes(audit)).hexdigest(),
                "normalization": {"sourceToExport": IDENTITY.copy(), "exportToSource": IDENTITY.copy(), "reason": audit["normalization"]}, "views": views}
    validate_camera_contract(contract)
    verify_protected_inputs(inventory)
    return contract, inventory, audit


def build_camera_contract(input_config: dict) -> dict:
    """Build the six-view strict camera contract from protected on-disk inputs."""
    try:
        return _build_bundle(input_config)[0]
    except (KeyError, IndexError, TypeError) as error:
        raise ValueError("malformed source/build/invocation evidence") from error
    except (OSError, UnicodeError) as error:
        raise RuntimeError("cannot inspect protected Brush inputs") from error


def _write_new(path, value):
    try:
        with path.open("xb") as stream:
            stream.write(_json_bytes(value))
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        raise RuntimeError("cannot persist new evidence: " + str(path)) from error


def main():
    """Persist identities and source audit before making cameras available."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        contract, inventory, audit = _build_bundle(_read_json(args.config))
        args.output.mkdir(parents=True, exist_ok=False)
        _write_new(args.output / "protected-inputs.json", inventory)
        _write_new(args.output / "brush-render-contract-audit.json", audit)
        if _hash(args.output / "protected-inputs.json") != contract["protectedInputsSHA256"] or _hash(args.output / "brush-render-contract-audit.json") != contract["auditSHA256"]:
            raise ValueError("durable audit/input hashes differ")
        _write_new(args.output / "calibrated-views.json", contract)
        print("[OK] audited all six calibrated views; no image comparison performed")
        return 0
    except (ValueError, RuntimeError, OSError, KeyError, IndexError, TypeError) as error:
        print("[FAIL] " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
