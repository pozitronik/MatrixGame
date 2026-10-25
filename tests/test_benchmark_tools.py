# MatrixGame - licensed under GPLv2 or any later version.
import copy
import importlib.util
import io
import json
from pathlib import Path
import random
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("benchmark", PROJECT / "tools/benchmark.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


def process_report(samples, work=None, allocations=10):
    return {
        "schema": "matrixgame-benchmark/1",
        "build": {"compiler": "GCC 13.2.0", "configuration": "Release", "architecture": "x86",
                  "engine_heap_tracking": False},
        "host": {"processor": "Synthetic CPU", "logical_processors": 8, "timer_frequency_hz": 10000000},
        "runner": {"warmup": 1, "repetitions": len(samples), "validation_delay_us": 0},
        "workloads": [{
            "name": "navigation.crowd_move",
            "description": "synthetic",
            "parameters": {"robots": "48"},
            "iterations_per_sample": 20,
            "work_per_sample": work or {"local_searches": 960},
            "inspection": {"rng_draws": 1},
            "samples_ns": samples,
            "statistics_ns": {"median": benchmark.quantile(samples, 0.5)},
            "allocations_per_sample": {"stable": True, "allocations": [allocations] * len(samples),
                                       "reallocations": [0] * len(samples), "frees": [allocations] * len(samples),
                                       "requested_bytes": [100] * len(samples)},
        }],
    }


def result(scale=1.0, seed=1, noise=0.02, processes=3, work=None, offsets=None):
    generator = random.Random(seed)
    reports = []
    for index in range(processes):
        level = scale * (offsets[index] if offsets else 1.0)
        samples = [1.0e7 * level * (1.0 + generator.uniform(-noise, noise)) for _ in range(15)]
        reports.append(process_report(samples, work))
    context = {"revision": {"commit": "synthetic", "dirty": False},
               "cmake": {"build_type": "Release", "dll": "OFF", "cheats": "OFF"}}
    return benchmark.combine(reports, context)


class BenchmarkToolTests(unittest.TestCase):
    def test_statistics_use_interpolated_quantiles(self):
        stats = benchmark.describe([4.0, 1.0, 3.0, 2.0])
        self.assertEqual(stats["median"], 2.5)
        self.assertEqual(stats["min"], 1.0)
        self.assertEqual(stats["max"], 4.0)
        self.assertAlmostEqual(stats["p90"], 3.7)
        self.assertEqual(stats["mad"], 1.0)

    def test_processes_must_repeat_the_same_work(self):
        reports = [process_report([1.0, 2.0]), process_report([1.0, 2.0], work={"local_searches": 959})]
        with self.assertRaisesRegex(benchmark.BenchmarkError, "not repeatable"):
            benchmark.combine(reports, {})

    def test_combined_result_keeps_every_sample(self):
        combined = result()
        workload = combined["workloads"][0]
        self.assertEqual(len(workload["samples_ns"]), 45)
        self.assertEqual(len(workload["process_medians_ns"]), 3)
        self.assertEqual([len(group) for group in workload["process_samples_ns"]], [15, 15, 15])
        self.assertTrue(workload["allocations_per_sample"]["stable"])
        self.assertEqual(combined["runner"]["processes"], 3)

    def test_repeated_measurement_is_not_a_regression(self):
        rows, problems, _ = benchmark.compare(result(seed=1), result(seed=2))
        self.assertEqual(problems, [])
        self.assertEqual(rows[0]["verdict"], "no material change")

    def test_material_slowdown_is_a_regression(self):
        rows, problems, _ = benchmark.compare(result(seed=1), result(scale=1.2, seed=2))
        self.assertEqual(problems, [])
        self.assertEqual(rows[0]["verdict"], "regression")
        rows, _, _ = benchmark.compare(result(seed=1), result(scale=0.8, seed=2))
        self.assertEqual(rows[0]["verdict"], "improvement")

    def test_small_change_inside_noise_is_not_material(self):
        rows, _, _ = benchmark.compare(result(seed=1, noise=0.2), result(scale=1.06, seed=2, noise=0.2))
        self.assertIn(rows[0]["verdict"], ("inconclusive", "no material change"))

    def test_process_variation_widens_the_interval(self):
        # Within-process noise is tiny, but the candidate processes disagree by several percent.
        steady = result(seed=1, noise=0.001)
        scattered = result(seed=2, noise=0.001, offsets=[0.96, 1.0, 1.06])
        rows, _, _ = benchmark.compare(steady, scattered)
        low, high = rows[0]["interval"]
        self.assertLess(low, -0.03)
        self.assertGreater(high, 0.05)
        self.assertEqual(rows[0]["verdict"], "inconclusive")

    def test_bootstrap_is_deterministic(self):
        base, candidate = result(seed=1), result(seed=2)
        self.assertEqual(benchmark.compare(base, candidate)[0][0]["interval"],
                         benchmark.compare(base, candidate)[0][0]["interval"])

    def test_allocation_stability_covers_every_counter(self):
        reports = [process_report([1.0, 2.0]), process_report([1.0, 2.0])]
        reports[1]["workloads"][0]["allocations_per_sample"]["requested_bytes"] = [100, 120]
        combined = benchmark.combine(reports, {})
        self.assertFalse(combined["workloads"][0]["allocations_per_sample"]["stable"])
        text = benchmark.allocation_text((result()["workloads"][0]["allocations_per_sample"],
                                          combined["workloads"][0]["allocations_per_sample"]))
        self.assertIn("vary between samples", text)

    def test_candidate_only_workloads_are_reported(self):
        candidate = result()
        extra = copy.deepcopy(candidate["workloads"][0])
        extra["name"] = "selection.extra"
        candidate["workloads"].append(extra)
        _, problems, notes = benchmark.compare(result(), candidate)
        self.assertEqual(problems, [])
        self.assertEqual(notes, ["selection.extra exists only in the candidate"])

    def test_overlapping_processes_prevent_a_regression_verdict(self):
        self.assertEqual(benchmark.classify(1.2, 1.3, 0.05, separated=False), "inconclusive")
        self.assertEqual(benchmark.classify(1.2, 1.3, 0.05, separated=True), "regression")
        self.assertEqual(benchmark.classify(0.98, 1.08, 0.05, separated=True), "inconclusive")

    def test_different_work_or_identity_is_not_compared(self):
        _, problems, _ = benchmark.compare(result(), result(work={"local_searches": 961}))
        self.assertTrue(any("different work" in problem for problem in problems))
        other = result()
        other["build"]["configuration"] = "Debug"
        _, problems, _ = benchmark.compare(result(), other)
        self.assertTrue(any("build.configuration" in problem for problem in problems))
        _, problems, _ = benchmark.compare(result(), other, allow_identity_mismatch=True)
        self.assertEqual(problems, [])

    def test_strict_inspection_rejects_differences_before_timing(self):
        base, candidate = result(), result()
        base['workloads'][0]['inspection']['interval_0.local_searches'] = 2
        candidate['workloads'][0]['inspection']['interval_0.local_searches'] = 3
        # Existing optimization comparisons intentionally retain their previous behavior.
        self.assertEqual(benchmark.compare(base, candidate)[1], [])
        with patch.object(benchmark, 'bootstrap_ratio') as timing:
            rows, problems, _ = benchmark.compare(base, candidate, require_inspection_match=True)
        timing.assert_not_called()
        self.assertEqual(rows, [])
        self.assertTrue(any("inspection['interval_0.local_searches'] differs" in problem for problem in problems))

    def test_strict_inspection_rejects_missing_fields_and_objects(self):
        for role in ('base', 'candidate', 'both'):
            for missing_counter in (False, True):
                with self.subTest(role=role, missing_counter=missing_counter):
                    base, candidate = result(), result()
                    reports = {'base': [base], 'candidate': [candidate], 'both': [base, candidate]}
                    for report in reports[role]:
                        workload = report['workloads'][0]
                        if missing_counter:
                            del workload['inspection']['rng_draws']
                        else:
                            del workload['inspection']
                    if missing_counter and role == 'both':
                        # Empty inspection objects are emitted by Sessions without diagnostic counters.
                        self.assertEqual(benchmark.compare(base, candidate, require_inspection_match=True)[1], [])
                    else:
                        with patch.object(benchmark, 'bootstrap_ratio') as timing:
                            rows, problems, _ = benchmark.compare(base, candidate, require_inspection_match=True)
                        timing.assert_not_called()
                        self.assertEqual(rows, [])
                        self.assertTrue(any('inspection' in problem for problem in problems))

    def test_strict_inspection_rejects_correlated_malformed_counters(self):
        for value in (True, False, 1.0, '1', None, [], {'nested': 1}, -(1 << 63) - 1, 1 << 63):
            with self.subTest(value=value):
                base, candidate = result(), result()
                for report in (base, candidate):
                    report['workloads'][0]['inspection']['rng_draws'] = value
                with patch.object(benchmark, 'bootstrap_ratio') as timing:
                    rows, problems, _ = benchmark.compare(base, candidate, require_inspection_match=True)
                timing.assert_not_called()
                self.assertEqual(rows, [])
                self.assertTrue(any("inspection['rng_draws'] must be an int64 counter" in p for p in problems))
        for inspection in (None, [], 1, 'counters', True, {1: 2}):
            with self.subTest(inspection=inspection):
                base, candidate = result(), result()
                for report in (base, candidate):
                    report['workloads'][0]['inspection'] = inspection
                self.assertTrue(benchmark.compare(base, candidate, require_inspection_match=True)[1])

    def test_strict_inspection_accepts_equal_signed_counters_regardless_of_key_order(self):
        base, candidate = result(), result()
        base['workloads'][0]['inspection'] = {'minimum': -(1 << 63), 'maximum': (1 << 63) - 1, 'zero': 0}
        candidate['workloads'][0]['inspection'] = {'zero': 0, 'maximum': (1 << 63) - 1, 'minimum': -(1 << 63)}
        rows, problems, _ = benchmark.compare(base, candidate, require_inspection_match=True)
        self.assertEqual(problems, [])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['verdict'], 'no material change')

    def test_strict_inspection_cannot_be_bypassed_by_other_overrides(self):
        base, candidate = result(), result()
        candidate['build']['configuration'] = 'Debug'
        candidate['workloads'][0]['parameters']['robots'] = '49'
        candidate['workloads'][0]['inspection']['rng_draws'] = 2
        with patch.object(benchmark, 'bootstrap_ratio') as timing:
            rows, problems, _ = benchmark.compare(base, candidate, allow_identity_mismatch=True,
                                                 allow_parameter_change=True, require_inspection_match=True)
        timing.assert_not_called()
        self.assertEqual(rows, [])
        self.assertTrue(any("inspection['rng_draws'] differs" in problem for problem in problems))

    def test_instrumentation_precision_and_counters_are_identity(self):
        changes = [("cmake", "coverage", "OFF", "ON"),
                   ("runner", "x87_control_word", "0x001f", "0x0000"),
                   ("runner", "allocation_scope", "executable CRT imports", None),
                   ("host", "timer_frequency_hz", 10000000, 3000000)]
        for section, field, before, after in changes:
            base, candidate = result(seed=1), result(scale=1.2, seed=2)
            base.setdefault(section, {})[field] = before
            candidate.setdefault(section, {})[field] = after
            rows, problems, _ = benchmark.compare(base, candidate)
            self.assertTrue(any(f"{section}.{field} differs" in problem for problem in problems), field)
            _, problems, _ = benchmark.compare(base, candidate, allow_identity_mismatch=True)
            self.assertEqual(problems, [])

    def test_context_differences_are_notes(self):
        base, candidate = result(), result()
        base["os"], candidate["os"] = "Windows-11-10.0.26100", "Windows-11-10.0.26200"
        candidate["runner"]["repetitions"] = 30
        _, problems, notes = benchmark.compare(base, candidate)
        self.assertEqual(problems, [])
        self.assertTrue(any(note.startswith("os differs") for note in notes))
        self.assertTrue(any(note.startswith("runner.repetitions differs") for note in notes))

    def test_run_records_selected_executable_without_changing_default_command(self):
        base = PROJECT / "build/benchmark-tests"
        base.mkdir(parents=True, exist_ok=True)
        for selection in ("implicit-default", "explicit-default", "custom"):
            with self.subTest(selection=selection), tempfile.TemporaryDirectory(dir=base) as temporary:
                build = Path(temporary)
                binary = build / "tests" / ("custom-navigation.exe" if selection == "custom" else "matrixgame_benchmarks.exe")
                binary.parent.mkdir()
                binary.write_bytes(b"synthetic benchmark; never executed")
                (binary.parent / "d3dx9_43.dll").write_bytes(b"synthetic runtime; never loaded")
                (build / "CMakeCache.txt").write_text(
                    "CMAKE_BUILD_TYPE:STRING=Release\nMATRIXGAME_BUILD_DLL:BOOL=OFF\n"
                    "MATRIXGAME_CHEATS:BOOL=OFF\nMATRIXGAME_COVERAGE:BOOL=OFF\n", encoding="utf-8")
                output = build / "combined.json"
                launched = []

                def fake_process(command, **kwargs):
                    launched.append(command[0])
                    path = Path(command[command.index("--output") + 1])
                    path.write_text(json.dumps(process_report([1.0, 2.0])), encoding="utf-8")
                    return benchmark.subprocess.CompletedProcess(command, 0, "", "")

                arguments = ["run", "--build-dir", str(build), "--output", str(output),
                             "--processes", "2", "--repetitions", "2"]
                if selection != "implicit-default":
                    arguments += ["--executable", str(binary)]
                with patch.object(benchmark.subprocess, "run", side_effect=fake_process), \
                        patch.object(benchmark, "revision", return_value={"commit": "synthetic", "dirty": False}), \
                        redirect_stdout(io.StringIO()):
                    self.assertEqual(benchmark.main(arguments), 0)
                report = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(launched, [str(binary), str(binary)])
                self.assertEqual(report["executable"]["name"], binary.name)
                self.assertEqual(report["command"][0], binary.stem)

    def test_compare_command_exit_status(self):
        base = PROJECT / "build/benchmark-tests"
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temporary:
            root = Path(temporary)
            reference = root / "reference.json"
            slower = root / "slower.json"
            reference.write_text(json.dumps(result(seed=1)))
            slower.write_text(json.dumps(result(scale=1.3, seed=2)))
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.main(["compare", str(reference), str(reference)]), 0)
                self.assertEqual(benchmark.main(["compare", str(reference), str(slower)]), 1)
                broken = copy.deepcopy(result())
                broken["schema"] = "other"
                slower.write_text(json.dumps(broken))
                self.assertEqual(benchmark.main(["compare", str(reference), str(slower)]), 2)
            self.assertIn("regression", output.getvalue())

    def test_strict_inspection_compare_command_exit_status(self):
        base = PROJECT / 'build/benchmark-tests'
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as temporary:
            root = Path(temporary)
            reference, changed = root / 'reference.json', root / 'changed.json'
            reference.write_text(json.dumps(result()), encoding='utf-8')
            command = ['compare', str(reference), str(changed)]
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.main(['compare', str(reference), str(reference),
                                                 '--require-inspection-match']), 0)
            for inspection in ({'rng_draws': 2}, {'rng_draws': True}, {'rng_draws': 1.0},
                               {'rng_draws': {'nested': 1}}, {}, None):
                with self.subTest(inspection=inspection):
                    candidate = result()
                    candidate['workloads'][0]['inspection'] = inspection
                    changed.write_text(json.dumps(candidate), encoding='utf-8')
                    output = io.StringIO()
                    with redirect_stdout(output), redirect_stderr(io.StringIO()):
                        self.assertEqual(benchmark.main(command), 0)
                        self.assertEqual(benchmark.main(command + ['--require-inspection-match',
                                                                  '--allow-identity-mismatch',
                                                                  '--allow-parameter-change']), 2)
                    self.assertIn('Not comparable: navigation.crowd_move', output.getvalue())
                    self.assertIn('inspection', output.getvalue())
            for path in (reference, changed):
                report = result()
                del report['workloads'][0]['inspection']
                path.write_text(json.dumps(report), encoding='utf-8')
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.main(command + ['--require-inspection-match']), 2)


if __name__ == "__main__":
    unittest.main()
