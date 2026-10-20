"""Prepare an explicitly licensed video as an audited COLMAP Brush dataset.

Uses explicit tool paths, preserves originals, and refuses an existing output.
Suitability is a geometric admission check, not training or quality acceptance.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time


POLICY = {
    "minExtractedFrames": 24,
    "minRegisteredImages": 24,
    "minRegisteredFraction": 0.8,
    "minTemporalCoverage": 0.8,
    "minSparsePoints": 100,
    "maxMedianReprojectionErrorPixels": 2.0,
    "minPositiveObservedDepthFraction": 0.95,
    "minCameraSpanToSceneDiagonal": 0.0001,
}


def sha256(path):
    """Hash a file without retaining its contents in memory."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    """Write and flush a new evidence file; never replace existing evidence."""
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=True, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def inventory(root):
    """Return sorted relative paths, lengths and hashes, excluding symlinks."""
    result = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("symlink in prepared dataset: " + str(path))
        if path.is_file():
            result.append({"path": path.relative_to(root).as_posix(),
                           "bytes": path.stat().st_size, "sha256": sha256(path)})
    return result


def run(command, output, name, timeout):
    """Capture both streams and command identity, including failed commands."""
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    clock = time.monotonic()
    entry = {"command": [str(v) for v in command], "startedAtUtc": started}
    try:
        with (output / (name + ".stdout.log")).open("xb") as stdout, \
                (output / (name + ".stderr.log")).open("xb") as stderr:
            result = subprocess.run(entry["command"], stdout=stdout, stderr=stderr,
                                    timeout=timeout, check=False)
        entry["exitCode"] = result.returncode
    except (OSError, subprocess.SubprocessError) as error:
        entry["error"] = str(error)
        raise
    finally:
        entry["elapsedSeconds"] = time.monotonic() - clock
        write_json(output / (name + ".command.json"), entry)
    if result.returncode:
        raise ValueError("command failed: " + name + "; see preserved logs")


def rows(path):
    """Read non-comment COLMAP text lines, preserving empty observation lines."""
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if not line.startswith("#")]


def rotation(q):
    """Convert a finite unit COLMAP wxyz quaternion to a world-to-camera matrix."""
    if not all(math.isfinite(v) for v in q) or abs(sum(v*v for v in q) - 1) > 0.001:
        raise ValueError("invalid camera rotation")
    w, x, y, z = q
    return ((1-2*y*y-2*z*z, 2*x*y-2*w*z, 2*x*z+2*w*y),
            (2*x*y+2*w*z, 1-2*x*x-2*z*z, 2*y*z-2*w*x),
            (2*x*z-2*w*y, 2*y*z+2*w*x, 1-2*x*x-2*y*y))


def model_metrics(model, frame_names, require_pinhole=False):
    """Validate poses, intrinsics, points and observations; measure coverage."""
    cameras = {}
    for line in rows(model / "cameras.txt"):
        if not line:
            continue
        fields = line.split()
        params = list(map(float, fields[4:]))
        if fields[1] not in ("SIMPLE_RADIAL", "PINHOLE", "SIMPLE_PINHOLE"):
            raise ValueError("unsupported camera model: " + fields[1])
        if require_pinhole and fields[1] not in ("PINHOLE", "SIMPLE_PINHOLE"):
            raise ValueError("Brush dataset is not undistorted pinhole")
        width, height = int(fields[2]), int(fields[3])
        if width <= 0 or height <= 0 or not params or params[0] <= 0 \
                or not all(math.isfinite(v) for v in params):
            raise ValueError("invalid camera intrinsics")
        cameras[int(fields[0])] = fields[1]
    points = {}
    for line in rows(model / "points3D.txt"):
        if not line:
            continue
        fields = line.split()
        xyz = list(map(float, fields[1:4]))
        error = float(fields[7])
        if not all(math.isfinite(v) for v in xyz + [error]) or error < 0:
            raise ValueError("nonfinite point or reprojection error")
        points[int(fields[0])] = (xyz, error)
    image_rows = rows(model / "images.txt")
    if len(image_rows) % 2:
        raise ValueError("incomplete image/observation text pairs")
    names, centers = [], []
    positive, observations = 0, 0
    for i in range(0, len(image_rows), 2):
        fields = image_rows[i].split()
        if len(fields) != 10 or fields[9] not in frame_names:
            raise ValueError("invalid image row or unmatched image")
        if int(fields[8]) not in cameras or fields[9] in names:
            raise ValueError("unknown camera or duplicate image")
        matrix = rotation(list(map(float, fields[1:5])))
        translation = list(map(float, fields[5:8]))
        if not all(math.isfinite(v) for v in translation):
            raise ValueError("nonfinite camera translation")
        centers.append([-sum(matrix[j][k]*translation[j] for j in range(3))
                        for k in range(3)])
        names.append(fields[9])
        observed = image_rows[i+1].split()
        if len(observed) % 3:
            raise ValueError("invalid observation triples")
        for index in range(2, len(observed), 3):
            point_id = int(observed[index])
            if point_id == -1:
                continue
            if point_id not in points:
                raise ValueError("missing observed point")
            xyz = points[point_id][0]
            depth = sum(matrix[2][k]*xyz[k] for k in range(3)) + translation[2]
            observations += 1
            positive += depth > 0
    if not points or not centers:
        raise ValueError("empty model")
    def diagonal(values):
        return math.sqrt(sum((max(v[k] for v in values)-min(v[k] for v in values))**2
                             for k in range(3)))
    scene_diagonal = diagonal([p[0] for p in points.values()])
    selected = [frame_names.index(name) for name in names]
    return {"registeredImages": len(names), "extractedFrames": len(frame_names),
            "registeredFraction": len(names)/len(frame_names),
            "temporalCoverage": (max(selected)-min(selected))/max(1, len(frame_names)-1),
            "sparsePoints": len(points),
            "medianReprojectionErrorPixels": statistics.median(p[1] for p in points.values()),
            "positiveObservedDepthFraction": positive/max(1, observations),
            "observedPoints": observations, "cameraCenterSpan": diagonal(centers),
            "sceneDiagonal": scene_diagonal,
            "cameraSpanToSceneDiagonal": diagonal(centers)/max(scene_diagonal, 1e-30),
            "cameraModels": sorted(set(cameras.values())), "registeredNames": sorted(names)}


def admission(metrics):
    """Evaluate the immutable predeclared small-scene geometry thresholds."""
    checks = {
        "frameCount": metrics["extractedFrames"] >= POLICY["minExtractedFrames"],
        "registeredCount": metrics["registeredImages"] >= POLICY["minRegisteredImages"],
        "registration": metrics["registeredFraction"] >= POLICY["minRegisteredFraction"],
        "temporalCoverage": metrics["temporalCoverage"] >= POLICY["minTemporalCoverage"],
        "points": metrics["sparsePoints"] >= POLICY["minSparsePoints"],
        "reprojection": metrics["medianReprojectionErrorPixels"] <= POLICY["maxMedianReprojectionErrorPixels"],
        "depth": metrics["positiveObservedDepthFraction"] >= POLICY["minPositiveObservedDepthFraction"],
        "motion": metrics["cameraSpanToSceneDiagonal"] >= POLICY["minCameraSpanToSceneDiagonal"],
    }
    return {"passed": all(checks.values()), "checks": checks}


def prepare(args):
    """Extract, reconstruct, undistort and inventory a new local scene directory."""
    source = args.video.resolve(strict=True)
    provenance_path = args.provenance.resolve(strict=True)
    attribution_path = args.attribution.resolve(strict=True)
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    source_hash = sha256(source)
    if provenance.get("videoSha256", "").lower() != source_hash:
        raise ValueError("source video does not match provenance")
    if not provenance.get("license") or not provenance.get("sourcePage"):
        raise ValueError("explicit license and source page required")
    tools = {name: Path(getattr(args, name)).resolve(strict=True)
             for name in ("ffmpeg", "ffprobe", "colmap")}
    output = args.output.resolve()
    if output == source.parent or output in source.parents:
        raise ValueError("output must not contain source material")
    output.mkdir(parents=True, exist_ok=False)
    logs = output / "logs"
    logs.mkdir()
    report = {"schemaVersion": 1, "status": "FAILED", "sourceVideo": str(source),
              "sourceSha256": source_hash, "provenance": provenance,
              "provenanceSha256": sha256(provenance_path),
              "tools": {name: {"path": str(path), "sha256": sha256(path)}
                        for name, path in tools.items()},
              "preparationScriptSha256": sha256(Path(__file__)),
              "trainingAttempted": False, "productAdmission": "BLOCKED"}
    write_json(output / "suitability-policy.json", POLICY)
    shutil.copyfile(provenance_path, output / "provenance.json")
    shutil.copyfile(attribution_path, output / "ATTRIBUTION.txt")
    try:
        images = output / "frames"
        images.mkdir()
        run([tools["ffprobe"], "-v", "error", "-show_format", "-show_streams",
             "-of", "json", source], logs, "ffprobe", 60)
        run([tools["ffmpeg"], "-nostdin", "-n", "-i", source, "-vf",
             f"fps={args.fps},scale={args.width}:-2", "-q:v", "2",
             images / "frame_%05d.jpg"], logs, "extract", 300)
        frame_names = sorted(p.name for p in images.glob("*.jpg"))
        write_json(output / "frame-inventory.json", inventory(images))
        if len(frame_names) < POLICY["minExtractedFrames"]:
            raise ValueError("insufficient extracted frames")
        database = output / "database.db"
        run([tools["colmap"], "feature_extractor", "--database_path", database,
             "--image_path", images, "--ImageReader.single_camera", "1",
             "--ImageReader.camera_model", "SIMPLE_RADIAL", "--FeatureExtraction.use_gpu", "0",
             "--FeatureExtraction.num_threads", "4", "--default_random_seed", "0"],
            logs, "features", 900)
        run([tools["colmap"], "exhaustive_matcher", "--database_path", database,
             "--FeatureMatching.use_gpu", "0", "--FeatureMatching.num_threads", "4",
             "--default_random_seed", "0"], logs, "matching", 1800)
        sparse = output / "sparse"
        sparse.mkdir()
        run([tools["colmap"], "mapper", "--database_path", database, "--image_path", images,
             "--output_path", sparse, "--Mapper.num_threads", "4", "--default_random_seed", "0"],
            logs, "mapping", 1800)
        candidates = []
        for index, model in enumerate(sorted(p for p in sparse.iterdir() if p.is_dir())):
            text = output / ("model-text-" + str(index))
            text.mkdir()
            run([tools["colmap"], "model_converter", "--input_path", model,
                 "--output_path", text, "--output_type", "TXT"], logs, "convert-"+str(index), 120)
            metrics = model_metrics(text, frame_names)
            candidates.append({"model": str(model), "metrics": metrics, "admission": admission(metrics)})
        report["reconstructionCandidates"] = candidates
        if not candidates:
            raise ValueError("COLMAP produced no reconstruction")
        selected = max(candidates, key=lambda v: v["metrics"]["registeredImages"])
        if not selected["admission"]["passed"]:
            raise ValueError("largest model failed predeclared suitability thresholds")
        dataset = output / "dataset"
        dataset.mkdir()
        run([tools["colmap"], "image_undistorter", "--image_path", images,
             "--input_path", selected["model"], "--output_path", dataset,
             "--output_type", "COLMAP", "--max_image_size", str(args.width)],
            logs, "undistort", 600)
        text = output / "undistorted-text"
        text.mkdir()
        run([tools["colmap"], "model_converter", "--input_path", dataset / "sparse",
             "--output_path", text, "--output_type", "TXT"], logs, "convert-undistorted", 120)
        final_metrics = model_metrics(text, frame_names, require_pinhole=True)
        report["finalMetrics"] = final_metrics
        report["finalAdmission"] = admission(final_metrics)
        if not report["finalAdmission"]["passed"]:
            raise ValueError("undistorted model failed suitability")
        # Brush's pinned loader looks up lowercase points3d.bin specifically.
        points = dataset / "sparse" / "points3D.bin"
        points.rename(points.with_name("points3d.bin"))
        shutil.copyfile(attribution_path, dataset / "ATTRIBUTION.txt")
        (dataset / "MODIFICATIONS.txt").write_text(
            f"Derived from source video SHA256 {source_hash}.\n"
            f"FFmpeg extraction {args.fps} fps, width {args.width}; JPEG encoding.\n"
            "COLMAP reconstructed poses and sparse points; images undistorted to pinhole.\n"
            "Source video preserved unchanged. See parent provenance and command logs.\n",
            encoding="utf-8")
        report["datasetPath"] = str(dataset)
        report["datasetInventory"] = inventory(dataset)
        report["status"] = "PREPARED_GEOMETRY_ACCEPTED"
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
        raise
    finally:
        report["sourceUnchanged"] = sha256(source) == source_hash
        if not report["sourceUnchanged"]:
            report["status"] = "FAILED"
            report["failure"] = "source changed during preparation"
        report["recordedAtUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_json(output / "prepared-scene-identity.json", report)
    return report


def main():
    """Command-line entry point; never grant training acceptance from geometry."""
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("video", "provenance", "attribution", "ffmpeg", "ffprobe", "colmap", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--fps", type=float, default=3.0)
    parser.add_argument("--width", type=int, default=960)
    args = parser.parse_args()
    try:
        if not math.isfinite(args.fps) or not 0 < args.fps <= 30 or not 64 <= args.width <= 4096:
            raise ValueError("invalid extraction bounds")
        report = prepare(args)
        if report["status"] != "PREPARED_GEOMETRY_ACCEPTED":
            raise ValueError(report.get("failure", "preparation failed"))
        print("Scene geometry prepared; training remains NOT_VERIFIED: " + report["datasetPath"])
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("Scene preparation failed; evidence preserved: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
