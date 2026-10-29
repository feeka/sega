import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from collections import Counter

spec = importlib.util.spec_from_file_location("run_experiments", Path(__file__).resolve().parents[1] / "run_experiments.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def record(method="bfs-se", phase="correctness"):
    return {"schema_version": 1, "status": "ok", "method": method,
            "family": method.split("-", 1)[0], "N": 10, "valid_nodes": 10, "k": 0,
            "backend": "synthetic", "source": 0, "forest": False,
            "visited": 3, "roots": 1, "event_count": 6 if method.startswith("dfs") else 3,
            "wall_ms": 1.0, "load_ms": 0.0,
            "verification": "sha256" if phase == "correctness" else "none", "event_file_bytes": 0,
            "aux_fixed_bytes": 8, "aux_dynamic_peak_bytes": 16, "aux_peak_bytes": 24,
            "graph_rss_kb": -1, "graph_peak_rss_kb": -1, "peak_rss_kb": -1, "rss_after_kb": -1, "swap_kb": -1,
            "digest_sha256": "a" * 64 if phase == "correctness" else None}


def entry(method, phase="correctness", repeat=1):
    return {"run_id": f"{method}-{phase}-{repeat}", "target": "first", "method": method,
            "phase": phase, "repeat": repeat, "result": record(method, phase)}


class RunnerTests(unittest.TestCase):
    def test_paper_defaults_and_separate_repeat_counts(self):
        args = runner.parse_args(["--binary", "bench", "--synthetic", "path", "--out", "unused"])
        self.assertEqual(len(args.methods), 9)
        self.assertNotIn("bfs-se", args.methods)
        self.assertNotIn("bfs-se32", args.methods)
        self.assertIn("dfs-se", args.methods)
        jobs = runner.make_jobs(args)
        self.assertEqual(Counter(j[2] for j in jobs), {"correctness": 9, "measurement": 45})
        self.assertEqual(len(jobs), runner.schedule_summary(args)["total_processes"])
        self.assertTrue(all(j[2] == "correctness" for j in jobs[:9]))
        for repeat in range(1, 6):
            block = jobs[9 * repeat:9 * (repeat + 1)]
            self.assertEqual({j[1] for j in block}, set(args.methods))
            self.assertEqual({j[3] for j in block}, {repeat})
        self.assertEqual(jobs, runner.make_jobs(args))

    def test_custom_counts_and_multiple_sources(self):
        args = runner.parse_args(["--binary", "bench", "--synthetic", "path", "--out", "unused",
                                  "--methods", "bfs-base,bfs-stream", "--sources", "0,7",
                                  "--repeats", "3", "--correctness-repeats", "2"])
        self.assertEqual(Counter(j[2] for j in runner.make_jobs(args)),
                         {"correctness": 8, "measurement": 12})

    def test_dry_run_does_not_create_output_or_require_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "not-created"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = runner.main(["--binary", str(Path(folder) / "absent"), "--synthetic", "path",
                                    "--out", str(output), "--dry-run"])
            self.assertEqual(code, 0)
            self.assertFalse(output.exists())
            self.assertEqual(json.loads(stdout.getvalue())["total_processes"], 54)

    def test_schema_accepts_valid_records(self):
        runner.validate_record(record(), "bfs-se", "correctness", False)
        runner.validate_record(record("dfs-packed", "measurement"), "dfs-packed", "measurement", False)

    def test_rejects_missing_digest_false_success_and_bad_events(self):
        for mutation in ({"digest_sha256": None}, {"status": "failed"}, {"event_count": 9},
                         {"N": True}, {"visited": 11}, {"source": 10}, {"wall_ms": float("nan")},
                         {"schema_version": 2}, {"method": "bfs-base"}, {"forest": True}):
            data = record()
            data.update(mutation)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                runner.validate_record(data, "bfs-se", "correctness", False)

    def test_rejects_hashed_timing(self):
        with self.assertRaises(ValueError):
            runner.validate_record(record(), "bfs-se", "measurement", False)

    def test_forest_and_empty(self):
        data = record()
        data.update(N=0, valid_nodes=0, source=None, forest=True, roots=0, visited=0, event_count=0)
        runner.validate_record(data, "bfs-se", "correctness", True)

    def test_cross_method_and_repetition_mismatch(self):
        first, second = entry("bfs-se"), entry("bfs-base", repeat=2)
        self.assertEqual(runner.compare_records([first, second]), [])
        second["result"]["digest_sha256"] = "b" * 64
        self.assertIn("digest mismatch", runner.compare_records([first, second])[0])

    def test_timing_count_mismatch_fails(self):
        first, second = entry("dfs-se"), entry("dfs-packed", "measurement")
        second["result"]["visited"] = 2
        self.assertTrue(any("mismatch" in error for error in runner.compare_records([first, second])))

    def test_rejects_invalid_metrics_coverage_and_audit(self):
        for mutation in ({"valid_nodes": 11}, {"valid_nodes": 2}, {"verification": "none"},
                         {"aux_fixed_bytes": -1}, {"peak_rss_kb": -2},
                         {"audit": {"status": "failed"}}, {"event_file_bytes": -1}):
            data = record()
            data.update(mutation)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                runner.validate_record(data, "bfs-se", "correctness", False)
        data = record()
        data.update(forest=True, source=None)
        with self.assertRaises(ValueError):
            runner.validate_record(data, "bfs-se", "correctness", True)

    def test_cross_family_coverage_mismatch(self):
        a, b = entry("bfs-se"), entry("dfs-se")
        b["result"]["visited"] = 4
        self.assertIn("Cross-family", runner.compare_records([a, b])[0])

    def test_graph_file_selection_is_narrow(self):
        with tempfile.TemporaryDirectory() as folder:
            prefix = Path(folder) / "graph"
            for suffix in (".sdbg_info", ".sdbg.0", ".sdbg.1", "-unrelated.txt", ".report.json"):
                Path(str(prefix) + suffix).write_text("x")
            self.assertEqual([p.name for p in runner.graph_files(prefix)],
                             ["graph.sdbg.0", "graph.sdbg.1", "graph.sdbg_info"])

    def test_invalid_cli_inputs(self):
        base = ["--binary", "bench", "--synthetic", "path", "--out", "unused"]
        for flags in (["--sources", "forest"], ["--seed", "-1"], ["--seed", str(2**64)],
                      ["--timeout", "nan"], ["--sources", ""], ["--methods", "bfs-se"],
                      ["--repeats", "0"], ["--correctness-repeats", "0"], ["--correctness-repeats", "-1"]):
            with self.subTest(flags=flags), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                runner.parse_args(base + flags)
            self.assertEqual(error.exception.code, 2)

    def test_audit_histogram_consistency(self):
        data = record()
        data["audit"] = {"status": "ok", "valid_nodes": 10, "transitions": 9,
                         "topology_sha256": "c" * 64, "indegree_histogram": [1, 9, 0, 0, 0],
                         "outdegree_histogram": [1, 9, 0, 0, 0]}
        runner.validate_record(data, "bfs-se", "correctness", False)
        data["audit"]["outdegree_histogram"] = [2, 8, 0, 0, 0]
        with self.assertRaises(ValueError):
            runner.validate_record(data, "bfs-se", "correctness", False)

    def test_incremental_digest_comparison(self):
        references = {}
        self.assertEqual(runner.compare_records([entry("bfs-se")], references=references), [])
        other = entry("bfs-base")
        other["result"]["digest_sha256"] = "b" * 64
        self.assertTrue(runner.compare_records([other], references=references))

    def test_different_family_or_source_independent(self):
        a, b, c = entry("dfs-se"), entry("bfs-se"), entry("bfs-base")
        c["target"] = "4"
        c["result"]["source"] = 4
        c["result"]["digest_sha256"] = "c" * 64
        self.assertEqual(runner.compare_records([a, b, c]), [])

    def test_exact_stream_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder) / "a", Path(folder) / "b"
            a.write_bytes(b"same output")
            b.write_bytes(b"same output")
            self.assertTrue(runner.exact_equal(a, b))
            entries = [entry("bfs-se"), entry("bfs-base")]
            entries[0]["events"], entries[1]["events"] = str(a), str(b)
            self.assertEqual(runner.compare_records(entries, exact=True), [])
            b.write_bytes(b"bad! output")
            self.assertIn("Exact event stream mismatch", runner.compare_records(entries, exact=True)[0])
            b.unlink()
            self.assertIn("Missing exact event file", runner.compare_records(entries, exact=True)[0])

    def test_only_unhashed_measurements_summarized(self):
        entries = [entry("bfs-se"), entry("bfs-se", "measurement"), entry("bfs-se", "measurement", 2)]
        entries[0]["result"]["wall_ms"] = 999
        entries[1]["result"]["wall_ms"] = 2
        entries[2]["result"]["wall_ms"] = 4
        summary = runner.summarize(entries)
        self.assertEqual(summary[0]["metrics"]["wall_ms"], {"median": 3, "min": 2, "max": 4})

    def test_atomic_output(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "summary.json"
            runner.atomic_json(p, {"status": "passed"})
            self.assertEqual(json.loads(p.read_text())["status"], "passed")
            self.assertEqual(list(Path(folder).glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
