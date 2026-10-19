"""Real file fixtures for exact-source bounded APK staging contracts."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("prepare_assets", Path(__file__).with_name("prepare_assets.py"))
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class BoundedAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scene.mgs").write_bytes(b"manifest")

    def entry(self, name="scene.mgs"):
        path = self.root / name
        return {"path": name, "bytes": path.stat().st_size, "sha256": assets.digest(path)}

    def test_confined_manifest_and_pages_pass(self):
        (self.root / "pages").mkdir()
        (self.root / "pages/0.bin").write_bytes(b"real page fixture")
        self.assertEqual(len(list(assets.checked_files(self.root, [self.entry(), self.entry("pages/0.bin")]))), 2)

    def test_missing_or_duplicate_manifest_rejected(self):
        for entries in ([], [self.entry(), self.entry()]):
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                list(assets.checked_files(self.root, entries))

    def test_unsafe_and_missing_asset_rejected(self):
        for name in ("../scene.mgs", "C:/outside", "sub\\page", "/outside", "missing"):
            entry = {"path": name, "bytes": 0, "sha256": "0" * 64}
            with self.subTest(name=name), self.assertRaises(ValueError):
                list(assets.checked_files(self.root, [self.entry(), entry]))

    def test_size_hash_and_per_asset_cap_rejected(self):
        for change in ({"bytes": 0}, {"bytes": 67108865}, {"sha256": "0" * 64}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                list(assets.checked_files(self.root, [{**self.entry(), **change}]))

    def test_exact_source_hash_count_and_input_preservation(self):
        path = self.root / "exact.ply"
        original = b"ply\nformat ascii 1.0\nelement vertex 1244819\nend_header\nfixture\n"
        path.write_bytes(original)
        expected = {"path": "exact.ply", "bytes": len(original), "sha256": assets.digest(path), "records": 1244819}
        self.assertEqual(assets.exact_source(self.root, expected), path)
        for change in ({"bytes": 0}, {"sha256": "0" * 64}, {"records": 30000}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                assets.exact_source(self.root, {**expected, **change})
            self.assertEqual(path.read_bytes(), original)

    def test_manifest_native_source_tool_config_and_content_bound(self):
        identity = {"source": {"sha256": "a" * 64}, "builderSHA256": "b" * 64, "config": {"pageCapacity": 8192}}
        fields = ["a" * 64, "MonsterEngine-MobileLOD-1", "b" * 64, "parser", '{"pageCapacity":8192}']
        data = struct.pack("<III", 0x4D53474D, 3, 0)
        for field in fields:
            value = field.encode(); data += struct.pack("<I", len(value)) + value
        (self.root / "scene.mgs").write_bytes(data)
        validation = {"contentSHA256": assets.digest(self.root / "scene.mgs")}
        assets.validate_lod_identity(self.root, identity, validation)
        for key, value in (("source", {"sha256": "c" * 64}), ("builderSHA256", "d" * 64), ("config", {"pageCapacity": 1})):
            changed = copy.deepcopy(identity); changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                assets.validate_lod_identity(self.root, changed, validation)
        with self.assertRaises(ValueError):
            assets.validate_lod_identity(self.root, identity, {"contentSHA256": "0" * 64})

    def test_manifest_unknown_version_and_oversized_identity_fail(self):
        for content in (struct.pack("<III", 0x4D53474D, 4, 0), struct.pack("<IIII", 0x4D53474D, 3, 0, 65537)):
            (self.root / "scene.mgs").write_bytes(content)
            with self.assertRaises(ValueError):
                assets.validate_lod_identity(self.root, {}, {})

    def staging_fixture(self):
        source = self.root / "source"; config_dir = source / "resources/android"
        config_dir.mkdir(parents=True)
        ply = source / "exact.ply"; ply.write_bytes(b"ply\nelement vertex 1\nend_header\nfixture")
        expected = {"path": "exact.ply", "bytes": ply.stat().st_size, "sha256": assets.digest(ply), "records": 1}
        config = {"pageCapacity": 8192}
        policy = {"builtinScene": {"source": expected, "buildConfig": config}}
        (config_dir / "default-viewer.json").write_text(json.dumps(policy))
        (config_dir / "builtin-scene-license.txt").write_text("External fixture; rights unresolved")
        lod = self.root / "lod"; scene = lod / "key/scene"; scene.mkdir(parents=True)
        data = struct.pack("<III", 0x4D53474D, 3, 0)
        for text in [expected["sha256"], "MonsterEngine-MobileLOD-1", "b" * 64, "parser", json.dumps(config)]:
            value = text.encode(); data += struct.pack("<I", len(value)) + value
        (scene / "scene.mgs").write_bytes(data)
        (scene / "page.mgp").write_bytes(b"bounded fixture page")
        inventory = [{"path": p.name, "bytes": p.stat().st_size, "sha256": assets.digest(p)} for p in scene.iterdir()]
        identity = {"schemaVersion": 1, "source": expected, "config": config, "builderSHA256": "b" * 64,
                    "sceneDirectory": "key/scene", "validation": {"ok": True, "sourceRecords": 1,
                    "contentSHA256": assets.digest(scene / "scene.mgs")}, "files": inventory}
        (lod / "builtin-lod.json").write_text(json.dumps(identity))
        shader = self.root / "shaders"; shader.mkdir(); (shader / "test.spv").write_bytes(b"shader identity fixture")
        (shader / "shader-manifest.json").write_text(json.dumps({"schemaVersion": 1,
            "shaders": [{"path": "test.spv", "sha256": assets.digest(shader / "test.spv")}]}))
        output = self.root / "staged"; output.mkdir(); (output / "obsolete.ply").write_bytes(b"old raw input")
        command = [sys.executable, str(Path(assets.__file__)), "--source-root", str(source),
                   "--shader-dir", str(shader), "--lod-dir", str(lod), "--output", str(output)]
        return command, scene, output, lod

    def test_complete_staging_publishes_inventory_and_removes_obsolete_raw(self):
        command, _, output, _ = self.staging_fixture()
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        inventory = json.loads((output / "asset-manifest.json").read_text())["assets"]
        self.assertFalse((output / "obsolete.ply").exists())
        self.assertTrue((output / "scenes/bonsai-lod/page.mgp").is_file())
        for entry in inventory:
            self.assertEqual(assets.digest(output / entry["path"]), entry["sha256"])
        self.assertFalse(any(p.suffix == ".ply" for p in output.rglob("*")))

    def test_corrupted_page_or_stale_config_fails_before_publication(self):
        command, scene, output, lod = self.staging_fixture()
        sentinel = output / "asset-manifest.json"; sentinel.write_text("retained published inventory")
        before = sentinel.read_bytes()
        (scene / "page.mgp").write_bytes(b"corrupted page")
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(sentinel.read_bytes(), before)
        identity_path = lod / "builtin-lod.json"; identity = json.loads(identity_path.read_text())
        identity["config"]["pageCapacity"] = 1; identity_path.write_text(json.dumps(identity))
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(sentinel.read_bytes(), before)

    def test_host_publication_marker_is_excluded_from_runtime_assets(self):
        command, scene, output, lod = self.staging_fixture()
        marker = scene / ".monster-publication"; marker.write_bytes(b"host publication identity")
        identity_path = lod / "builtin-lod.json"; identity = json.loads(identity_path.read_text())
        identity["files"].append({"path": marker.name, "bytes": marker.stat().st_size, "sha256": assets.digest(marker)})
        identity_path.write_text(json.dumps(identity))
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((output / "scenes/bonsai-lod/.monster-publication").exists(),
                         "Host publication marker is not an admitted JNI runtime scene file")


if __name__ == "__main__":
    unittest.main(verbosity=2)
