"""Bridge validation unit cases; runtime root is explicitly supplied locally."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("reconstruction_runner", ROOT / "Tools/VideoTo3D/reconstruction_runner.py")
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)

class BridgeValidation(unittest.TestCase):
    def setUp(self):
        root = Path(os.environ["MONSTER_RECONSTRUCTION_PY_RUNTIME"])
        root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_unambiguous_finite_json(self):
        for raw in (b'{"a":1,"a":2}', b'{"x":NaN}', b'{"x":1e9999}', b'{}extra'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                r.decode_json(raw)
        self.assertEqual(r.decode_json(b'\xef\xbb\xbf{"generation":1}'), {"generation": 1})

    def test_pin_failure_preserves_bytes(self):
        p = self.root / "tool.bin"
        p.write_bytes(b"actual immutable file")
        before = p.read_bytes()
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            r.admit({"path": str(p), "sha256": "0" * 64})
        self.assertEqual(p.read_bytes(), before)
        self.assertEqual(r.admit({"path": str(p), "sha256": r.sha256(p)})["bytes"], len(before))

    def test_no_path_fallback_or_traversal(self):
        for path in ("../input", "a/../b", "a\\b", "C:/absolute", "a//b", ""):
            with self.subTest(path=path), self.assertRaises(ValueError):
                r.within(self.root, path)
        for token in ("..", "CON", "com1", "a/b", "", "a" * 121):
            self.assertFalse(r.token(token))
        with self.assertRaises(ValueError):
            r.absolute("python.exe")

    def test_atomic_records_never_overwrite(self):
        p = self.root / "checkpoint-contract.json"
        r.write_json(p, {"generation": 1})
        before = p.read_bytes()
        with self.assertRaises((ValueError, FileExistsError)):
            r.write_json(p, {"generation": 2})
        self.assertEqual(before, p.read_bytes())

    def test_jsonl_partial_record_and_truncation(self):
        p = self.root / "events.jsonl"
        p.write_bytes(b'{"sequence":1}\n{"sequence":')
        reader = r.JsonlReader(p)
        self.assertEqual(reader.read(), [{"sequence": 1}])
        with p.open("ab") as f:
            f.write(b'2}\n')
        self.assertEqual(reader.read(), [{"sequence": 2}])
        p.write_bytes(b'{}\n')
        with self.assertRaisesRegex(ValueError, "truncated"):
            reader.read()

    def ply(self, value=0.0):
        props = "x y z f_dc_0 f_dc_1 f_dc_2 opacity scale_0 scale_1 scale_2 rot_0 rot_1 rot_2 rot_3".split()
        p = self.root / "export.ply"
        header = "ply\nformat binary_little_endian 1.0\nelement vertex 1\n" + "".join("property float " + n + "\n" for n in props) + "end_header\n"
        p.write_bytes(header.encode("ascii") + struct.pack("<14f", *([value] * 14)))
        return p

    def test_finite_gaussian_export(self):
        p = self.ply()
        self.assertTrue(r.validate_ply(p, 1)["allFinite"])
        for value in (float("nan"), float("inf")):
            self.ply(value)
            with self.assertRaisesRegex(ValueError, "nonfinite"):
                r.validate_ply(p, 1)

    def test_export_size_and_budget_rejection(self):
        p = self.ply()
        with self.assertRaises(ValueError):
            r.validate_ply(p, 0)
        before = p.read_bytes()
        for data in (before[:-1], before + b"extra"):
            p.write_bytes(data)
            with self.assertRaises(ValueError):
                r.validate_ply(p, 1)

    def test_invalid_checkpoint_does_not_mutate(self):
        cp = self.root / "cp-test"
        cp.mkdir()
        marker = cp / "COMMITTED.json"
        marker.write_text(json.dumps({"id": "cp-test", "manifest_sha256": "0" * 64}))
        manifest = cp / "manifest.json"
        manifest.write_text("{}")
        before = {p.name: p.read_bytes() for p in cp.iterdir()}
        with self.assertRaisesRegex(ValueError, "commit marker"):
            r.validate_checkpoint(self.root, "cp-test", {})
        self.assertEqual(before, {p.name: p.read_bytes() for p in cp.iterdir()})

    def protocol_failure_keeps_trainer(self, mode):
        from unittest.mock import patch
        import time
        runner = r.Runner.__new__(r.Runner)
        runner.root = self.root
        runner.attempt = self.root / "attempt-1"
        runner.attempt.mkdir()
        runner.generation = 1
        runner.request = {"action": "start", "executionId": "execution", "generation": 1}
        runner.sequence = 0
        runner.commands, runner.controls, runner.brush_events = [], [], []
        runner.settings = {"brushSteps": 600}
        runner.brush_arguments = lambda: []
        runner.budgets = {"brush": 30, "control": 0.01}
        tool = self.root / "brush-fixture.bin"
        tool.write_bytes(b"test-only process seam")
        runner.config = {"tools": {"brush": {"path": str(tool), "sha256": r.sha256(tool)}}}
        sentinel = self.root / "checkpoints" / "old-valid-sentinel"
        sentinel.parent.mkdir()
        sentinel.write_bytes(b"old checkpoint must remain")
        observed = []
        def emit(kind, message, **fields):
            runner.sequence += 1
            observed.append((kind, message, fields))
        runner.emit = emit
        controls = [[{"command": "PauseAndSave", "requestId": "pause-1"}]]
        runner.controls_ready = lambda: controls.pop() if controls else []
        control_root = runner.attempt / "brush-control"
        generation = []
        class Process:
            pid = 1234
            def poll(self):
                return None
        process = Process()
        def event(value):
            with (control_root / "events.jsonl").open("a") as f:
                f.write(json.dumps(value) + "\n")
        def launch(argv, **kwargs):
            generation.append(argv[argv.index("--run-generation") + 1])
            (control_root / "commands").mkdir(parents=True)
            identity = {"source_commit": "1" * 40, "build_sha256": r.sha256(tool), "config_sha256": "2" * 64, "dataset_sha256": "3" * 64, "backend": "Vulkan/wgpu<wgsl>", "device": "test-only"}
            event({"event": "RunReady", "generation": generation[0], "identity": identity})
            if mode == "completion":
                for iteration in range(600):
                    event({"event": "Sample", "generation": generation[0], "iteration": iteration, "sequence": iteration})
                event({"event": "Completed", "generation": generation[0], "nextIteration": 600})
            return process
        injected = []
        def guard(active, deadline, logs):
            self.assertIs(active, process)
            if any(v[0] == "SaveFailed" for v in observed):
                self.assertIsNone(active.poll(), "failure retired trainer unexpectedly")
                raise InterruptedError("explicit test force-stop after observed error")
            if (control_root / "commands/pause-1.json").exists() and not injected:
                injected.append(True)
                if mode == "missing":
                    time.sleep(0.02)
                elif mode == "savefailed":
                    event({"event": "SaveFailed", "generation": generation[0], "requestId": "pause-1", "reason": "disk failure"})
                elif mode == "wrong":
                    event({"event": "Paused", "generation": generation[0], "requestId": "foreign", "nextIteration": 101, "checkpointId": "cp-fake", "durable": True})
                elif mode == "stale":
                    event({"event": "Paused", "generation": "old-generation", "requestId": "pause-1", "nextIteration": 101, "checkpointId": "cp-fake", "durable": True})
        runner.guard = guard
        with patch.object(r.subprocess, "Popen", launch), self.assertRaises(InterruptedError):
            runner.trainer(self.root / "dataset")
        self.assertTrue(any(v[0] == "SaveFailed" for v in observed))
        self.assertFalse(any(v[0] in ("PausedCandidate", "CompletedCandidate") for v in observed))
        self.assertFalse(any("exit-" in p.name for p in (control_root / "commands").iterdir()))
        self.assertEqual(sentinel.read_bytes(), b"old checkpoint must remain")

    def test_missing_pause_ack_retains_live_trainer(self):
        self.protocol_failure_keeps_trainer("missing")

    def test_wrong_pause_ack_retains_live_trainer(self):
        self.protocol_failure_keeps_trainer("wrong")

    def test_stale_pause_ack_retains_live_trainer(self):
        self.protocol_failure_keeps_trainer("stale")

    def test_completion_racing_pause_never_publishes_paused(self):
        self.protocol_failure_keeps_trainer("completion")

    def test_savefailed_retains_live_trainer(self):
        self.protocol_failure_keeps_trainer("savefailed")

if __name__ == "__main__":
    unittest.main()

