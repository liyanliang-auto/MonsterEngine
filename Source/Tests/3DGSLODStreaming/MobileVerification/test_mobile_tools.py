"""Real host tool integration: no mocked native validator or renderer."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / "Tools"
VALIDATOR = Path(os.environ.get("MONSTER_MOBILE_VALIDATOR", ROOT / "out/mobile-verification/bin/Release/SplatMobileLODBuilder.exe"))
EVIDENCE = Path(os.environ.get("MONSTER_TEST_OUTPUT", ROOT / "out/mobile-verification/evidence"))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MobileToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="mobile-tools-", dir=EVIDENCE)
        cls.dir = Path(cls.tmp.name)
        cls.generator = load("monster_builtin_generator", TOOLS / "Android/generate_builtin_scene.py")
        cls.package = TOOLS / "SplatScenePackage/main.py"
        source = cls.dir / "source.ply"
        names = ("x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                 "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3")
        with source.open("wb") as stream:
            stream.write(("ply\nformat binary_little_endian 1.0\nelement vertex 12\n" +
                          "".join("property float " + key + "\n" for key in names) + "end_header\n").encode())
            for i in range(12):
                stream.write(struct.pack("<14f", i, 0, -10, .1, .2, .3, 0, -2, -2, -2, 1, 0, 0, 0))
        config = cls.dir / "config.json"
        config.write_text(json.dumps({"leafCapacity": 4, "pageCapacity": 2, "maxProxyCount": 2,
                                     "rootProxyCount": 1, "batchCapacity": 8,
                                     "diskReserveBytes": 0}), encoding="utf-8")
        cls.scene = cls.dir / "scene"
        cls.run_checked([str(VALIDATOR), "--input", str(source), "--job-directory", str(cls.dir / "job"),
                         "--output", str(cls.scene), "--config", str(config)], "tools-native-builder")
        cls.archive = cls.dir / "reference.monsterlod"
        # Independent schema oracle avoids making every rejection case depend on
        # the pack command under test successfully bootstrapping its own fixture.
        files = sorted(path for path in cls.scene.rglob("*") if path.is_file())
        descriptor = {"schemaVersion": 1, "format": "monsterlod", "manifest": "scene.mgs", "files": [
            {"path": path.relative_to(cls.scene).as_posix(), "bytes": path.stat().st_size,
             "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in files]}
        with zipfile.ZipFile(cls.archive, "w") as archive:
            archive.writestr("package.json", json.dumps(descriptor))
            for path in files:
                archive.writestr(path.relative_to(cls.scene).as_posix(), path.read_bytes())
        cls.cli("validate", cls.archive)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def run_checked(cls, args, label, success=True):
        result = subprocess.run(args, capture_output=True, timeout=120)
        (EVIDENCE / (label + ".log")).write_bytes(result.stdout + result.stderr)
        if success and result.returncode != 0:
            raise AssertionError(f"{label}: exit {result.returncode}; see its log")
        if not success and result.returncode == 0:
            raise AssertionError(f"{label}: invalid fixture accepted")
        return result

    @classmethod
    def cli(cls, action, source, output=None, success=True, extra=()):
        args = [sys.executable, str(cls.package), action, str(source)]
        if output is not None:
            args.append(str(output))
        args += ["--validator", str(VALIDATOR), *extra]
        return cls.run_checked(args, "tools-" + action + "-" + Path(source).stem, success)

    def assert_rejected(self, path):
        output = self.dir / (path.stem + "-rejected-output")
        self.cli("unpack", path, output, success=False)
        self.assertFalse(output.exists(), "invalid package must never publish destination")
        self.cli("validate", self.archive)

    def rewrite(self, name, transform):
        path = self.dir / (name + ".monsterlod")
        with zipfile.ZipFile(self.archive) as original, zipfile.ZipFile(path, "w") as target:
            entries = [(entry.filename, original.read(entry)) for entry in original.infolist()]
            for filename, data in transform(entries):
                target.writestr(filename, data)
        return path

    def roundtrip(self, stored):
        archive = self.dir / ("stored.monsterlod" if stored else "deflated.monsterlod")
        self.cli("pack", self.scene, archive, extra=("--stored",) if stored else ())
        restored = self.dir / (archive.stem + "-restored")
        self.cli("unpack", archive, restored)
        self.run_checked([str(VALIDATOR), "--validate-scene", str(restored)], "tools-native-validate-" + archive.stem)
        for source in self.scene.rglob("*"):
            if source.is_file():
                self.assertEqual(source.read_bytes(), (restored / source.relative_to(self.scene)).read_bytes())

    def test_scene_package_stored_roundtrip_exact_payload(self):
        self.roundtrip(True)

    def test_scene_package_deflated_roundtrip_exact_payload(self):
        self.roundtrip(False)

    def test_scene_package_traversal_cannot_escape_or_publish(self):
        path = self.rewrite("traversal", lambda entries: entries + [("../escape.bin", b"bad")])
        self.assert_rejected(path)
        self.assertFalse((self.dir / "escape.bin").exists())

    def test_scene_package_duplicate_casefold_names_rejected(self):
        path = self.rewrite("duplicate", lambda entries: entries + [("SCENE.MGS", b"bad")])
        self.assert_rejected(path)

    def test_scene_package_hash_corruption_rejected_before_publication(self):
        path = self.rewrite("bad-hash", lambda entries: [(name, data[:-1] + bytes([data[-1] ^ 1]))
                                                       if name == "scene.mgs" else (name, data) for name, data in entries])
        self.assert_rejected(path)

    def test_scene_package_forged_expansion_cap_rejected_without_allocation(self):
        path = self.dir / "oversized.monsterlod"
        data = bytearray(self.archive.read_bytes())
        central = data.find(b"PK\x01\x02")
        self.assertGreaterEqual(central, 0)
        struct.pack_into("<I", data, central + 24, (256 << 20) + 1)
        path.write_bytes(data)
        self.assert_rejected(path)

    def test_scene_package_existing_destination_remains_unchanged(self):
        output = self.dir / "existing"
        output.mkdir()
        marker = output / "keep.txt"
        marker.write_bytes(b"existing-user-scene")
        self.cli("unpack", self.archive, output, success=False)
        self.assertEqual(marker.read_bytes(), b"existing-user-scene")

    def test_builtin_generation_deterministic_licensed_scene_and_native_parse(self):
        first, second = self.dir / "builtin-a.ply", self.dir / "builtin-b.ply"
        a, b = self.generator.generate(first), self.generator.generate(second)
        self.assertEqual(a, b)
        self.assertEqual(a["gaussians"], 49152)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertTrue((ROOT / "resources/android/builtin-scene-license.txt").is_file())
        self.run_checked([str(VALIDATOR), "--validate-scene", str(first)], "tools-native-builtin-validation")


if __name__ == "__main__":
    unittest.main(verbosity=2)
