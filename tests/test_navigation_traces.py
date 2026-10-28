"""Exercise the N5 checker's public entry point with a complete synthetic corpus."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("navigation_traces", Path(__file__).with_name("check_navigation_traces.py"))
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
REVISION = "1" * 40
MOVERS = [1, 2, 5, 6, 7, 8, 9, 10]


def encode(record):
    return json.dumps(record, separators=(",", ":")) + "\n"


def synthetic_trace(family, rng, step):
    """Build independently of expected()/validate(), retaining real fixture horizons."""
    physical = family in ("intermediate", "trailing-reference")
    count = (5 if family == "intermediate" else 6) if physical else 10
    last = 32 if physical else 32000 // step
    draws = 17 if family == "opposing" else 10
    fixture = {"intermediate": "formation-moving-intermediate-v1", "trailing-reference": "formation-moving-reference-v1",
               "crowded": "crowded-destination-v1", "opposing": "opposing-flow-v1"}[family]
    yield {"type": "header", "schema": 1, "fixture": fixture, "rng": rng, "seed": 81, "step_ms": step,
           "configuration": "Debug", "compiler": "MinGW", "compiler_version": "synthetic", "precision_control": 0,
           "revision": REVISION, "dirty": 0, "build_exe": True, "cheats": False, "assert_off": False,
           "delayed_damage": False, "group_counts": False, "place_containment": False, "zero_x_trace": False,
           "audio": "unavailable", "render": "storage-device-no-draw-cursor-surrogate", "viewport": [1280, 720],
           "initially_selected": True, "navigation_observation": "physical-actors-v1" if physical else "work-actors-v1",
           "navigation_layout": "central-obstacle-v1" if family == "crowded" else "open-v1",
           "max_frames": 6000 if physical else last}
    for index in range(8 if family == "opposing" else 1):
        yield {"type": "command", "frame": 0, "time_ms": 0,
               "name": "PGOrderMoveTo" if family == "opposing" else "context-ground",
               "actor": MOVERS[index] if family == "opposing" else 0,
               "destination": [48 if index < 4 else 8, 18 + index % 4 * 6] if family == "opposing" else [600, 260] if physical else [500, 300],
               "rng_draws_before": 9 + index, "rng_draws_after": 10 + index}
    for frame in range(last + 1):
        arrived = frame >= (12 if physical else 2)
        observed = []
        for actor_id in range(1, count + 1):
            queue = [] if arrived else [[1, 0, 0, 0, 0, 0, 0]]
            if physical:
                observed.append({"id": actor_id, "alive": True, "side": 1, "state": 2,
                                 "x_bits": actor_id, "y_bits": actor_id + 10, "forward_x_bits": 0, "forward_y_bits": 0,
                                 "health_bits": 1120403456, "speed_bits": 0, "group_speed_bits": 0,
                                 "maximum_speed_bits": 0, "group": 0, "in_place": arrived,
                                 "queue": [dict(zip(("type", "phase", "param1_bits", "param2_bits", "param3_bits", "param4", "object"), q)) for q in queue]})
            else:
                observed.append([actor_id, actor_id, actor_id + 10, 0, 0, 1120403456, 0, int(arrived), queue])
        robots = [{"id": i, "alive": True, "x_bits": i, "y_bits": i + 10, "side": 1, "state": 2,
                   "health_bits": 1120403456, "in_place": arrived, "group": 0, "order": 1, "target": 0,
                   "target_is_null": True, "destination": [0, 0], "queue": []} for i in range(1, 5)]
        yield {"type": "snapshot", "state": {"frame": frame, "time_ms": frame * step, "rng_draws": draws,
                                              "robots": robots, "player_groups": [{"slot": 0, "stored_count": count, "members": count, "order": 1, "target": 0}]},
               "formation" if physical else "navigation": {"actors": observed}}
    events = {"initial_rng_draws": draws, "final_rng_draws": draws, "minimum_separation_bits": 0}
    if physical:
        events.update(stationary_frames=20, arrival_ms=12 * step, reference_samples=10, physical_samples=10,
                      assigned=[[58, 24], [54, 20], [54, 28]] + ([[58, 20]] if count == 6 else []))
    else:
        events["movers"] = [{"id": i, "assigned": [index, 0], "first_movement_ms": step, "first_arrival_ms": 2 * step,
                             "x_bits": i, "y_bits": i + 10} for index, i in enumerate(MOVERS)]
    yield {"type": "complete", "frame": last, "time_ms": last * step, "formation" if physical else "events": events}


class NavigationCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="navigation-checker-")
        cls.directory = Path(cls.temporary.name)
        cls.groups = []
        # Literal inventory is independent of the checker, including its exact 40-file size.
        for family in ("intermediate", "trailing-reference", "crowded", "opposing"):
            physical = family in ("intermediate", "trailing-reference")
            for rng, profile in (("pm", "ParkMiller"), ("crt", "LegacyCRT")):
                for step in (10, 16):
                    roles = ("run0", "run1") if physical else ("inactive", "capture0", "capture1")
                    names = [f"n5-{'formation' if physical else 'work'}-{family}-{rng}-{step}-{role}.jsonl" for role in roles]
                    content = "".join(map(encode, synthetic_trace(family, profile, step)))
                    for name in names:
                        (cls.directory / name).write_text(content, encoding="utf-8")
                    cls.groups.append((family, rng, step, names))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.originals = {}
        self.addCleanup(self.restore)

    def restore(self):
        for path, content in self.originals.items():
            if content is None:
                path.unlink()
            else:
                path.write_bytes(content)

    def replace(self, path, content):
        if path not in self.originals:
            self.originals[path] = path.read_bytes() if path.exists() else None
        if content is None:
            path.unlink()
        else:
            path.write_text(content, encoding="utf-8")

    def mutate_group(self, family, edit):
        names = next(names for f, rng, step, names in self.groups if f == family and rng == "pm" and step == 10)
        records = [json.loads(line) for line in (self.directory / names[0]).read_text(encoding="utf-8").splitlines()]
        edit(records)
        content = "".join(map(encode, records))
        for name in names:
            self.replace(self.directory / name, content)

    def mutate_headers(self, key, value):
        for *_, names in self.groups:
            for name in names:
                path = self.directory / name
                header, body = path.read_text(encoding="utf-8").split("\n", 1)
                record = json.loads(header)
                record[key] = value
                self.replace(path, encode(record) + body)

    def run_main(self):
        arguments = ["check_navigation_traces.py", str(self.directory), "--configuration", "Debug", "--compiler", "MinGW",
                     "--target", "EXE", "--cheats", "false", "--revision", REVISION]
        output = io.StringIO()
        with patch.object(sys, "argv", arguments), contextlib.redirect_stdout(output):
            CHECK.main()
        return output.getvalue()

    def rejects(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.run_main()

    def test_complete_inventory_and_representative_controls(self):
        with patch.object(CHECK, "controls", wraps=CHECK.controls) as controls:
            output = self.run_main()
        self.assertIn("40 real recordings, 24 same-profile comparisons and 96 sensitivity controls", output)
        self.assertEqual(len(controls.call_args_list), 16)
        tails = [call.args[2][0] for call in controls.call_args_list if call.args[-1]]
        self.assertEqual(tails, ["intermediate", "trailing-reference", "crowded", "opposing"])

    def test_equal_physical_files_cannot_use_a_string_order_parameter(self):
        def edit(records):
            first = next(record for record in records if record["type"] == "snapshot")
            first["formation"]["actors"][0]["queue"][0]["param4"] = "not-an-integer"
        self.mutate_group("intermediate", edit)
        with patch.object(CHECK.TRACES, "compare_records", side_effect=AssertionError("replay must not decide this")):
            self.rejects("invalid physical order integers")

    def test_equal_work_files_cannot_use_a_string_order_parameter(self):
        def edit(records):
            first = next(record for record in records if record["type"] == "snapshot")
            first["navigation"]["actors"][0][8][0][5] = "not-an-integer"
        self.mutate_group("crowded", edit)
        compare = CHECK.TRACES.compare_records
        def reject_corrupt_comparison(left, right):
            if left[0]["fixture"] == "crowded-destination-v1":
                raise AssertionError("replay must not decide this")
            return compare(left, right)
        with patch.object(CHECK.TRACES, "compare_records", side_effect=reject_corrupt_comparison):
            self.rejects("invalid work order integers")

    def test_equal_files_cannot_replace_the_session_queue_with_a_string(self):
        def edit(records):
            first = next(record for record in records if record["type"] == "snapshot")
            first["state"]["robots"][0]["queue"] = "not-an-array"
        self.mutate_group("intermediate", edit)
        with patch.object(CHECK.TRACES, "compare_records", side_effect=AssertionError("replay must not decide this")):
            self.rejects("invalid Session queue")

    def test_consistent_missing_frame(self):
        self.mutate_group("intermediate", lambda records: records.pop(3))
        self.rejects("missing/duplicate whole-frame boundary")

    def test_consistent_wrong_frame_time(self):
        self.mutate_group("intermediate", lambda records: records[3]["state"].__setitem__("time_ms", 11))
        self.rejects("missing/duplicate whole-frame boundary")

    def test_consistent_extra_actor_id(self):
        def edit(records):
            for record in records:
                if record["type"] == "snapshot":
                    record["formation"]["actors"][-1]["id"] = 6
        self.mutate_group("intermediate", edit)
        self.rejects("missing/duplicate/renumbered actor, including added movers")

    def test_consistent_work_actor_id(self):
        def edit(records):
            for record in records:
                if record["type"] == "snapshot":
                    record["navigation"]["actors"][-1][0] = 11
        self.mutate_group("crowded", edit)
        self.rejects("missing/duplicate/renumbered actor, including added movers")

    def test_valid_one_file_replay_divergence(self):
        path = self.directory / self.groups[0][-1][1]
        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        # Added actor 5 is outside the original Session's four-robot observation;
        # this remains semantically valid and must reach the replay comparator.
        records[2]["formation"]["actors"][4]["x_bits"] ^= 1
        self.replace(path, "".join(map(encode, records)))
        self.rejects(r"Divergence at line 3 .*formation\.actors\[4\]\.x_bits")

    def test_consistent_later_group_execution_profile(self):
        self.mutate_group("trailing-reference", lambda records: records[0].__setitem__("compiler_version", "other"))
        self.rejects("corpus execution profiles differ")

    def test_consistent_terminal_mover_ids(self):
        self.mutate_group("crowded", lambda records: records[-1]["events"]["movers"].reverse())
        self.rejects("terminal movers missing")

    def test_consistent_terminal_mover_position(self):
        self.mutate_group("crowded", lambda records: records[-1]["events"]["movers"][0].__setitem__("x_bits", 999))
        self.rejects("terminal mover does not match final state")

    def test_consistent_missing_completion(self):
        self.mutate_group("intermediate", lambda records: records.pop())
        self.rejects("missing completion record")

    def test_consistent_completion_boundary(self):
        self.mutate_group("intermediate", lambda records: records[-1].__setitem__("frame", 33))
        self.rejects("completion must match the last snapshot boundary")

    def test_consistent_record_after_completion(self):
        self.mutate_group("intermediate", lambda records: records.append(copy.deepcopy(records[-1])))
        self.rejects("record after completion")

    def test_consistent_header_profile(self):
        self.mutate_headers("configuration", "Release")
        self.rejects("build mismatch")

    def test_consistent_header_revision(self):
        self.mutate_headers("revision", "2" * 40)
        self.rejects("stale or dirty compiled source identity")

    def test_consistent_dirty_header(self):
        self.mutate_headers("dirty", 1)
        self.rejects("stale or dirty compiled source identity")

    def test_consistent_header_rng(self):
        self.mutate_headers("rng", "LegacyCRT")
        self.rejects("fixture/RNG mismatch")

    def test_consistent_header_seed(self):
        self.mutate_headers("seed", 82)
        self.rejects("seed/timestep mismatch")

    def test_missing_inventory(self):
        self.replace(self.directory / self.groups[0][-1][0], None)
        self.rejects("expected 40 N5 files; missing=")

    def test_extra_inventory(self):
        self.replace(self.directory / "n5-unexpected.jsonl", "not a recording\n")
        self.rejects("expected 40 N5 files; missing=")

    def test_consistent_initial_rng(self):
        def edit(records):
            for record in records:
                if record["type"] == "snapshot":
                    record["state"]["rng_draws"] = 11
            records[-1]["formation"].update(initial_rng_draws=11, final_rng_draws=11)
        self.mutate_group("intermediate", edit)
        self.rejects("initial snapshot does not follow actual commands")

    def test_consistent_command_rng_chain(self):
        self.mutate_group("opposing", lambda records: records[2].__setitem__("rng_draws_before", 11))
        self.rejects("command RNG boundaries are discontinuous")

    def test_consistent_command_rng_range(self):
        self.mutate_group("intermediate", lambda records: records[1].__setitem__("rng_draws_before", 11))
        self.rejects("invalid actual command RNG boundary")

    def test_consistent_terminal_rng(self):
        self.mutate_group("intermediate", lambda records: records[-1]["formation"].__setitem__("initial_rng_draws", 11))
        self.rejects("terminal RNG boundaries mismatch")


class QueueScalarContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.arguments = SimpleNamespace(configuration="Debug", compiler="MinGW", target="EXE",
                                        cheats=False, revision=REVISION)
        cls.records = {physical: list(synthetic_trace(family, "ParkMiller", 10))
                       for physical, family in ((True, "intermediate"), (False, "crowded"))}
        for physical, records in cls.records.items():
            cls.validate(records, physical)

    @classmethod
    def validate(cls, records, physical):
        CHECK.validate(records, "intermediate" if physical else "crowded", physical, "pm", 10, cls.arguments)

    def changed_queue(self, physical, edit, session=False):
        records = self.records[physical]
        first = next(i for i, record in enumerate(records) if record["type"] == "snapshot")
        def change(snapshot):
            actor = snapshot["state"]["robots"][0] if session else CHECK.actors(snapshot, physical)[0]
            edit(actor, "queue" if session or physical else 8)
        return CHECK.changed(records, first, change)

    def reject_queue(self, physical, edit, message, session=False):
        records = self.changed_queue(physical, edit, session)
        with self.assertRaisesRegex(ValueError, message):
            self.validate(records, physical)

    def test_named_order_scalar_types_and_integer_width(self):
        for key in ("type", "phase", "param4"):
            for value in (True, False, 0.0, "0", None, -0x80000001, 0x80000000):
                with self.subTest(key=key, value=value):
                    self.reject_queue(True, lambda actor, q: actor[q][0].__setitem__(key, value),
                                      "invalid physical order integers")

    def test_work_order_scalar_types_and_integer_width(self):
        for index in (0, 1, 5):
            for value in (True, False, 0.0, "0", None, -0x80000001, 0x80000000):
                with self.subTest(index=index, value=value):
                    self.reject_queue(False, lambda actor, q: actor[q][0].__setitem__(index, value),
                                      "invalid work order integers")

    def test_named_and_work_parameter_bit_widths(self):
        for physical in (True, False):
            fields = ("param1_bits", "param2_bits", "param3_bits") if physical else (2, 3, 4)
            for field in fields:
                for value in (True, -1, 0x100000000, 0.0, "0", None):
                    with self.subTest(physical=physical, field=field, value=value):
                        self.reject_queue(physical, lambda actor, q: actor[q][0].__setitem__(field, value),
                                          "invalid order bits" if physical else "invalid work order bits")

    def test_named_order_shape(self):
        for value in (None, [], 1, {"type": 1},
                      dict(self.records[True][2]["formation"]["actors"][0]["queue"][0], extra=0)):
            with self.subTest(value=value):
                self.reject_queue(True, lambda actor, q: actor.__setitem__(q, [value]),
                                  "incomplete physical order")
        for queue in (None, "queue", {}):
            with self.subTest(queue=queue):
                self.reject_queue(True, lambda actor, q: actor.__setitem__(q, queue), "missing physical queue")

    def test_work_order_shape(self):
        for value in (None, {}, 1, [1, 0, 0, 0, 0, 0], [1, 0, 0, 0, 0, 0, 0, 0]):
            with self.subTest(value=value):
                self.reject_queue(False, lambda actor, q: actor.__setitem__(q, [value]), "incomplete work order")
        for queue in (None, "queue", {}):
            with self.subTest(queue=queue):
                self.reject_queue(False, lambda actor, q: actor.__setitem__(q, queue),
                                  "invalid work group/arrival/queue")

    def test_named_and_work_objects_are_integer_actor_ids(self):
        for physical, count in ((True, 5), (False, 10)):
            field = "object" if physical else 6
            for value in (True, False, 0.0, "0", -1, count + 1):
                with self.subTest(physical=physical, value=value):
                    self.reject_queue(physical, lambda actor, q: actor[q][0].__setitem__(field, value),
                                      "unnormalized order object" if physical else "unnormalized work order object")

    def test_session_queue_shape_and_scalar_types(self):
        for queue in (None, "queue", {}):
            with self.subTest(queue=queue):
                self.reject_queue(True, lambda actor, q: actor.__setitem__(q, queue),
                                  "invalid Session queue", session=True)
        for order in (None, {}, [1, 0, 0, 0], [1, 0, 0, 0, 0, 0]):
            with self.subTest(order=order):
                self.reject_queue(True, lambda actor, q: actor.__setitem__(q, [order]),
                                  "incomplete Session order", session=True)
        for index in (0, 1):
            for value in (True, 0.0, "0", -0x80000001, 0x80000000):
                order = [1, 0, 0, 0, 0]
                order[index] = value
                with self.subTest(index=index, value=value):
                    self.reject_queue(True, lambda actor, q: actor.__setitem__(q, [order]),
                                      "invalid Session order integers", session=True)
        for index in (2, 3):
            for value in (True, 0.0, -1, 0x100000000):
                order = [1, 0, 0, 0, 0]
                order[index] = value
                with self.subTest(index=index, value=value):
                    self.reject_queue(True, lambda actor, q: actor.__setitem__(q, [order]),
                                      "invalid Session order bits", session=True)
        for value in (True, False, 0.0, "0", -2, 5):
            with self.subTest(value=value):
                self.reject_queue(True, lambda actor, q: actor.__setitem__(q, [[1, 0, 0, 0, value]]),
                                  "unnormalized Session order object", session=True)

    def test_representable_parameters_and_object_namespaces_remain_supported(self):
        for physical in (True, False):
            for mode in (-0x80000000, -1, 0, 2, 0x7fffffff):
                def edit(actor, q):
                    if physical:
                        actor[q][0].update(param3_bits=0x420c0000, param4=mode)
                    else:
                        actor[q][0][4:6] = [0x420c0000, mode]
                self.validate(self.changed_queue(physical, edit), physical)
        for object_id in (-1, 0, 1, 4):
            records = self.changed_queue(True, lambda actor, q: actor.__setitem__(q, [[1, 0, 0, 0, object_id]]),
                                         session=True)
            self.validate(records, True)


if __name__ == "__main__":
    unittest.main()
