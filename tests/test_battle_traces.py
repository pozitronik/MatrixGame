"""Read-only scenario trace comparison contracts; no game or external assets."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("battle_traces", Path(__file__).parents[1] / "tools" / "compare-battle-traces.py")
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)


def trace():
    return [
        {"type": "header", "schema": 1, "fixture": "obstacle-v1", "rng": "ParkMiller", "seed": 79,
         "step_ms": 10, "configuration": "Debug", "compiler": "MinGW", "precision_control": 0,
         "compiler_version": "13.2.0", "render": "no-draw", "audio": "synthetic", "revision": "abc", "dirty": False,
         "assert_off": False, "build_exe": True, "cheats": False, "group_counts": "Corrected",
         "viewport": [1280, 720], "max_frames": 3000, "initially_selected": True},
        {"type": "command", "frame": 0, "name": "move(500,300)"},
        {"type": "snapshot", "state": {"frame": 0, "time_ms": 0, "rng_draws": 2,
         "robots": [{"id": 1, "alive": True, "x_bits": 1065353216}]}},
        {"type": "snapshot", "state": {"frame": 1, "time_ms": 10, "rng_draws": 4,
         "robots": [{"id": 1, "alive": True, "x_bits": 1073741824}]}},
        {"type": "complete", "frame": 1, "time_ms": 10},
    ]


def text(records):
    return "\n".join(json.dumps(record) for record in records) + "\n"


def validated(records):
    return TRACES.read_records(io.StringIO(text(records)))


class TraceContracts(unittest.TestCase):
    def test_identical_and_reordered_object_keys_match(self):
        left = validated(trace())
        right = validated([{key: record[key] for key in reversed(record)} for record in trace()])
        self.assertIsNone(TRACES.compare_records(left, right))

    def test_revision_and_dirty_are_provenance_only(self):
        left, right = trace(), trace()
        right[0]["revision"] = "def"
        right[0]["dirty"] = True
        self.assertIsNone(TRACES.compare_records(validated(left), validated(right)))

    def test_execution_profile_changes_reject_by_default(self):
        for key, value in {"step_ms": 16, "configuration": "Release", "compiler": "MSVC", "precision_control": 65536,
                           "seed": 80, "rng": "LegacyCRT", "fixture": "obstacle-v2", "audio": "unavailable",
                           "render": "draw", "compiler_version": "14"}.items():
            with self.subTest(key=key):
                right = trace()
                right[0][key] = value
                result = TRACES.compare_records(validated(trace()), validated(right))
                self.assertIn("Profile mismatch at line 1", result)
                self.assertIn("$." + key, result)

    def test_override_does_not_relax_record_comparison(self):
        right = trace()
        right[0]["configuration"] = "Release"
        self.assertIsNone(TRACES.compare_records(validated(trace()), validated(right), True))
        right[3]["state"]["robots"][0]["x_bits"] += 1
        result = TRACES.compare_records(validated(trace()), validated(right), True)
        self.assertIn("line 4", result)
        self.assertIn("snapshot boundary 1", result)
        self.assertIn("$.state.robots[0].x_bits", result)
        self.assertIn("float32=2.000000238418579", result)

    def test_override_allows_only_build_profile_fields(self):
        changes = {"configuration": "Release", "compiler": "MSVC", "compiler_version": "19.44",
                   "assert_off": True, "precision_control": 65536, "build_exe": False, "cheats": True,
                   "group_counts": "Original"}
        for key, value in changes.items():
            with self.subTest(key=key):
                right = trace()
                right[0][key] = value
                self.assertIn("$." + key, TRACES.compare_records(validated(trace()), validated(right)))
                self.assertIsNone(TRACES.compare_records(validated(trace()), validated(right), True))
        right = trace()
        right[0].update(changes)
        self.assertIsNone(TRACES.compare_records(validated(trace()), validated(right), True))

    def test_override_rejects_scenario_changes_even_with_identical_records(self):
        changes = {"seed": 80, "rng": "LegacyCRT", "step_ms": 16, "fixture": "obstacle-v2",
                   "audio": "unavailable", "render": "draw", "viewport": [1920, 1080],
                   "max_frames": 1500, "initially_selected": False}
        for key, value in changes.items():
            with self.subTest(key=key):
                right = trace()
                right[0][key] = value
                result = TRACES.compare_records(validated(trace()), validated(right), True)
                self.assertIn("Profile mismatch at line 1", result)
                self.assertIn("$." + key, result)

    def test_override_rejects_missing_and_unknown_scenario_fields(self):
        for key in ("audio", "render", "viewport", "max_frames", "initially_selected"):
            with self.subTest(key=key):
                right = trace()
                del right[0][key]
                self.assertIn("right=<missing>", TRACES.compare_records(validated(trace()), validated(right), True))
        right = trace()
        right[0]["future_scenario_option"] = True
        result = TRACES.compare_records(validated(trace()), validated(right), True)
        self.assertIn("$.future_scenario_option", result)
        self.assertIn("left=<missing>", result)

    def test_first_field_is_alphabetical_regardless_of_emission_order(self):
        right = trace()
        right[0]["seed"] += 1
        right[0]["audio"] = "unavailable"
        result = TRACES.compare_records(validated(trace()), validated(right), True)
        self.assertIn("$.audio", result)

    def test_earliest_difference_includes_commands(self):
        right = trace()
        right[1]["name"] = "stop"
        right[3]["state"]["rng_draws"] = 99
        result = TRACES.compare_records(validated(trace()), validated(right))
        self.assertIn("line 2", result)
        self.assertIn("command boundary 0", result)
        self.assertIn("$.name", result)

    def test_missing_records_and_fields_do_not_match(self):
        right = trace()[:3] + [{"type": "complete", "frame": 0, "time_ms": 0}]
        result = TRACES.compare_records(validated(trace()), validated(right))
        self.assertIn("line 4", result)
        self.assertIn("right completed boundary 0", result)
        right = trace()
        del right[2]["state"]["robots"][0]["alive"]
        self.assertIn("right=<missing>", TRACES.compare_records(validated(trace()), validated(right)))
        right = trace()
        del right[1]
        self.assertIn("line 2", TRACES.compare_records(validated(trace()), validated(right)))

    def test_numeric_types_and_array_lengths_are_exact(self):
        right = trace()
        right[2]["state"]["robots"][0]["id"] = 1.0
        self.assertIn(".id", TRACES.compare_records(validated(trace()), validated(right)))
        right = trace()
        right[2]["state"]["robots"].append({"id": 2})
        self.assertIn(".robots.length", TRACES.compare_records(validated(trace()), validated(right)))

    def test_rejects_empty_missing_repeated_and_unsupported_headers(self):
        invalid = [[], trace()[1:], trace()[:1], trace()[:2], [trace()[0], trace()[0], trace()[2]]]
        for records in invalid:
            with self.subTest(records=records):
                with self.assertRaises(TRACES.TraceError):
                    validated(records)
        for schema in (2, True, "1"):
            records = trace()
            records[0]["schema"] = schema
            with self.assertRaises(TRACES.TraceError):
                validated(records)

    def test_rejects_malformed_tail_even_if_prefix_would_differ(self):
        for suffix in ("{", "", "[]", '{"type":"event"}', '{"type":"snapshot","type":"command"}',
                       '{"bad":NaN}', '{"bad":1e999}'):
            with self.subTest(suffix=suffix):
                with self.assertRaises(TRACES.TraceError):
                    TRACES.read_records(io.StringIO(text(trace()) + suffix + "\n"), "candidate")

    def test_rejects_malformed_records_and_backwards_boundaries(self):
        for key in ("fixture", "rng", "configuration", "compiler", "precision_control", "seed", "step_ms"):
            records = trace()
            del records[0][key]
            with self.assertRaises(TRACES.TraceError):
                validated(records)
        changes = [(2, "frame", -1), (2, "rng_draws", True), (3, "time_ms", -1), (3, "rng_draws", 1)]
        for index, key, value in changes:
            records = trace()
            records[index]["state"][key] = value
            with self.assertRaises(TRACES.TraceError):
                validated(records)
        records = trace() + [{"type": "command", "frame": 0, "name": "late"}]
        with self.assertRaises(TRACES.TraceError):
            validated(records)
        for value in (-1, 4294967296, 1.0, True):
            records = trace()
            records[2]["state"]["robots"][0]["x_bits"] = value
            with self.assertRaises(TRACES.TraceError):
                validated(records)

    def test_cli_exit_codes_and_override_notice(self):
        different_scenario = trace()
        different_scenario[0]["seed"] += 1
        for candidate, extra, expected in [(trace(), [], 0), (trace()[:-1], [], 2), ([], [], 2),
                                            (trace(), ["--allow-profile-difference"], 0),
                                            (different_scenario, ["--allow-profile-difference"], 1)]:
            with self.subTest(expected=expected, extra=extra):
                output = io.StringIO()
                with mock.patch.object(Path, "open", side_effect=[io.StringIO(text(trace())), io.StringIO(text(candidate))]), \
                     contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                    self.assertEqual(expected, TRACES.main(["left.jsonl", "right.jsonl"] + extra))
                if extra and expected == 0:
                    self.assertIn("not same-profile evidence", output.getvalue())
        with mock.patch.object(Path, "open", side_effect=OSError("missing")), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(2, TRACES.main(["left.jsonl", "right.jsonl"]))

    def test_well_formed_truncation_and_false_completion_are_rejected(self):
        for records in (trace()[:-1], trace()[:3], trace() + [trace()[-1]],
                        trace()[:-1] + [{"type": "complete", "frame": 2, "time_ms": 20}]):
            with self.subTest(records=records), self.assertRaises(TRACES.TraceError):
                validated(records)


if __name__ == "__main__":
    unittest.main()
