"""M1 behavior verification. Fixture probes never establish real training acceptance."""
import copy
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
config_path = os.environ.get("MONSTER_VIDEO3D_TEST_CONFIG")
if not config_path:
    raise RuntimeError("Set MONSTER_VIDEO3D_TEST_CONFIG to an explicit test configuration JSON; see Tests/VideoTo3D/README.md")
CONFIG = json.loads(Path(config_path).read_text(encoding="utf-8-sig"))
if CONFIG.get("schemaVersion") != 1:
    raise ValueError("Unsupported VideoTo3D test configuration schema")
def configured_path(key):
    path = Path(CONFIG[key])
    if not path.is_absolute():
        raise ValueError("Test configuration requires absolute path: " + key)
    return path
EVIDENCE = configured_path("evidenceRoot")
CLI = configured_path("cliExecutable")
FIXTURE = configured_path("fixtureExecutable")
SOURCE = configured_path("brushSource")
DESCRIPTOR = ROOT / "Tools/VideoTo3D/brush-admission.json"
spec = importlib.util.spec_from_file_location("audit_brush", ROOT / "Tools/VideoTo3D/audit_brush.py")
audit_brush = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit_brush)


class AdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        identities = {}
        for role, path in (("cli", CLI), ("fixture", FIXTURE)):
            if not path.is_file():
                raise FileNotFoundError("Configured test binary unavailable: " + str(path))
            with path.open("rb") as stream:
                identities[role] = {"path": str(path), "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}
        audit_brush.publish(EVIDENCE / "testing-suite-binary-identities.json", identities)

    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(prefix="verification_\u4e2d\u6587 ", dir=EVIDENCE))
        self.output = self.directory / "report.json"
        self.descriptor = json.loads(DESCRIPTOR.read_text(encoding="utf-8"))

    def tearDown(self):
        self.assertTrue(self.directory.resolve().is_relative_to(EVIDENCE.resolve()))
        def writable_retry(function, path, unused):
            os.chmod(path, stat.S_IWRITE)
            function(path)
        shutil.rmtree(self.directory, onerror=writable_retry)

    def run_cli(self, *args, expected=1):
        result = subprocess.run([str(CLI), *map(str, args)], cwd=ROOT, capture_output=True, timeout=40)
        self.assertEqual(result.returncode, expected, result.stderr.decode("utf-8", "replace"))
        return result

    def fixture_args(self):
        pin = hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
        args = []
        for kind in ("ffprobe", "ffmpeg", "colmap", "brush"):
            args.extend(["--" + kind, FIXTURE, "--" + kind + "-sha256", pin])
        return args

    def test_cli_help_and_invalid_options(self):
        self.assertIn(b"developer M1 probe", self.run_cli("--help", expected=0).stdout)
        for args in ([], ["--unknown", "x"], ["--output"], ["--output", "x", "--output", "y"]):
            with self.subTest(args=args):
                self.run_cli(*args)

    def test_missing_tools_emit_incomplete_and_closed_product_gates(self):
        self.run_cli("--output", self.output, expected=2)
        report = json.loads(self.output.read_text())
        self.assertEqual(report["productAdmission"], "BLOCKED")
        self.assertEqual(report["versionProbeAdmission"], "INCOMPLETE")
        self.assertTrue(all(not tool["attempted"] for tool in report["tools"]))
        self.assertTrue(all(value == "NOT_VERIFIED" for value in report["gates"].values()))

    def test_unpinned_and_mismatched_files_are_never_executed(self):
        self.run_cli("--output", self.output, "--ffprobe", FIXTURE,
                     "--ffmpeg", FIXTURE, "--ffmpeg-sha256", "0" * 64, expected=2)
        tools = json.loads(self.output.read_text())["tools"]
        self.assertEqual(tools[0]["identityStatus"], "Unpinned")
        self.assertEqual(tools[1]["identityStatus"], "Mismatch")
        self.assertFalse(any(tool["attempted"] for tool in tools))

    def test_fixture_probe_complete_does_not_admit_product_or_training(self):
        self.run_cli("--output", self.output, *self.fixture_args(), expected=0)
        report = json.loads(self.output.read_text())
        self.assertEqual(report["versionProbeAdmission"], "PROBES_COMPLETED")
        self.assertEqual(report["productAdmission"], "BLOCKED")
        self.assertEqual(report["scope"], "VERSION_PROBE_ONLY")
        self.assertTrue(all(value == "NOT_VERIFIED" for value in report["gates"].values()))
        self.assertIn("NOT_VERIFIED", " ".join(report["limitations"]))
        for tool in report["tools"]:
            self.assertEqual(tool["status"], "PROBE_COMPLETED")
            self.assertEqual(tool["streamEncoding"], "hex-raw-bytes")
            self.assertEqual(bytes.fromhex(tool["stdoutHex"]), b"TEST_FIXTURE_ONLY\xff\x80")
            self.assertEqual(tool["postProbeIdentityStatus"], "Match")
        self.assertEqual([tool["argv"][1] for tool in report["tools"]], ["-version", "-version", "help", "--help"])

    def test_unknown_schema_rejected_preserving_raw_and_previous_report(self):
        raw = self.directory / "unknown.json"
        data = b'{"schemaVersion":99}'
        raw.write_bytes(data)
        self.output.write_bytes(b"previous valid report")
        self.run_cli("--output", self.output, "--brush-audit", raw, *self.fixture_args())
        self.assertEqual(raw.read_bytes(), data)
        self.assertEqual(self.output.read_bytes(), b"previous valid report")

    def test_output_cannot_overwrite_audit_or_tool_input(self):
        before = DESCRIPTOR.read_bytes()
        self.run_cli("--output", DESCRIPTOR)
        self.assertEqual(DESCRIPTOR.read_bytes(), before)
        before = hashlib.sha256(FIXTURE.read_bytes()).hexdigest()
        self.run_cli("--output", FIXTURE, "--ffprobe", FIXTURE)
        self.assertEqual(hashlib.sha256(FIXTURE.read_bytes()).hexdigest(), before)

    def test_failed_atomic_publication_retains_existing_report_and_cleans_temp(self):
        self.output.write_bytes(b"previous valid report")
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                      ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(self.output), 0x80000000, 3, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            self.run_cli("--output", self.output)
            self.assertEqual(self.output.read_bytes(), b"previous valid report")
            self.assertFalse(list(self.directory.glob("report.json.tmp-*")))
        finally:
            kernel.CloseHandle(handle)

    def test_nonexistent_output_parent_fails_without_creating_report(self):
        destination = self.directory / "missing" / "report.json"
        self.run_cli("--output", destination)
        self.assertFalse(destination.exists())

    def test_actual_official_pinned_source_audit(self):
        result = audit_brush.audit(SOURCE, self.descriptor)
        self.assertEqual(result["revision"], self.descriptor["revision"])
        self.assertGreater(result["anchorsChecked"], 10)
        self.assertGreater(len(result["files"]), 3)
        for capability in ("safePause", "fullCheckpointRestore"):
            self.assertEqual(self.descriptor["capabilities"][capability]["status"], "MISSING")
        self.assertEqual(len(self.descriptor["requiredCheckpointState"]), 7)
        audit_brush.publish(EVIDENCE / "testing-brush-source-facts.json", result)

    def test_audit_rejects_unknown_schema_revision_path_anchor_url(self):
        for mutation in ("schema", "revision", "path", "anchor", "url"):
            data = copy.deepcopy(self.descriptor)
            if mutation == "schema": data["schemaVersion"] = 99
            elif mutation == "revision": data["revision"] = "0" * 40
            else:
                item = data["capabilities"]["safePause"]["evidence"][0]
                if mutation == "path": item["file"] = "../outside"
                elif mutation == "anchor": item["anchor"] = "__no_such_source_anchor__"
                else: item["url"] += "tampered"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                audit_brush.audit(SOURCE, data)

    def test_audit_rejects_tampered_working_source(self):
        checkout = self.directory / "checkout"
        def git(*args):
            result = subprocess.run(["git", *map(str,args)], capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        git("clone", "--shared", "--no-checkout", SOURCE, checkout)
        git("-C", checkout, "checkout", "--detach", self.descriptor["revision"])
        git("-C", checkout, "remote", "set-url", "origin", self.descriptor["upstreamUrl"])
        path = checkout / self.descriptor["capabilities"]["safePause"]["evidence"][0]["file"]
        path.write_bytes(path.read_bytes() + b"\n// tamper\n")
        with self.assertRaisesRegex(ValueError, "working source differs"):
            audit_brush.audit(checkout, self.descriptor)

    def test_audit_failed_serialization_preserves_previous_report(self):
        self.output.write_bytes(b"previous valid report")
        with self.assertRaises(TypeError):
            audit_brush.publish(self.output, {"invalid": object()})
        self.assertEqual(self.output.read_bytes(), b"previous valid report")
        self.assertEqual(list(self.directory.iterdir()), [self.output])

    def test_real_selected_ffmpeg_ffprobe_colmap_probes(self):
        paths = {kind: Path(CONFIG["tools"][kind]) for kind in ("ffprobe", "ffmpeg", "colmap")}
        args, identities = [], {}
        for kind,path in paths.items():
            self.assertTrue(path.is_absolute(), "configured real tool must have an absolute path: " + kind)
            self.assertTrue(path.is_file(), "selected real tool unavailable: " + str(path))
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            identities[kind] = {"path": str(path), "sha256": digest}
            args.extend(["--" + kind, path, "--" + kind + "-sha256", digest])
        audit_brush.publish(EVIDENCE / "testing-real-tool-identities.json", identities)
        output = EVIDENCE / "testing-real-tools-report.json"
        self.run_cli("--output", output, *args, expected=2)
        report = json.loads(output.read_text())
        for tool in report["tools"][:3]:
            self.assertEqual(tool["status"], "PROBE_COMPLETED", tool)
            self.assertTrue(tool["attempted"])
            self.assertGreater(tool["stdoutTotalBytes"] + tool["stderrTotalBytes"], 0)
        self.assertFalse(report["tools"][3]["attempted"])
        self.assertEqual(report["productAdmission"], "BLOCKED")
        self.assertTrue(all(value == "NOT_VERIFIED" for value in report["gates"].values()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
