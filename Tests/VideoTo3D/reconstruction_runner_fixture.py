"""Test-only pinned runner for actual native lifecycle/error seams. No successes."""
import json
from pathlib import Path
import subprocess
import sys
import time

request = json.loads(Path(sys.argv[-1]).read_text(encoding="utf-8-sig"))
config = json.loads(Path(request["configPath"]).read_text(encoding="utf-8-sig"))
mode = config["fixtureMode"]
attempt = Path(request["controlDirectory"]).parent
sequence = 0

def emit(kind, message, **fields):
    global sequence
    sequence += 1
    record = dict(schemaVersion=1, executionId=request["executionId"], generation=request["generation"], sequence=sequence, kind=kind, message=message, **fields)
    if mode == "stale":
        record["generation"] += 1
    with (attempt / "events.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(record) + "\n")
        stream.flush()

if mode == "nonzero":
    emit("Failed", "deliberate helper nonzero failure")
    candidate = dict(schemaVersion=1, projectId=request["projectId"], executionId=request["executionId"], generation=request["generation"], configSha256=request["configSha256"], input=request["input"], outcome="Failed", failure="nonzero")
    (attempt / "result-candidate.json").write_text(json.dumps(candidate))
    sys.exit(17)
child = subprocess.Popen([sys.executable, "-I", "-B", "-c", "import time; time.sleep(60)"])
(attempt / "descendant.pid").write_text(str(child.pid))
emit("StageStarted", "test descendant running", stage="helper-live")
if mode not in ("prepare", "timeout"):
    emit("BrushReady", "test-only native control seam ready")
seen = set()
end = time.monotonic() + 45
while time.monotonic() < end:
    for control in Path(request["controlDirectory"]).glob("*.json"):
        if control.name in seen:
            continue
        seen.add(control.name)
        value = json.loads(control.read_text())
        if value["command"] == "PauseAndSave":
            if mode == "wrong":
                emit("Saving", "wrong control identity", requestId="foreign-request")
            else:
                emit("Saving", "test save pending", requestId=value["requestId"])
                time.sleep(0.1)
                emit("SaveFailed", "deliberate save failure; trainer/controller retained", requestId=value["requestId"])
    time.sleep(0.01)
sys.exit(19)
