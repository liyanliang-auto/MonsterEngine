"""Analyze a recorded Bicycle P03/P02 manifest without inventing unavailable evidence."""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def image_metrics(left, right):
    require(len(left) == len(right) and len(left) % 4 == 0, "RGBA capture size mismatch")
    changed_pixels = 0
    changed_channels = 0
    total = 0
    squared = 0
    maximum = 0
    for index in range(0, len(left), 4):
        pixel_changed = False
        for channel in range(4):
            difference = abs(left[index + channel] - right[index + channel])
            total += difference
            squared += difference * difference
            maximum = max(maximum, difference)
            if difference:
                changed_channels += 1
                pixel_changed = True
        changed_pixels += int(pixel_changed)
    count = len(left)
    mse = squared / count / (255 * 255)
    return {
        "meanAbsoluteError": total / count / 255,
        "maximumAbsoluteError": maximum / 255,
        "rmse": math.sqrt(mse),
        "changedPixels": changed_pixels,
        "changedChannels": changed_channels,
        "channelCount": count,
        "psnrDb": None if mse == 0 else 10 * math.log10(1 / mse),
    }


def compare_captures(first, second):
    first_meta = read_json(first / "capture_000.json")
    second_meta = read_json(second / "capture_000.json")
    for key in ("width", "height", "format", "colorSpace", "view", "projection", "cameraPosition", "focal"):
        require(first_meta.get(key) == second_meta.get(key), "Capture identity mismatch: " + key)
    left = (first / "capture_000.rgba").read_bytes()
    right = (second / "capture_000.rgba").read_bytes()
    metrics = image_metrics(left, right)
    metrics["width"] = first_meta.get("width")
    metrics["height"] = first_meta.get("height")
    metrics["status"] = "PASS" if metrics["meanAbsoluteError"] <= 1 / 255 and metrics["maximumAbsoluteError"] <= 16 / 255 else "FAIL"
    return metrics


def summarize_variant(directory):
    metadata_path = directory / "metadata.json"
    if not metadata_path.exists():
        return {"status": "UNAVAILABLE", "reason": "metadata.json was not produced"}
    metadata = read_json(metadata_path)
    details = metadata.get("metadata", metadata)
    result = {"status": metadata.get("status", "UNKNOWN"), "metadata": details}
    frames = directory / "frames.csv"
    if frames.exists():
        import csv
        with frames.open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        sampled = [row for row in rows if row.get("sampled") == "1" and row.get("frame_ms")]
        if sampled:
            intervals = [float(row["frame_ms"]) for row in sampled]
            result["sampleCount"] = len(intervals)
            result["meanFrameTimeMs"] = sum(intervals) / len(intervals)
            result["fps"] = 1000 / result["meanFrameTimeMs"]
    return result


def analyze(manifest_path):
    manifest = read_json(manifest_path)
    require(manifest.get("status") == "RECORDED", "Run manifest is incomplete")
    require(manifest.get("qualityGate", {}).get("representation") == "normalized linear RGB RGBA8_UNORM", "Unexpected image representation")
    for item in manifest.get("identity", []):
        require(Path(item["path"]).exists(), "Missing experiment input: " + item["path"])
        require(sha256(item["path"]) == item["sha256"], "Experiment input changed: " + item["path"])
    variants = {}
    for record in manifest.get("variants", []):
        directory = Path(record["directory"])
        variants.setdefault(record["variant"], {})[record["kind"]] = summarize_variant(directory)
        if record["kind"] == "capture" and (directory / "capture_000.rgba").exists():
            variants[record["variant"]]["capturePath"] = str(directory)
    ordered = [name for name in ("baseline", "p03", "p02") if name in variants]
    result = {"status": "PASS", "scope": manifest["scope"], "p03Degree": manifest["p03Degree"], "variants": variants, "comparisons": {}, "titlebarEvidence": "REQUIRED: record matching native windows separately"}
    for left, right, label in (("baseline", "p03", "P03"), ("p03", "p02", "P02")):
        if left in variants and right in variants and "capturePath" in variants[left] and "capturePath" in variants[right]:
            result["comparisons"][label] = compare_captures(Path(variants[left]["capturePath"]), Path(variants[right]["capturePath"]))
        else:
            result["comparisons"][label] = {"status": "UNAVAILABLE", "reason": "GPU captures missing; no visual equivalence claim"}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.manifest)
    except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError) as error:
        result = {"status": "FAIL", "errors": [str(error)]}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(result["status"])
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
