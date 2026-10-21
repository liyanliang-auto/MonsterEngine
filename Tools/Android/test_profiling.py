"""Behavioral profiling correlation and host failure contracts (unittest)."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import analyze_profile as analyzer
import profiling_mcp
import profiling_tools
import tracy_adapter


def event(sequence, phase, timestamp, request=1, frame=2, submission=3,
          scene=4, page=5, epoch=1, value=0):
    return dict(eventSequence=sequence, timestampNs=timestamp,
                clockDomain="CLOCK_MONOTONIC", captureEpoch=epoch, threadId=9,
                name="vulkan.acquire", phase=phase, requestId=request,
                frameId=frame, submissionId=submission, sceneId=scene,
                pageId=page, value=value)


class ProfilingCorrelationTests(unittest.TestCase):
    def test_complete_scope_keeps_measured_duration(self):
        result = analyzer.event_summary([event(1, "B", 100), event(2, "E", 2000100)])
        self.assertEqual(result["scopeMilliseconds"]["vulkan.acquire"]["mean"], 2)

    def test_missing_scope_pairs_do_not_cross_causal_identity(self):
        # A dropped E for request 1 and dropped B for request 2 must remain
        # unmatched, rather than inventing a latency that crosses requests.
        for identity in ("request", "frame", "submission", "scene", "page"):
            with self.subTest(identity=identity):
                end = event(4, "E", 9000100, **{identity: 99})
                result = analyzer.event_summary([event(1, "B", 100), end])
                self.assertNotIn("vulkan.acquire", result["scopeMilliseconds"])
                self.assertEqual(result["unmatchedScopeBegins"], {"vulkan.acquire": 1})
                self.assertEqual(result["unmatchedScopeEnds"], {"vulkan.acquire": 1})

    def test_toggle_epoch_prevents_pairing(self):
        result = analyzer.event_summary([event(1, "B", 100), event(2, "E", 200, epoch=2)])
        self.assertEqual(result["scopeMilliseconds"], {})
        self.assertEqual(result["unmatchedScopeBegins"], {"vulkan.acquire": 1})
        self.assertEqual(result["unmatchedScopeEnds"], {"vulkan.acquire": 1})

    def test_thread_boundary_prevents_pairing(self):
        end = event(2, "E", 200)
        end["threadId"] = 10
        result = analyzer.event_summary([event(1, "B", 100), end])
        self.assertEqual(result["scopeMilliseconds"], {})
        self.assertEqual(result["unmatchedScopeBegins"], {"vulkan.acquire": 1})
        self.assertEqual(result["unmatchedScopeEnds"], {"vulkan.acquire": 1})

    def test_known_sequence_loss_does_not_invent_same_context_scope(self):
        # Repeated static diagnostic scopes often have identical (all-zero)
        # context. Lost E1 and B2 cannot safely turn B1/E2 into a duration.
        begin = event(1, "B", 100, request=0, frame=0, submission=0, scene=0, page=0)
        end = event(4, "E", 9000100, request=0, frame=0, submission=0, scene=0, page=0)
        result = analyzer.event_summary([begin, end])
        self.assertNotIn("vulkan.acquire", result["scopeMilliseconds"])

    def test_missing_zero_identity_is_not_observed_identity(self):
        result = analyzer.event_summary([event(1, "I", 100, request=0, frame=0,
                                               submission=0, scene=0, page=0)])
        self.assertTrue(all(count == 0 for count in result["observedNonzeroIdentityCounts"].values()))

    def test_off_empty_events_is_unavailable_duration_not_zero(self):
        result = analyzer.event_summary([])
        self.assertEqual(result["scopeMilliseconds"], {})
        self.assertIsNone(result["requestReceivedToOwnerRenderMilliseconds"]["mean"])

    def test_raw_json_preserves_negative_vkresult_uint64(self):
        encoded = (1 << 64) - 1000001004
        raw = dict(schemaVersion=1, events=[event(1, "E", 100, value=encoded)])
        payload = {"profilingRawJSON": json.dumps(raw), "profiling": {"schemaVersion": 1, "events": []}}
        exports = list(analyzer.profile_objects(payload))
        self.assertEqual(exports, [raw])
        result = analyzer.event_summary(exports[0]["events"])
        self.assertEqual(result["outcomeValues"]["vulkan.acquire"], {"-1000001004": 1})

    def test_invalid_raw_json_export_rejected(self):
        with self.assertRaises(ValueError):
            list(analyzer.profile_objects({"profilingRawJSON": '{"schemaVersion":2,"events":[]}'}))

    def test_native_export_ring_capacity_overflow_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "harness/raw").mkdir(parents=True)
            (root / "capture.json").write_text(json.dumps({"completed": True, "captureSession": "fixture"}))
            (root / "harness/metadata.json").write_text(json.dumps({"captureCompleted": True, "scenarios": []}))
            (root / "harness/raw/one.json").write_text(json.dumps({"schemaVersion": 1, "events": [event(1, "I", 1)] * 16385}))
            with patch.object(analyzer.measurements, "collect", return_value=([], [], [], 0, 0, [])):
                with self.assertRaisesRegex(ValueError, "ring capacity"):
                    analyzer.analyze(root, root)

    def test_duplicate_events_deduplicate_but_conflicting_identity_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "harness/raw").mkdir(parents=True)
            (root / "capture.json").write_text(json.dumps({"completed": True, "captureSession": "fixture", "gpuMemory": {"status": "unavailable"}}))
            (root / "harness/metadata.json").write_text(json.dumps({"captureCompleted": True, "scenarios": []}))
            export = {"schemaVersion": 1, "events": [event(1, "I", 1)]}
            for name in ("one", "two"):
                (root / ("harness/raw/" + name + ".json")).write_text(json.dumps(export))
            with patch.object(analyzer.measurements, "collect", return_value=([], [], [], 0, 0, [])):
                self.assertEqual(analyzer.analyze(root, root)["profiling"]["uniqueEvents"], 1)
                export["events"][0]["requestId"] = 999
                (root / "harness/raw/two.json").write_text(json.dumps(export))
                with self.assertRaisesRegex(ValueError, "Conflicting profiling event identity"):
                    analyzer.analyze(root, root)

    def test_comparison_rejects_manifest_and_mode_mismatch(self):
        base = dict(capture=dict(mode="off", manifestSha256="original"), identities=[], scenarios={})
        current = dict(capture=dict(mode="diagnostic", manifestSha256="derived"), identities=[], scenarios={})
        self.assertEqual(analyzer.compare(current, base)["status"], "unmatched")
        current["capture"]["manifestSha256"] = "original"
        current["capture"]["mode"] = "off"
        self.assertEqual(analyzer.compare(current, base)["overheadClaim"], "unavailable")


class ProfilingHostFailureTests(unittest.TestCase):
    def test_sql_comment_markers_inside_literals_are_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.pftrace"
            trace.write_bytes(b"fixture")
            result = {"exitCode": 0, "stdout": '"value"\n"ok"\n',
                      "stderr": "", "command": ["fixture-processor"]}
            for sql in ("SELECT 'ME/*' AS value", "SELECT 'semi;--literal' AS value",
                        "SELECT 'it''s /*quoted*/' AS value"):
                with self.subTest(sql=sql), patch.object(profiling_tools, "tool", return_value=Path("fixture")), \
                        patch.object(profiling_tools, "run", return_value=result):
                    self.assertEqual(profiling_tools.query(trace, sql)["rows"], [{"value": "ok"}])

    def test_sql_mutation_and_script_rejected_before_tool_execution(self):
        for sql in ("DROP TABLE slice", "SELECT 1; SELECT 2", "SELECT 1 -- comment"):
            with self.subTest(sql=sql), self.assertRaises(ValueError):
                profiling_tools.query(Path("absent.pftrace"), sql)

    def test_missing_executable_records_failure_instead_of_success(self):
        result = profiling_tools.run(["monsterengine_nonexistent_test_binary.exe"])
        self.assertIsNone(result["exitCode"])
        self.assertIn("error", result)

    def test_mcp_initialize_required_and_query_failure_preserved(self):
        requests = [dict(jsonrpc="2.0", id=1, method="tools/list"),
                    dict(jsonrpc="2.0", id=2, method="initialize"),
                    dict(jsonrpc="2.0", method="notifications/initialized"),
                    dict(jsonrpc="2.0", id=3, method="tools/call", params={
                        "name": "perfetto_query", "arguments": {"sql": "SELECT 1"}})]
        output = io.StringIO()
        with patch("sys.stdin", io.StringIO("\n".join(map(json.dumps, requests)))), \
                contextlib.redirect_stdout(output), \
                patch.object(profiling_mcp, "query", side_effect=RuntimeError("processor failed")):
            profiling_mcp.serve(Path("trace.pftrace"), Path("cache"))
        responses = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(len(responses), 3)
        self.assertIn("error", responses[0])
        self.assertEqual(responses[1]["result"]["protocolVersion"], "2024-11-05")
        self.assertTrue(responses[2]["result"]["isError"])
        self.assertIn("processor failed", responses[2]["result"]["content"][0]["text"])

    def test_tracy_empty_and_truncated_queries_fail_instead_of_fake_import(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ({"truncated": False, "rows": []}, {"truncated": True, "rows": []}):
                with self.subTest(value=value), patch.object(tracy_adapter, "query", return_value=value):
                    with self.assertRaises(ValueError):
                        tracy_adapter.import_trace(Path("trace"), Path(directory) / "out.tracy", Path("cache"), 10)
                    self.assertFalse((Path(directory) / "out.tracy").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
