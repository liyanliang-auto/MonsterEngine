"""Identity-bound reconstruction bridge, supervised by the native Windows Job.

Invoke only through ProjectReconstruction: --request <absolute request.json>.
Candidate outcomes are not authoritative until the native host retires the tree.
Only Python's standard library and the explicitly pinned preparation file load.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import time
import types
import uuid


MAX_JSON = 16 * 1024 * 1024
MAX_EVENT_LINE = 1024 * 1024
MAX_EVENT_FILE = 64 * 1024 * 1024
MAX_LOG = 512 * 1024 * 1024
SETTING_LIMITS = {
    "extractionFps": (0.1, 30), "extractionWidth": (64, 4096),
    "colmapThreads": (1, 32), "colmapSeed": (0, 2147483647),
    "brushSteps": (1, 100000), "brushSeed": (0, 2147483647),
    "maxResolution": (64, 4096), "maxSplats": (1, 2000000),
    "refineEvery": (1, 100000), "evalEvery": (1, 100000),
    "exportEvery": (1, 100000), "evalSplitEvery": (2, 1000),
}
BUDGET_NAMES = {"wholeRun", "ffprobe", "ffmpeg", "colmapFeature", "colmapMatch",
                "colmapMapper", "colmapOther", "brush", "control", "retirement"}


def unique_object(pairs):
    """Reject duplicate keys rather than accepting an ambiguous control/identity."""
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key: " + key)
        result[key] = value
    return result


def decode_json(raw):
    """Decode finite, unambiguous JSON, accepting an optional UTF-8 BOM."""
    def invalid(value):
        raise ValueError("nonfinite JSON value: " + value)
    def finite_float(value):
        parsed = float(value)
        if not math.isfinite(parsed):
            raise ValueError("nonfinite JSON number")
        return parsed
    return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object,
                      parse_constant=invalid, parse_float=finite_float)


def ordinary(path, directory=False):
    """Reject links/reparse points, including existing ancestry."""
    path = Path(path)
    for item in (path, *path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("link/reparse path rejected: " + str(item))
        if item == path and not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
            raise ValueError("expected ordinary file/directory: " + str(path))
    return path


def read_json(path, maximum=MAX_JSON):
    """Read a bounded ordinary JSON file."""
    ordinary(path)
    with Path(path).open("rb") as stream:
        raw = stream.read(maximum + 1)
    if len(raw) > maximum:
        raise ValueError("JSON file exceeds bound: " + str(path))
    return decode_json(raw)


def sha256(path):
    """Hash ordinary bytes without loading a whole model into memory."""
    ordinary(path)
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def file_identity(path):
    """Record the observed file length/hash with an absolute path."""
    path = ordinary(path).absolute()
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": sha256(path)}


def write_json(path, value):
    """Flush then atomically publish a new immutable record, never replace one."""
    path = Path(path)
    if path.exists():
        raise ValueError("refusing to replace evidence: " + str(path))
    ordinary(path.parent, True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=True, allow_nan=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    # Windows rename refuses an existing destination. The execution lease is the
    # serialization boundary on other platforms; production is Windows only.
    temporary.rename(path)


def token(value, maximum=120):
    """Require a non-reserved single ASCII path/control component."""
    return (isinstance(value, str) and 1 <= len(value) <= maximum
            and re.fullmatch(r"[A-Za-z0-9_-]+", value) is not None
            and value.upper() not in {"CON", "PRN", "AUX", "NUL",
                                     *[f"COM{i}" for i in range(1, 10)],
                                     *[f"LPT{i}" for i in range(1, 10)]})


def absolute(value):
    """Require an explicit absolute path, never PATH discovery."""
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise ValueError("absolute path required")
    return Path(value)


def within(root, relative):
    """Resolve a project/execution relative path without traversal or aliases."""
    if not isinstance(relative, str) or "\\" in relative or ":" in relative:
        raise ValueError("invalid portable relative path")
    parts = relative.split("/")
    if not parts or any(not part or part in (".", "..") for part in parts):
        raise ValueError("relative path traversal rejected")
    return root.joinpath(*parts)


def admit(pin):
    """Observe an explicit expected SHA-256 before use; this is not attestation."""
    if not isinstance(pin, dict) or not isinstance(pin.get("sha256"), str) \
            or re.fullmatch(r"[0-9a-fA-F]{64}", pin["sha256"]) is None:
        raise ValueError("missing/invalid tool or helper SHA-256 pin")
    actual = file_identity(absolute(pin["path"]))
    if actual["sha256"] != pin["sha256"].lower():
        raise ValueError("identity mismatch: " + actual["path"])
    return actual


def inventory(root):
    """Record all ordinary files in deterministic order, rejecting reparse dirs."""
    ordinary(root, True)
    result = []
    for path in sorted(root.rglob("*")):
        ordinary(path, path.is_dir())
        if path.is_file():
            identity = file_identity(path)
            identity["path"] = path.relative_to(root).as_posix()
            result.append(identity)
    if not result:
        raise ValueError("empty inventory: " + str(root))
    return result


def validate_ply(path, max_splats):
    """Validate exact binary Gaussian payload length and every finite f32."""
    ordinary(path)
    with path.open("rb") as stream:
        header = []
        for _ in range(256):
            line = stream.readline(4097)
            if not line or len(line) > 4096:
                raise ValueError("invalid PLY header")
            header.append(line.decode("ascii").strip())
            if header[-1] == "end_header":
                break
        else:
            raise ValueError("PLY header exceeds bound")
        elements = [value for value in header if value.startswith("element ")]
        if (header[0] != "ply" or "format binary_little_endian 1.0" not in header
                or len(elements) != 1 or not elements[0].startswith("element vertex ")):
            raise ValueError("unsupported Gaussian PLY format")
        count = int(elements[0].split()[2])
        properties = [value.split() for value in header if value.startswith("property ")]
        if any(len(p) != 3 or p[1] != "float" for p in properties):
            raise ValueError("Gaussian PLY requires scalar float properties")
        names = [p[2] for p in properties]
        required = {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                    "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}
        if not required <= set(names) or len(set(names)) != len(names) or not 0 < count <= max_splats:
            raise ValueError("invalid Gaussian PLY properties/count")
        remaining = count * len(names) * 4
        while remaining:
            data = stream.read(min(1024 * 1024, remaining))
            if not data or len(data) % 4 or any(not math.isfinite(v[0]) for v in struct.iter_unpack("<f", data)):
                raise ValueError("truncated/nonfinite Gaussian PLY")
            remaining -= len(data)
        if stream.read(1):
            raise ValueError("trailing Gaussian PLY bytes")
    return {"gaussians": count, "floatProperties": names, "allFinite": True}


def validate_checkpoint(store, checkpoint_id, expected_identity):
    """Validate a committed immutable checkpoint, every payload and safe cursors.

    The fork additionally validates strict trainer/sampler semantics before its
    RestoreValidated/Restored events; this loader never activates trainer state.
    """
    if not token(checkpoint_id, 180) or not checkpoint_id.startswith("cp-"):
        raise ValueError("invalid checkpoint ID")
    root = store / checkpoint_id
    ordinary(root, True)
    marker = read_json(root / "COMMITTED.json")
    if set(marker) != {"id", "manifest_sha256"} or marker["id"] != checkpoint_id \
            or sha256(root / "manifest.json") != marker["manifest_sha256"]:
        raise ValueError("checkpoint commit marker/manifest mismatch")
    manifest = read_json(root / "manifest.json")
    if set(manifest) != {"schema_version", "identity", "next_iteration", "metadata", "tensors"} \
            or manifest["schema_version"] != 1 or manifest["identity"] != expected_identity:
        raise ValueError("checkpoint schema/runtime identity mismatch")
    next_iteration = manifest["next_iteration"]
    metadata = manifest["metadata"]
    if (type(next_iteration) is not int or next_iteration < 0
            or metadata.get("schema_version") != 1
            or metadata.get("trainer", {}).get("schema_version") != 1
            or metadata.get("sampler", {}).get("schema_version") != 1
            or metadata["trainer"].get("next_iteration") != next_iteration
            or metadata["sampler"].get("next_sequence") != next_iteration
            or not token(metadata.get("generation")) or not token(metadata.get("request_id"))
            or not isinstance(metadata.get("training_seconds"), (int, float))
            or not math.isfinite(metadata["training_seconds"]) or metadata["training_seconds"] < 0):
        raise ValueError("checkpoint trainer/sampler cursor/metadata mismatch")
    tensors = manifest["tensors"]
    if not isinstance(tensors, list) or not 1 <= len(tensors) <= 65536:
        raise ValueError("invalid checkpoint tensor inventory")
    names, payloads = set(), set()
    for tensor in tensors:
        required = {"name", "payload", "shape", "dtype", "bytes", "sha256"}
        if not isinstance(tensor, dict) or not required <= tensor.keys() \
                or tensor.keys() - (required | {"param_id"}):
            raise ValueError("invalid checkpoint tensor descriptor")
        name, payload = tensor["name"], tensor["payload"]
        if (not token(name, 180) or not token(payload, 180) or name in names or payload in payloads
                or tensor.get("param_id") is not None and not token(tensor["param_id"], 180)):
            raise ValueError("duplicate/invalid tensor name/payload")
        names.add(name)
        payloads.add(payload)
        shape = tensor["shape"]
        if tensor["dtype"] != "f32le" or not isinstance(shape, list) or len(shape) > 8:
            raise ValueError("unsupported tensor dtype/shape")
        size = 4
        for dimension in shape:
            if type(dimension) is not int or not 0 <= dimension <= (1 << 64) - 1:
                raise ValueError("invalid tensor dimension")
            size *= dimension
            if size > (1 << 64) - 1:
                raise ValueError("tensor size overflow")
        path = root / "payloads" / payload
        if (type(tensor["bytes"]) is not int or tensor["bytes"] != size
                or path.stat().st_size != size or sha256(path) != tensor["sha256"]):
            raise ValueError("tensor length/hash mismatch: " + name)
        with path.open("rb") as stream:
            for data in iter(lambda: stream.read(1024 * 1024), b""):
                if len(data) % 4 or any(not math.isfinite(v[0]) for v in struct.iter_unpack("<f", data)):
                    raise ValueError("nonfinite checkpoint tensor: " + name)
    if {p.name for p in (root / "payloads").iterdir()} != payloads:
        raise ValueError("undeclared/missing checkpoint payload")
    if not {"means", "rotation", "log_scales", "sh_coeffs", "raw_opacity"} <= names:
        raise ValueError("missing full trainer tensor family")
    return {"checkpointId": checkpoint_id, "validated": True, "manifest": file_identity(root / "manifest.json"),
            "inventory": inventory(root), "nextIteration": next_iteration,
            "identity": expected_identity, "metadata": metadata}


class JsonlReader:
    """Incrementally consume bounded complete records without accepting a tail."""

    def __init__(self, path):
        self.path, self.offset, self.pending = path, 0, b""

    def read(self):
        if not self.path.exists():
            return []
        ordinary(self.path)
        size = self.path.stat().st_size
        if size < self.offset or size > MAX_EVENT_FILE:
            raise ValueError("event stream truncated/oversized")
        with self.path.open("rb") as stream:
            stream.seek(self.offset)
            raw = stream.read(MAX_EVENT_FILE + 1 - self.offset)
        self.offset += len(raw)
        chunks = (self.pending + raw).split(b"\n")
        self.pending = chunks.pop()
        if len(self.pending) > MAX_EVENT_LINE or any(len(v) > MAX_EVENT_LINE for v in chunks):
            raise ValueError("event line exceeds bound")
        return [decode_json(value) for value in chunks if value]


class Runner:
    """Orchestrate observed stages; the native host owns forced tree retirement."""

    def __init__(self, request):
        self.request = request
        self.sequence = 0
        self.commands = []
        self.controls = []
        self.brush_events = []
        self.seen_controls = {}
        self.actual_tools = {}
        self.active_process = None
        self.started = time.monotonic()
        self.outcome = "Failed"
        self.failure = ""
        self.dataset_inventory = []
        self.runtime_identity = None
        self.checkpoint = None
        self.output = None
        self.resume_contract = None
        self.prepared = None
        if request.get("schemaVersion") != 1 or request.get("action") not in ("start", "resume"):
            raise ValueError("unsupported runner request")
        for key in ("executionId", "projectId"):
            if not token(request.get(key)):
                raise ValueError("invalid " + key)
        self.generation = request.get("generation")
        if type(self.generation) is not int or self.generation <= 0:
            raise ValueError("positive attempt generation required")
        self.root = absolute(request["executionDirectory"])
        self.attempt = self.root / ("attempt-" + str(self.generation))
        ordinary(self.root, True)
        # Native may precreate the attempt for its request/control transport.
        self.attempt.mkdir(exist_ok=True)
        ordinary(self.attempt, True)
        self.control = absolute(request["controlDirectory"])
        if self.control != self.attempt / "controls":
            raise ValueError("control directory must be attempt-N/controls")
        self.control.mkdir(exist_ok=True)
        self.events_path = self.attempt / "events.jsonl"
        with self.events_path.open("xb"):
            pass

    def emit(self, kind, message, **fields):
        """Append/flushed ordered observations; candidates remain provisional."""
        self.sequence += 1
        event = {"schemaVersion": 1, "executionId": self.request["executionId"],
                 "generation": self.generation, "sequence": self.sequence,
                 "kind": kind, "message": message, **fields}
        raw = (json.dumps(event, ensure_ascii=True, allow_nan=False) + "\n").encode("utf-8")
        if len(raw) > MAX_EVENT_LINE:
            raise ValueError("outgoing event exceeds bound")
        with self.events_path.open("ab") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())

    def configure(self):
        """Revalidate config, helpers, tools and project-owned media before work."""
        config_path = absolute(self.request["configPath"])
        if sha256(config_path) != self.request["configSha256"].lower():
            raise ValueError("configuration identity changed")
        config = read_json(config_path)
        if config.get("schemaVersion") != 1:
            raise ValueError("unsupported reconstruction config schema")
        self.config = config
        self.settings = config["settings"]
        if set(self.settings) != set(SETTING_LIMITS):
            raise ValueError("all bounded settings must be explicit; unknown setting rejected")
        for name, (low, high) in SETTING_LIMITS.items():
            value = self.settings[name]
            if (type(value) not in (int, float) or not math.isfinite(value)
                    or not low <= value <= high or name != "extractionFps" and type(value) is not int):
                raise ValueError("invalid bounded setting: " + name)
        self.budgets = config["budgetsSeconds"]
        if set(self.budgets) != BUDGET_NAMES or any(type(v) not in (int, float)
                or not math.isfinite(v) or not 0 < v <= 86400 for v in self.budgets.values()):
            raise ValueError("explicit finite stage/control budgets required")
        if set(config["tools"]) != {"ffprobe", "ffmpeg", "colmap", "brush", "python"} \
                or set(config["helpers"]) != {"runner", "preparation"}:
            raise ValueError("complete explicit tool/helper identities required")
        self.actual_tools = {name: admit(pin) for name, pin in config["tools"].items()}
        self.actual_helpers = {name: admit(pin) for name, pin in config["helpers"].items()}
        if sha256(Path(__file__).absolute()) != self.actual_helpers["runner"]["sha256"] \
                or sha256(Path(sys.executable).absolute()) != self.actual_tools["python"]["sha256"]:
            raise ValueError("running Python/runner identity mismatch")
        prep_path = Path(self.actual_helpers["preparation"]["path"])
        raw = prep_path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != self.actual_helpers["preparation"]["sha256"]:
            raise ValueError("preparation changed before import")
        self.geometry = types.ModuleType("pinned_preparation")
        self.geometry.__file__ = str(prep_path)
        exec(compile(raw, str(prep_path), "exec"), self.geometry.__dict__)
        project = absolute(self.request["projectDirectory"])
        ordinary(project, True)
        if self.root != project / "executions" / self.request["executionId"]:
            raise ValueError("execution must belong to project/executions/executionId")
        # The owned-input manifest records native Windows separators. Preserve
        # those exact identity bytes, but validate its components through the
        # same portable traversal/alias boundary used by execution references.
        input_relative = self.request["input"]["relativePath"]
        if isinstance(input_relative, str):
            input_relative = input_relative.replace("\\", "/")
        self.source = within(project, input_relative)
        self.verify_source()
        write_json(self.attempt / "admitted-config.json", {
            "configSha256": self.request["configSha256"], "config": config,
            "tools": self.actual_tools, "helpers": self.actual_helpers,
            "observationBoundary": "Hash handles close before launch; no launch-image or DLL attestation."})

    def verify_source(self):
        """Never infer media integrity from import success or an old inventory."""
        item = self.request["input"]
        if self.source.stat().st_size != item["bytes"] or sha256(self.source) != item["sha256"].lower():
            raise ValueError("project-owned input size/SHA-256 mismatch")

    def controls_ready(self):
        """Read each immutable identity-matching native control exactly once."""
        result = []
        for path in sorted(self.control.glob("*.json")):
            value = read_json(path, 16384)
            raw_hash = sha256(path)
            if path.name in self.seen_controls:
                if self.seen_controls[path.name] != raw_hash:
                    raise ValueError("native control request bytes changed")
                continue
            self.seen_controls[path.name] = raw_hash
            if (value.get("schemaVersion") != 1 or value.get("executionId") != self.request["executionId"]
                    or value.get("generation") != self.generation
                    or not token(value.get("requestId")) or path.stem != value["requestId"]
                    or value.get("command") not in ("PauseAndSave", "ForceStop")):
                raise ValueError("stale/mismatched native control request")
            if "atNextIteration" in value and (type(value["atNextIteration"]) is not int
                    or not 0 <= value["atNextIteration"] <= self.settings["brushSteps"]):
                raise ValueError("invalid requested pause boundary")
            self.controls.append({"request": value, "sha256": raw_hash, "observedSequence": self.sequence})
            result.append(value)
        return result

    def guard(self, process, deadline, logs):
        """Bound each child; a raised failure delegates tree kill to native host."""
        if time.monotonic() > deadline or time.monotonic() - self.started > self.budgets["wholeRun"]:
            raise TimeoutError("execution/stage time budget exceeded; native host must retire tree")
        if any(path.exists() and path.stat().st_size > MAX_LOG for path in logs):
            raise ValueError("child log exceeds 512 MiB guard; native host must retire tree")

    def command(self, tool, arguments, name, budget):
        """Execute one pinned shell-free stage, retaining logs and exit record."""
        actual = admit(self.config["tools"][tool])
        argv = [actual["path"], *map(str, arguments)]
        self.emit("StageStarted", name, stage=name)
        stdout = self.attempt / (name + ".stdout.log")
        stderr = self.attempt / (name + ".stderr.log")
        entry = {"name": name, "argv": argv, "tool": actual,
                 "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                 "cwd": str(self.attempt), "budgetSeconds": self.budgets[budget]}
        self.commands.append(entry)
        begin = time.monotonic()
        try:
            with stdout.open("xb") as out, stderr.open("xb") as err:
                process = subprocess.Popen(argv, cwd=self.attempt, stdin=subprocess.DEVNULL,
                                           stdout=out, stderr=err, shell=False)
                self.active_process = process
                entry["pid"] = process.pid
                deadline = begin + self.budgets[budget]
                while process.poll() is None:
                    self.guard(process, deadline, (stdout, stderr))
                    for control in self.controls_ready():
                        if control["command"] == "ForceStop":
                            self.outcome = "Stopped"
                            raise RuntimeError("forced stop requested; native tree retirement required")
                        self.emit("SaveFailed", "Safe pause is supported only after BrushReady",
                                  requestId=control["requestId"])
                    time.sleep(0.05)
                entry["exitCode"] = process.returncode
                self.active_process = None
                if process.returncode:
                    raise RuntimeError(name + " returned " + str(process.returncode) + "; see preserved logs")
        except Exception as error:
            entry["error"] = str(error)
            raise
        finally:
            entry["elapsedSeconds"] = time.monotonic() - begin
            write_json(self.attempt / (name + ".command.json"), entry)
        self.emit("StageCompleted", name, stage=name)
        return stdout

    def prepare(self):
        """Fresh extraction/COLMAP; reuse pinned geometry parsers and policy."""
        root = self.root / "preparation"
        root.mkdir(exist_ok=False)
        images = root / "frames"
        images.mkdir()
        settings = self.settings
        self.command("ffprobe", ["-v", "error", "-show_format", "-show_streams", "-of", "json", self.source],
                     "ffprobe", "ffprobe")
        self.command("ffmpeg", ["-nostdin", "-n", "-i", self.source, "-vf",
                     f"fps={settings['extractionFps']},scale={settings['extractionWidth']}:-2", "-q:v", "2",
                     images / "frame_%05d.jpg"], "extract", "ffmpeg")
        frame_names = sorted(p.name for p in images.glob("*.jpg"))
        write_json(root / "frame-inventory.json", inventory(images))
        if len(frame_names) < self.geometry.POLICY["minExtractedFrames"]:
            raise ValueError("insufficient extracted frames for geometry policy")
        database = root / "database.db"
        seed = ["--default_random_seed", str(settings["colmapSeed"])]
        self.command("colmap", ["feature_extractor", "--database_path", database, "--image_path", images,
                     "--ImageReader.single_camera", "1", "--ImageReader.camera_model", "SIMPLE_RADIAL",
                     "--FeatureExtraction.use_gpu", "0", "--FeatureExtraction.num_threads",
                     settings["colmapThreads"], *seed], "features", "colmapFeature")
        self.command("colmap", ["exhaustive_matcher", "--database_path", database,
                     "--FeatureMatching.use_gpu", "0", "--FeatureMatching.num_threads", settings["colmapThreads"],
                     *seed], "matching", "colmapMatch")
        sparse = root / "sparse"
        sparse.mkdir()
        self.command("colmap", ["mapper", "--database_path", database, "--image_path", images,
                     "--output_path", sparse, "--Mapper.num_threads", settings["colmapThreads"], *seed],
                     "mapping", "colmapMapper")
        candidates = []
        for index, model in enumerate(sorted(p for p in sparse.iterdir() if p.is_dir())):
            text = root / ("model-text-" + str(index))
            text.mkdir()
            self.command("colmap", ["model_converter", "--input_path", model, "--output_path", text,
                         "--output_type", "TXT"], "convert-" + str(index), "colmapOther")
            metrics = self.geometry.model_metrics(text, frame_names)
            candidates.append({"model": str(model), "metrics": metrics,
                               "admission": self.geometry.admission(metrics)})
        write_json(root / "reconstruction-candidates.json", candidates)
        if not candidates:
            raise ValueError("COLMAP produced no reconstruction")
        selected = max(candidates, key=lambda value: value["metrics"]["registeredImages"])
        if not selected["admission"]["passed"]:
            raise ValueError("largest COLMAP model failed geometric admission")
        dataset = root / "dataset"
        dataset.mkdir()
        self.command("colmap", ["image_undistorter", "--image_path", images, "--input_path", selected["model"],
                     "--output_path", dataset, "--output_type", "COLMAP", "--max_image_size",
                     settings["extractionWidth"]], "undistort", "colmapOther")
        text = root / "undistorted-text"
        text.mkdir()
        self.command("colmap", ["model_converter", "--input_path", dataset / "sparse", "--output_path", text,
                     "--output_type", "TXT"], "convert-undistorted", "colmapOther")
        metrics = self.geometry.model_metrics(text, frame_names, require_pinhole=True)
        if not self.geometry.admission(metrics)["passed"]:
            raise ValueError("undistorted geometry failed admission")
        points = dataset / "sparse" / "points3D.bin"
        points.rename(points.with_name("points3d.bin"))
        for field, filename in (("provenance", "provenance.json"), ("attribution", "ATTRIBUTION.txt")):
            if field in self.config:
                source = absolute(self.config[field])
                ordinary(source)
                shutil.copyfile(source, root / filename)
                if field == "attribution":
                    shutil.copyfile(source, dataset / filename)
        (dataset / "MODIFICATIONS.txt").write_text(
            "Derived from project-owned video SHA256 " + self.request["input"]["sha256"] + ".\n"
            + f"FFmpeg extraction {settings['extractionFps']} fps width {settings['extractionWidth']}; JPEG q2.\n"
            + "COLMAP CPU reconstruction, geometric admission and PINHOLE undistortion. Original preserved.\n",
            encoding="utf-8")
        self.dataset_inventory = inventory(dataset)
        self.prepared = {"schemaVersion": 1, "datasetPath": str(dataset), "datasetInventory": self.dataset_inventory,
                         "metrics": metrics, "admission": self.geometry.admission(metrics),
                         "policy": self.geometry.POLICY, "input": self.request["input"]}
        write_json(root / "prepared-scene-identity.json", self.prepared)
        return dataset

    def brush_arguments(self):
        """Return complete explicit configurable semantics; fork pins defaults."""
        settings = self.settings
        return ["--total-steps", str(settings["brushSteps"]), "--seed", str(settings["brushSeed"]),
                "--max-resolution", str(settings["maxResolution"]), "--max-splats", str(settings["maxSplats"]),
                "--refine-every", str(settings["refineEvery"]), "--eval-every", str(settings["evalEvery"]),
                "--export-every", str(settings["exportEvery"]), "--eval-split-every", str(settings["evalSplitEvery"]),
                "--eval-save-to-disk"]

    def restore(self):
        """Revalidate the sealed lineage before starting a fresh restore process."""
        checkpoint_id = self.request.get("resumeCheckpoint")
        if not token(checkpoint_id, 180) or not checkpoint_id.startswith("cp-"):
            raise ValueError("explicit committed resumeCheckpoint required")
        path = self.root / "checkpoint-contracts" / (checkpoint_id + ".json")
        contract = read_json(path)
        if contract.get("schemaVersion") != 1:
            raise ValueError("unsupported resume contract schema")
        for key in ("projectId", "executionId", "input", "configSha256"):
            if contract[key] != self.request[key]:
                raise ValueError("resume " + key + " identity mismatch")
        if contract["settings"] != self.settings or contract["tools"] != self.actual_tools \
                or contract["helpers"] != self.actual_helpers or contract["brushArguments"] != self.brush_arguments():
            raise ValueError("resume tool/helper/semantic settings changed")
        dataset = self.root / "preparation" / "dataset"
        self.dataset_inventory = inventory(dataset)
        if self.dataset_inventory != contract["datasetInventory"]:
            raise ValueError("prepared dataset inventory changed")
        checkpoint = validate_checkpoint(self.root / "checkpoints", checkpoint_id, contract["runtimeIdentity"])
        if checkpoint != contract["checkpoint"]:
            raise ValueError("checkpoint metadata/inventory changed")
        self.runtime_identity = contract["runtimeIdentity"]
        self.restored_checkpoint = checkpoint
        self.prepared = read_json(self.root / "preparation" / "prepared-scene-identity.json")
        return dataset

    def trainer(self, dataset):
        """Translate matched durable protocol, never human logs, into candidates."""
        brush_control = self.attempt / "brush-control"
        store = self.root / "checkpoints"
        store.mkdir(exist_ok=True)
        brush_generation = "g" + str(self.generation) + "-" + uuid.uuid4().hex
        actual = admit(self.config["tools"]["brush"])
        argv = [actual["path"], str(dataset), *self.brush_arguments(), "--checkpoint-root", str(store),
                "--run-generation", brush_generation, "--control-directory", str(brush_control),
                "--export-path", str(self.attempt / "output")]
        resume = self.request["action"] == "resume"
        if resume:
            argv += ["--restore-checkpoint", self.request["resumeCheckpoint"]]
        environment = dict(os.environ, WGPU_BACKEND="vulkan", RUST_LOG="info", RUST_BACKTRACE="1")
        if "CUBECL_WGPU_DEFAULT_DEVICE" in environment:
            raise ValueError("default device override incompatible with owned checkpoint backend")
        entry = {"name": "brush", "argv": argv, "tool": actual, "cwd": str(self.attempt),
                 "environment": {k: environment[k] for k in ("WGPU_BACKEND", "RUST_LOG", "RUST_BACKTRACE")},
                 "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                 "budgetSeconds": self.budgets["brush"]}
        self.commands.append(entry)
        stdout, stderr = self.attempt / "brush.stdout.log", self.attempt / "brush.stderr.log"
        reader = JsonlReader(brush_control / "events.jsonl")
        begin = time.monotonic()
        ready = restored = restore_validated = completed = safe_exited = False
        pending = None
        ack = None
        protocol_failed = False
        last_sample = None

        def brush_command(request_id, command, **fields):
            value = {"requestId": request_id, "generation": brush_generation, "command": command, **fields}
            write_json(brush_control / "commands" / (request_id + ".json"), value)
            self.controls.append({"brushCommand": value, "publishedSequence": self.sequence})

        def save_failed(message, request_id=""):
            self.emit("SaveFailed", message, requestId=request_id)

        def accept_ack(event):
            nonlocal ack
            checkpoint = validate_checkpoint(store, event["checkpointId"], self.runtime_identity)
            if (event.get("durable") is not True or checkpoint["nextIteration"] != event.get("nextIteration")
                    or checkpoint["metadata"]["generation"] != brush_generation
                    or checkpoint["metadata"]["request_id"] != event["requestId"]):
                raise ValueError("Paused ACK does not bind committed checkpoint and control generation")
            if checkpoint["nextIteration"] > self.settings["brushSteps"]:
                raise ValueError("paused checkpoint cursor exceeds configured run")
            self.verify_source()
            if inventory(dataset) != self.dataset_inventory:
                raise ValueError("prepared dataset changed during training")
            contract = {"schemaVersion": 1, "projectId": self.request["projectId"],
                        "executionId": self.request["executionId"], "input": self.request["input"],
                        "configSha256": self.request["configSha256"], "settings": self.settings,
                        "tools": self.actual_tools, "helpers": self.actual_helpers,
                        "brushArguments": self.brush_arguments(), "datasetInventory": self.dataset_inventory,
                        "runtimeIdentity": self.runtime_identity, "checkpoint": checkpoint,
                        "pauseAck": event}
            contracts = self.root / "checkpoint-contracts"
            contracts.mkdir(exist_ok=True)
            contract_path = contracts / (event["checkpointId"] + ".json")
            if contract_path.exists():
                if read_json(contract_path) != contract:
                    raise ValueError("existing immutable checkpoint contract mismatch")
            else:
                write_json(contract_path, contract)
            self.resume_contract = file_identity(contract_path)
            self.resume_contract["relativePath"] = contract_path.relative_to(self.root).as_posix()
            self.checkpoint = checkpoint
            ack = event
            # Only validated durable ACK authorizes this exact matched SafeExit.
            brush_command("exit-" + event["requestId"], "SafeExit", pauseRequestId=event["requestId"],
                          checkpointId=event["checkpointId"])

        self.emit("StageStarted", "Brush restoring" if resume else "Brush training", stage="brush")
        try:
            with stdout.open("xb") as out, stderr.open("xb") as err:
                process = subprocess.Popen(argv, cwd=self.attempt, env=environment, stdin=subprocess.DEVNULL,
                                           stdout=out, stderr=err, shell=False)
                self.active_process = process
                entry["pid"] = process.pid
                while True:
                    self.guard(process, begin + self.budgets["brush"], (stdout, stderr))
                    try:
                        for event in reader.read():
                            self.brush_events.append(event)
                            if event.get("generation") != brush_generation:
                                raise ValueError("stale/mismatched Brush event generation")
                            name = event.get("event")
                            if name == "RunReady":
                                if ready:
                                    raise ValueError("duplicate RunReady")
                                identity = event["identity"]
                                keys = {"source_commit", "build_sha256", "config_sha256", "dataset_sha256", "backend", "device"}
                                if (set(identity) != keys or identity["build_sha256"] != actual["sha256"]
                                        or re.fullmatch(r"[0-9a-f]{40}", identity["source_commit"]) is None
                                        or any(re.fullmatch(r"[0-9a-f]{64}", identity[k]) is None
                                               for k in ("config_sha256", "dataset_sha256"))
                                        or identity["backend"] != "Vulkan/wgpu<wgsl>" or not identity["device"]):
                                    raise ValueError("invalid actual runtime identity")
                                if resume and identity != self.runtime_identity:
                                    raise ValueError("actual resume backend/device/source/semantic identity mismatch")
                                self.runtime_identity = identity
                                ready = True
                                if not resume:
                                    self.emit("BrushReady", "Brush control ready; Sample is pre-step observation",
                                              runtimeIdentity=identity)
                            elif name in ("RestoreValidated", "Restored"):
                                if (not resume or not ready or event.get("checkpointId") != self.request["resumeCheckpoint"]
                                        or event.get("nextIteration") != self.restored_checkpoint["nextIteration"]):
                                    raise ValueError("restore acknowledgement mismatch")
                                if name == "RestoreValidated":
                                    if restore_validated:
                                        raise ValueError("duplicate RestoreValidated")
                                    restore_validated = True
                                else:
                                    if not restore_validated or restored:
                                        raise ValueError("Restored out of order")
                                    restored = True
                                self.emit(name, name, checkpointId=event["checkpointId"], nextIteration=event["nextIteration"])
                                if restored:
                                    self.emit("BrushReady", "Fresh process restored complete trainer state", runtimeIdentity=self.runtime_identity)
                            elif name == "Sample":
                                iteration = event.get("iteration")
                                expected = (self.restored_checkpoint["nextIteration"] if resume else 0) if last_sample is None else last_sample + 1
                                if not ready or resume and not restored or type(iteration) is not int or iteration != expected \
                                        or event.get("sequence") != iteration or iteration >= self.settings["brushSteps"]:
                                    raise ValueError("Sample order/cursor mismatch")
                                last_sample = iteration
                                self.emit("Sample", "iteration=" + str(iteration) + " Sample before training step",
                                          iteration=iteration, sampleSequence=event["sequence"], view=event.get("view"))
                            elif name == "Saving":
                                if (pending is None or event.get("requestId") != pending["requestId"]
                                        or pending.get("saving") is not None):
                                    raise ValueError("unmatched Saving event")
                                pending["saving"] = event.get("nextIteration")
                                self.emit("Saving", "Saving full trainer state", requestId=event["requestId"],
                                          nextIteration=event.get("nextIteration"))
                            elif name == "SaveFailed":
                                if pending is None or event.get("requestId") != pending["requestId"]:
                                    raise ValueError("unmatched SaveFailed event")
                                save_failed("Brush save failed: " + str(event.get("reason")), event["requestId"])
                                pending = None
                            elif name == "Paused":
                                if (pending is None or event.get("requestId") != pending["requestId"] or protocol_failed
                                        or pending.get("saving") != event.get("nextIteration")
                                        or type(event.get("nextIteration")) is not int):
                                    raise ValueError("unmatched/untrusted Paused acknowledgement")
                                accept_ack(event)
                                pending["deadline"] = time.monotonic() + self.budgets["control"]
                            elif name == "SafeExit":
                                if (ack is None or event.get("requestId") != "exit-" + ack["requestId"]
                                        or event.get("checkpointId") != ack["checkpointId"]
                                        or event.get("nextIteration") != ack["nextIteration"]):
                                    raise ValueError("unmatched SafeExit acknowledgement")
                                safe_exited = True
                            elif name == "Completed":
                                if not ready or resume and not restored or ack is not None \
                                        or event.get("nextIteration") != self.settings["brushSteps"] \
                                        or (last_sample != self.settings["brushSteps"] - 1
                                            and not (resume and self.restored_checkpoint["nextIteration"] == self.settings["brushSteps"])):
                                    raise ValueError("invalid Completed event")
                                completed = True
                            else:
                                raise ValueError("unsupported/unexpected Brush protocol event: " + str(name))
                        for control in self.controls_ready():
                            if control["command"] == "ForceStop":
                                self.outcome = "Stopped"
                                raise InterruptedError("forced stop requested; native tree retirement required")
                            if protocol_failed or not ready or resume and not restored or pending is not None or completed:
                                save_failed("Pause unavailable or control already pending; trainer retained; force stop is available",
                                            control["requestId"])
                                continue
                            fields = {"atNextIteration": control["atNextIteration"]} if "atNextIteration" in control else {}
                            # Prefix leaves room for the matched SafeExit ID's 120-byte limit.
                            if len(control["requestId"]) > 100:
                                raise ValueError("pause requestId exceeds 100 characters")
                            brush_command(control["requestId"], "PauseAndSave", **fields)
                            pending = {"requestId": control["requestId"],
                                       "deadline": time.monotonic() + self.budgets["control"]}
                        if pending and time.monotonic() > pending["deadline"] and not safe_exited:
                            save_failed("Pause/SafeExit acknowledgement timed out; trainer retained; force stop is available", pending["requestId"])
                            pending = None
                            protocol_failed = True
                    except InterruptedError:
                        raise
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        # A live trainer may still own unsaved work or a paused
                        # boundary. Never exit/kill it on a save/protocol failure.
                        save_failed("Control/checkpoint validation failed: " + str(error)
                                    + "; trainer retained; force stop is available",
                                    pending["requestId"] if pending else "")
                        protocol_failed = True
                        pending = None
                    if process.poll() is not None:
                        # A final read on the next cycle consumes flushed exit events.
                        if reader.offset == (reader.path.stat().st_size if reader.path.exists() else 0):
                            break
                    time.sleep(0.03)
                self.active_process = None
                entry["exitCode"] = process.returncode
                if process.returncode != 0 or protocol_failed or reader.pending:
                    raise ValueError("Brush failed or exited with untrusted/incomplete protocol; inspect preserved logs")
                if resume and not (restore_validated and restored):
                    raise ValueError("fresh process did not acknowledge full restore")
                if safe_exited and ack is not None:
                    self.outcome = "Paused"
                    self.emit("PausedCandidate", "Durable checkpoint validated; native retirement still required",
                              requestId=ack["requestId"], checkpointId=ack["checkpointId"],
                              nextIteration=ack["nextIteration"])
                elif completed and ack is None:
                    self.verify_source()
                    if inventory(dataset) != self.dataset_inventory:
                        raise ValueError("prepared dataset changed during training")
                    # The fork pads to the digit count of total_steps, so its
                    # final export has the ordinary decimal total_steps suffix.
                    path = self.attempt / "output" / ("export_" + str(self.settings["brushSteps"]) + ".ply")
                    validation = validate_ply(path, self.settings["maxSplats"])
                    self.output = {"relativePath": path.relative_to(self.root).as_posix(),
                                   **file_identity(path), "validation": validation, "validationPassed": True}
                    self.outcome = "Completed"
                    self.emit("CompletedCandidate", "Actual completion and finite Gaussian PLY validated; native retirement still required",
                              output=self.output)
                else:
                    raise ValueError("Brush exit lacks matched SafeExit or real Completed")
        except Exception as error:
            entry["error"] = str(error)
            raise
        finally:
            entry["elapsedSeconds"] = time.monotonic() - begin
            write_json(self.attempt / "brush.command.json", entry)

    def execute(self):
        """Preserve a candidate on every recoverable failure; never publish success."""
        try:
            self.configure()
            dataset = self.prepare() if self.request["action"] == "start" else self.restore()
            self.trainer(dataset)
            return 0
        except Exception as error:
            self.failure = str(error)
            if self.outcome != "Stopped":
                self.outcome = "Failed"
            self.emit("Failed", self.failure, outcome=self.outcome)
            return 1
        finally:
            candidate = {"schemaVersion": 1, "projectId": self.request["projectId"],
                         "executionId": self.request["executionId"], "generation": self.generation,
                         "input": self.request["input"], "configSha256": self.request["configSha256"],
                         "outcome": self.outcome, "failure": self.failure, "commands": self.commands,
                         "controls": self.controls, "brushEvents": self.brush_events,
                         "settings": getattr(self, "settings", None), "tools": self.actual_tools,
                         "helpers": getattr(self, "actual_helpers", {}), "prepared": self.prepared,
                         "runtimeIdentity": self.runtime_identity, "checkpoint": self.checkpoint,
                         "checkpointId": self.checkpoint["checkpointId"] if self.checkpoint else None,
                         "resumeContract": self.resume_contract, "output": self.output,
                         "elapsedSeconds": time.monotonic() - self.started,
                         "nativeRetirementRequired": True}
            write_json(self.attempt / "result-candidate.json", candidate)


def main():
    """Native-host entry; nonzero exits never mean safe paused state."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True)
    args = parser.parse_args()
    try:
        return Runner(read_json(absolute(args.request))).execute()
    except Exception as error:
        print("Reconstruction bridge failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
