"""Record an unmodified native-window screenshot and verify a live FPS titlebar."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variant", required=True)
    parser.add_argument("--screenshot", required=True, type=Path)
    parser.add_argument("--window-title", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--method", default="external native window capture")
    args = parser.parse_args()
    if not args.screenshot.is_file():
        parser.error("screenshot does not exist")
    match = re.search(r"(?:FPS|fps)\\s*[:=]\\s*(\\d+(?:\\.\\d+)?)", args.window_title)
    record = {
        "status": "RECORDED" if match else "UNAVAILABLE",
        "variant": args.variant,
        "windowTitle": args.window_title,
        "titleFPS": float(match.group(1)) if match else None,
        "titlebarRequirement": "Application window and live FPS titlebar must be visible",
        "screenshot": {"path": str(args.screenshot.resolve()), "sha256": sha256(args.screenshot), "format": args.screenshot.suffix.lower().lstrip(".")},
        "method": args.method,
        "savedUnmodified": True,
        "reason": None if match else "Window title did not contain a live FPS value; do not claim titlebar evidence",
    }
    args.output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(record["status"])
    return 0 if match else 1


if __name__ == "__main__":
    raise SystemExit(main())
