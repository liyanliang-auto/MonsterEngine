#!/usr/bin/env python3
"""Install hash-pinned local profilers and preserve compatibility evidence."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("profiling_tools.lock.json")
DEFAULT_CACHE = ROOT / "build" / "profiling-tools"


def sha256(path):
    """Return a streaming SHA256 of a local artifact."""
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    """Write an evidence object; callers choose non-raw output locations."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def run(command, timeout=120):
    """Run an argument vector with bounded runtime and record failures explicitly."""
    try:
        p = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=timeout)
        return {"command": [str(x) for x in command], "exitCode": p.returncode,
                "stdout": p.stdout, "stderr": p.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": [str(x) for x in command], "exitCode": None, "error": str(error)}


def checked(command, timeout=120):
    """Return stdout or raise with the complete command failure evidence."""
    result = run(command, timeout)
    if result.get("exitCode") != 0:
        raise RuntimeError(json.dumps(result))
    return result["stdout"]


def lock_data():
    """Read the committed tool identity lock."""
    return json.loads(LOCK.read_text(encoding="utf-8"))


def tool(cache, name):
    """Resolve and hash-verify the pinned trace processor or Tracy import CLI."""
    cache = Path(cache)
    lock = lock_data()
    artifact = cache / lock[name]["file"]
    if sha256(artifact) != lock[name]["sha256"]:
        raise ValueError("Pinned artifact hash mismatch: " + str(artifact))
    if name == "perfetto":
        return artifact
    matches = sorted((cache / "tracy-0.14.1").rglob("*import*chrome*.exe"))
    if len(matches) != 1:
        raise ValueError("Expected exactly one Tracy import-chrome executable")
    # Verify the executable against the pinned archive, not just its directory name.
    relative = matches[0].relative_to(cache / "tracy-0.14.1").as_posix()
    with zipfile.ZipFile(artifact) as archive:
        if hashlib.sha256(archive.read(relative)).hexdigest() != sha256(matches[0]):
            raise ValueError("Extracted Tracy executable hash mismatch")
    return matches[0]


def query(trace, sql, cache=DEFAULT_CACHE, max_rows=100000):
    """Execute one bounded read-only SELECT through pinned Perfetto 48.1."""
    if not isinstance(sql, str) or len(sql) > 65536 or not 1 <= max_rows <= 250000:
        raise ValueError("Invalid SQL or row bound")
    # SELECT-only grammar prevents mutations, ATTACH, scripts and pragma actions.
    import re
    sql = sql.strip()
    if sql.endswith(";"):
        sql = sql[:-1].rstrip()
    if not re.match(r"(?is)^select\s", sql):
        raise ValueError("Only a single SELECT statement is supported")
    quote = None
    index = 0
    while index < len(sql):
        char = sql[index]
        if quote is not None:
            if char == quote:
                if quote != "]" and index + 1 < len(sql) and sql[index + 1] == quote:
                    index += 2
                    continue
                quote = None
        elif char in "'\"`[":
            quote = "]" if char == "[" else char
        elif char == ";" or sql.startswith(("--", "/*"), index):
            raise ValueError("Only a single SELECT statement without comments is supported")
        index += 1
    if quote is not None:
        raise ValueError("Unterminated SQL quoted literal or identifier")
    trace = Path(trace).resolve(strict=True)
    result = run([tool(cache, "perfetto"), "-Q", "SELECT * FROM (" + sql + ") LIMIT " + str(max_rows + 1), trace])
    if result.get("exitCode") != 0:
        raise RuntimeError(json.dumps(result))
    rows = list(csv.DictReader(io.StringIO(result["stdout"])))
    return {"rows": rows[:max_rows], "truncated": len(rows) > max_rows,
            "traceSha256": sha256(trace), "processorVersion": "48.1",
            "command": result["command"], "stderr": result["stderr"]}


def install(cache):
    """Download only exact locked artifacts; reject hashes before extraction."""
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    results = []
    for name in ("perfetto", "tracy"):
        item = lock_data()[name]
        destination = cache / item["file"]
        if not destination.exists():
            temporary = destination.with_suffix(destination.suffix + ".partial")
            try:
                with urllib.request.urlopen(item["url"], timeout=120) as src, temporary.open("wb") as out:
                    while block := src.read(1 << 20):
                        out.write(block)
                if sha256(temporary) != item["sha256"]:
                    raise ValueError("Downloaded artifact hash mismatch: " + name)
                temporary.replace(destination)
            finally:
                temporary.unlink(missing_ok=True)
        if sha256(destination) != item["sha256"]:
            raise ValueError("Cached artifact hash mismatch: " + name)
        if name == "tracy":
            target = (cache / "tracy-0.14.1").resolve()
            with zipfile.ZipFile(destination) as archive:
                for entry in archive.infolist():
                    if not (target / entry.filename).resolve().is_relative_to(target):
                        raise ValueError("Unsafe archive member")
                archive.extractall(target)
        results.append({"tool": name, "version": item["version"], "artifact": str(destination),
                        "sha256": sha256(destination), "executable": str(tool(cache, name))})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("version", "install", "probe"))
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "version":
            result = lock_data()
        elif args.action == "install":
            result = {"installed": install(args.cache)}
        else:
            result = {"perfetto": run([tool(args.cache, "perfetto"), "--version"]),
                      "tracy": run([tool(args.cache, "tracy")]), "lock": lock_data(),
                      "note": "Tracy no-argument usage can exit nonzero; actual import proves compatibility."}
        if args.output:
            write_json(args.output, result)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        result = {"error": str(error)}
        if args.output:
            write_json(args.output, result)
        print(json.dumps(result), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
