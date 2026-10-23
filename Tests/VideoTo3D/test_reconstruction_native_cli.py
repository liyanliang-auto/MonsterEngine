"""Native CLI restore rejection and actual FFprobe nonzero integration.

All local paths arrive via environment; clones and logs remain in round runtime.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest


def identity(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checkpoint_inventory(project):
    return {str(p.relative_to(project)): identity(p) for p in (project / "executions").rglob("*") if p.is_file() and "checkpoints" in p.parts}


class NativeRestoreRejection(unittest.TestCase):
    def setUp(self):
        self.source = Path(os.environ["MONSTER_REAL_RECONSTRUCTION_PROJECT"])
        self.root = Path(os.environ["MONSTER_RECONSTRUCTION_NEGATIVE_RUNTIME"])
        self.root.mkdir(parents=True, exist_ok=True)
        self.directory = self.root / hashlib.sha256(self._testMethodName.encode()).hexdigest()[:6]
        self.directory.mkdir()
        self.project = self.directory / "project"
        shutil.copytree(self.source, self.project, ignore=shutil.ignore_patterns("target"))
        self.config = Path(os.environ["MONSTER_REAL_RECONSTRUCTION_CONFIG"])
        self.cli = Path(os.environ["MONSTER_RECONSTRUCTION_CLI"])
        self.protected = {n: (self.project / n).read_bytes() for n in ("project.mstudio", "input.mstudio")}
        self.source_checkpoints = checkpoint_inventory(self.source)
        pointer = json.loads((self.project / "executions/latest.json").read_text(encoding="utf-8-sig"))
        self.execution = self.project / "executions" / pointer["executionId"]
        candidate = json.loads((self.execution / pointer["candidatePath"]).read_text(encoding="utf-8-sig"))
        self.checkpoint = self.execution / "checkpoints" / candidate["checkpointId"]

    def tearDown(self):
        for name, value in self.protected.items():
            self.assertEqual((self.project / name).read_bytes(), value)
        self.assertEqual(checkpoint_inventory(self.source), self.source_checkpoints)

    def run_cli(self, action="resume", config=None):
        argv = [str(self.cli), "--project", str(self.project), "--config", str(config or self.config), "--action", action]
        result = subprocess.run(argv, capture_output=True, timeout=90)
        (self.directory / "native.stdout.jsonl").write_bytes(result.stdout)
        (self.directory / "native.stderr.jsonl").write_bytes(result.stderr)
        (self.directory / "command-result.json").write_text(json.dumps({"argv": argv, "exitCode": result.returncode}))
        return result, result.stdout.decode("utf-8") + result.stderr.decode("utf-8")

    def test_corrupt_checkpoint_payload_rejected_preserved(self):
        payload = next((self.checkpoint / "payloads").iterdir())
        payload.write_bytes(payload.read_bytes() + b"corruption")
        before = checkpoint_inventory(self.project)
        result, output = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tensor length/hash mismatch", output)
        self.assertEqual(checkpoint_inventory(self.project), before)
        self.assertEqual(json.loads((self.project / "executions/latest.json").read_text(encoding="utf-8-sig"))["state"], "Paused")

    def test_corrupt_checkpoint_commit_rejected_preserved(self):
        marker = self.checkpoint / "COMMITTED.json"
        marker.write_text('{"id":"wrong","manifest_sha256":"' + '0' * 64 + '"}')
        before = checkpoint_inventory(self.project)
        result, output = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("commit marker/manifest mismatch", output)
        self.assertEqual(checkpoint_inventory(self.project), before)

    def test_dataset_change_rejected_preserved(self):
        image = next((self.execution / "preparation/dataset").rglob("*.jpg"))
        image.write_bytes(image.read_bytes() + b"changed")
        before = checkpoint_inventory(self.project)
        result, output = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("prepared dataset inventory changed", output)
        self.assertEqual(checkpoint_inventory(self.project), before)

    def test_incompatible_semantics_config_preserves_pause(self):
        config = json.loads(self.config.read_text(encoding="utf-8-sig"))
        config["settings"]["brushSeed"] += 1
        changed = self.directory / "changed-config.json"
        changed.write_text(json.dumps(config))
        pointer = (self.project / "executions/latest.json").read_bytes()
        result, output = self.run_cli(config=changed)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("configuration bytes changed", output)
        self.assertEqual((self.project / "executions/latest.json").read_bytes(), pointer)

    def test_missing_checkpoint_does_not_invent_restore(self):
        self.checkpoint.rename(self.directory / "retained-unselected-checkpoint")
        result, output = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('"state":"Completed"', output)
        self.assertIn("previous paused checkpoint retained", output)

    def test_unknown_metadata_schema_is_conservative(self):
        p = self.project / "executions/latest.json"
        value = json.loads(p.read_text(encoding="utf-8-sig"))
        value["schemaVersion"] = 999
        p.write_text(json.dumps(value))
        before = p.read_bytes()
        result, output = self.run_cli("inspect")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported or foreign execution metadata", output)
        self.assertEqual(p.read_bytes(), before)

    def test_unconfirmed_active_reopens_interrupted(self):
        p = self.project / "executions/latest.json"
        value = json.loads(p.read_text(encoding="utf-8-sig"))
        value["state"] = "Training"
        p.write_text(json.dumps(value))
        result, output = self.run_cli("inspect")
        self.assertEqual(result.returncode, 0)
        self.assertIn('"state":"Interrupted"', output)
        self.assertIn('"resumeAvailable":false', output)

    def test_actual_ffprobe_nonzero_stops_pipeline_preserves_input(self):
        # A fresh project with correctly imported malformed media passes input
        # integrity admission and reaches the actual pinned FFprobe executable.
        self.project = self.directory / "invalid-media-project"
        fixture = os.environ["MONSTER_INPUT_FIXTURE_CLI"]
        importer = os.environ["MONSTER_INPUT_CLI"]
        create = subprocess.run([fixture, "--create-project", str(self.project)], capture_output=True, timeout=30)
        self.assertEqual(create.returncode, 0)
        source = self.directory / "invalid_video.mov"
        source.write_bytes(b"ordinary imported bytes; deliberately invalid video")
        imported = subprocess.run([importer, "import-video", "--project", str(self.project), "--source", str(source)], capture_output=True, timeout=30)
        self.assertEqual(imported.returncode, 0)
        self.protected = {n: (self.project / n).read_bytes() for n in ("project.mstudio", "input.mstudio")}
        result, output = self.run_cli("start")
        self.assertNotEqual(result.returncode, 0)
        execution = next(p for p in (self.project / "executions").iterdir() if p.is_dir())
        command = json.loads((execution / "attempt-1/ffprobe.command.json").read_text(encoding="utf-8-sig"))
        self.assertNotEqual(command["exitCode"], 0)
        self.assertEqual(command["tool"]["sha256"], json.loads(self.config.read_text(encoding="utf-8-sig"))["tools"]["ffprobe"]["sha256"].lower())
        self.assertFalse((execution / "attempt-1/extract.command.json").exists())
        host = json.loads((execution / "attempt-1/host-result.json").read_text(encoding="utf-8-sig"))
        self.assertTrue(host["treeRetired"])
        self.assertEqual(source.read_bytes(), b"ordinary imported bytes; deliberately invalid video")

    def test_missing_configuration_preserves_existing_pause(self):
        pointer = (self.project / "executions/latest.json").read_bytes()
        result, output = self.run_cli("start", config=self.directory / "missing-config.json")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('"state":"Completed"', output)
        self.assertEqual((self.project / "executions/latest.json").read_bytes(), pointer)

    def test_photo_input_kind_rejected_without_launch(self):
        self.project = self.directory / "photos-project"
        fixture, importer = os.environ["MONSTER_INPUT_FIXTURE_CLI"], os.environ["MONSTER_INPUT_CLI"]
        created = subprocess.run([fixture,"--create-project",str(self.project)],capture_output=True,timeout=30)
        self.assertEqual(created.returncode,0)
        source = self.directory / "photos"
        source.mkdir()
        (source / "image.jpg").write_bytes(b"fixture JPEG input metadata only")
        imported = subprocess.run([importer,"import-photos","--project",str(self.project),"--source",str(source)],capture_output=True,timeout=30)
        self.assertEqual(imported.returncode,0)
        self.protected = {n:(self.project/n).read_bytes() for n in ("project.mstudio","input.mstudio")}
        result, output = self.run_cli("start")
        self.assertNotEqual(result.returncode,0)
        self.assertIn("requires exactly one queue-owned imported video",output)
        self.assertFalse((self.project / "executions").exists())

if __name__ == "__main__":
    unittest.main()


