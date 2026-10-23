"""Independent semantic guards for equally incomplete fire-feedback recordings."""
import copy
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("fire_feedback_traces", Path(__file__).with_name("check_fire_feedback_traces.py"))
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def observation():
    robots = [{"id": i, "alive": True, "x_bits": 1120403456, "y_bits": 1128792064, "side": 1 if i < 3 else 2,
               "state": 2, "health_bits": 1120403456, "in_place": False, "group": 0 if i < 3 else -1,
               "order": 1 if i < 3 else -1, "target": 0, "target_is_null": True, "destination": [8, 8],
               "queue": [[1, 0, 1090519040, 1090519040, 0]]} for i in (1, 2, 3)] + [{"id": 4, "alive": False}]
    feedback = [{"id": i, "place": -1, "place_not_found": -1000000, "bad_places": [], "enemy_count": 0,
                 "target": -1, "attack_target": 0, "target_change": 0, "last_fire": 0, "last_hit": 0,
                 "health_bits": 1120403456, "moving": False, "move": [-1, -1]} for i in (1, 2, 3)]
    return {"type": "snapshot", "state": {"frame": 2, "time_ms": 20, "rng_draws": 300, "robots": robots,
            "player_groups": [{"slot": 0, "stored_count": 2, "members": 2, "order": 1, "target": 0}]}, "feedback": feedback, "war": False, "ally_on_ray": False}


def terminal():
    return {"type": "complete", "frame": 2, "time_ms": 20, "feedback_result": {"first_bad": 0, "first_relocation": 0, "first_recovery_hit": 0, "final_health_bits": 1120403456}}


class FeedbackSemantics(unittest.TestCase):
    def test_signed_unassigned_time_and_nonnull_unknown_target_are_preserved(self):
        CHECK.observations(observation())

    def test_both_equal_files_missing_live_actor_fail_independently(self):
        left, right = observation(), observation()
        left["feedback"].pop()
        right["feedback"].pop()
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "feedback actor inventory"):
                CHECK.observations(value)

    def test_both_equal_files_missing_custom_field_fail_independently(self):
        left, right = observation(), observation()
        del left["feedback"][0]["last_fire"]
        del right["feedback"][0]["last_fire"]
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "missing custom feedback"):
                CHECK.observations(value)

    def test_retired_session_id_cannot_disappear_or_be_reused(self):
        value = observation()
        value["state"]["robots"].pop()
        with self.assertRaisesRegex(ValueError, "Session actor inventory"):
            CHECK.observations(value)
        value = observation()
        value["state"]["robots"][-1]["alive"] = True
        with self.assertRaisesRegex(ValueError, "live/retired"):
            CHECK.observations(value)

    def test_boolean_id_or_timestamp_is_not_an_integer(self):
        for key in ("id", "last_fire"):
            value = observation()
            value["feedback"][0][key] = True
            with self.assertRaises(ValueError):
                CHECK.observations(value)

    def test_both_equal_files_missing_live_position_queue_or_group_fail(self):
        for field in ("x_bits", "queue", "group"):
            left, right = observation(), observation()
            del left["state"]["robots"][0][field]
            del right["state"]["robots"][0][field]
            self.assertEqual(left, right)
            for value in (left, right):
                with self.assertRaisesRegex(ValueError, "missing live Session actor field"):
                    CHECK.observations(value)

    def test_both_equal_files_missing_player_groups_or_group_fields_fail(self):
        for field in ("player_groups", "members", "order"):
            left, right = observation(), observation()
            for value in (left, right):
                if field == "player_groups":
                    del value["state"][field]
                else:
                    del value["state"]["player_groups"][0][field]
            self.assertEqual(left, right)
            for value in (left, right):
                with self.assertRaisesRegex(ValueError, "missing Session"):
                    CHECK.observations(value)

    def test_live_session_types_full_queues_and_group_inventory_are_required(self):
        edits = (lambda r: r["state"]["robots"][0].__setitem__("x_bits", True),
                 lambda r: r["state"]["robots"][0].__setitem__("side", "1"),
                 lambda r: r["state"]["robots"][0].__setitem__("destination", [8]),
                 lambda r: r["state"]["robots"][0].__setitem__("queue", [[1, 0, 0, 0]]),
                 lambda r: r["state"]["robots"][0].__setitem__("queue", [[1, 0, True, 0, 0]]),
                 lambda r: r["state"].__setitem__("player_groups", []),
                 lambda r: r["state"]["player_groups"][0].__setitem__("members", True))
        for edit in edits:
            value = observation()
            edit(value)
            with self.assertRaises(ValueError):
                CHECK.observations(value)

    def test_custom_health_is_bound_to_its_session_actor(self):
        value = observation()
        value["feedback"][2]["health_bits"] ^= 1
        with self.assertRaisesRegex(ValueError, "health differs"):
            CHECK.observations(value)

    def test_movement_war_and_ray_are_required_typed_observations(self):
        for key in ("moving", "move", "war", "ally_on_ray"):
            value = observation()
            if key in ("moving", "move"):
                del value["feedback"][0][key]
            else:
                del value[key]
            with self.assertRaises((ValueError, KeyError)):
                CHECK.observations(value)

    def test_both_equal_footers_missing_milestone_fail_independently(self):
        left, right = terminal(), terminal()
        del left["feedback_result"]["first_recovery_hit"]
        del right["feedback_result"]["first_recovery_hit"]
        self.assertEqual(left, right)
        for value in (left, right):
            with self.assertRaisesRegex(ValueError, "terminal feedback"):
                CHECK.completion(value, observation())

    def test_footer_boundary_health_and_milestone_types_are_checked(self):
        CHECK.completion(terminal(), observation())
        for edit in (lambda r: r.__setitem__("frame", 3),
                     lambda r: r["feedback_result"].__setitem__("final_health_bits", 0),
                     lambda r: r["feedback_result"].__setitem__("first_bad", True)):
            value = copy.deepcopy(terminal())
            edit(value)
            with self.assertRaises(ValueError):
                CHECK.completion(value, observation())


REVISION = "a" * 40


def arguments():
    return SimpleNamespace(configuration="Debug", compiler="MinGW", target="EXE", cheats=False, revision=REVISION)


def recording(description):
    """Build the producer's complete boundary/command shape with synthetic observations."""
    family, rng, step, role, context = description
    speed = .1 if family == "obstructed" else .35 if role == "moving" else 0.
    header = {"type": "header", "schema": 1, "fixture": "moving-target-fire-feedback-v1",
              "scenario": f"fire-{family}-{role}", "session_context": context,
              "rng": "ParkMiller" if rng == "pm" else "LegacyCRT", "seed": 84, "step_ms": step,
              "target_speed_bits": struct.unpack("<I", struct.pack("<f", speed))[0],
              "configuration": "Debug", "compiler": "MinGW", "compiler_version": "synthetic-1",
              "build_exe": True, "cheats": False, "revision": REVISION, "dirty": 0,
              "max_frames": 3000, "navigation_layout": "open-v1", "audio": "unavailable",
              "render": "storage-device-no-draw-cursor-surrogate", "viewport": [1280, 720],
              "initially_selected": True, "precision_control": 0, "assert_off": False,
              "delayed_damage": False, "group_counts": False, "place_containment": False, "zero_x_trace": False}
    records = [header]
    draws = 10

    def command(name, frame, advance=0, **inputs):
        nonlocal draws
        records.append({"type": "command", "name": name, "frame": frame, "time_ms": frame * step,
                        "rng_draws_before": draws, "rng_draws_after": draws + advance, **inputs})
        draws += advance

    command("hold-alternative-place", 0, world=[220, 40])
    command("initial-firing-place", 0, advance=1, world=[100, 100])
    patrol = (200 + step - 1) // step
    intervention = (1000 + step - 1) // step
    for frame in range((25000 + step - 1) // step + 1):
        value = observation()
        value["state"].update(frame=frame, time_ms=frame * step, rng_draws=draws)
        records.append(value)
        if frame == patrol:
            command("patrol", frame, advance=1, **{"from": [8, 8], "to": [48, 8]})
        elif role == "moving" and frame == intervention:
            command("cross-firing-line" if family == "obstructed" else "free-alternative-place", frame,
                    world=[220, 140] if family == "obstructed" else [100, 500])
        else:
            continue
        repeated = copy.deepcopy(value)
        repeated["state"]["rng_draws"] = draws
        records.append(repeated)
    end = terminal()
    end.update(frame=frame, time_ms=frame * step)
    end["feedback_result"]["first_bad"] = intervention * step
    records.append(end)
    return records


def dump(records):
    return "".join(json.dumps(record, separators=(",", ":")) + "\n" for record in records)


class CompleteRecordings(unittest.TestCase):
    description = ("ineffective", "pm", 10, "moving", "main-case")

    @classmethod
    def setUpClass(cls):
        cls.valid = recording(cls.description)

    def reject_pair(self, edit, message):
        pair = [copy.deepcopy(self.valid), copy.deepcopy(self.valid)]
        for value in pair:
            edit(value)
        self.assertIsNone(CHECK.TRACES.compare_records(*pair))
        for value in pair:
            with self.assertRaisesRegex(ValueError, message):
                CHECK.validate(value, self.description, arguments())

    def test_valid_recording_for_every_family_rng_step_role_and_context(self):
        for description, _ in CHECK.pairs():
            with self.subTest(description=description):
                value = CHECK.TRACES.read_records(io.StringIO(dump(recording(description))))
                self.assertEqual(CHECK.validate(value, description, arguments()), value[0])

    def test_paired_missing_frame_is_rejected(self):
        self.reject_pair(lambda r: r.__setitem__(slice(None), [v for v in r if v.get("state", {}).get("frame") != 500]),
                         "missing completed map boundary")

    def test_paired_extra_snapshot_is_rejected(self):
        def edit(records):
            index = next(i for i, r in enumerate(records) if r.get("state", {}).get("frame") == 500)
            records.insert(index, copy.deepcopy(records[index]))
        self.reject_pair(edit, "missing/extra command-boundary snapshot")

    def test_paired_command_rng_chain_is_rejected(self):
        def edit(records):
            next(r for r in records if r.get("name") == "patrol")["rng_draws_before"] += 1
        self.reject_pair(edit, "command RNG before differs from preceding observation")

    def test_paired_same_frame_snapshot_rng_is_rejected(self):
        def edit(records):
            index = next(i for i, r in enumerate(records) if r.get("name") == "patrol")
            for record in records[index + 1:-1]:
                if record["type"] == "snapshot":
                    record["state"]["rng_draws"] += 1
                else:
                    record["rng_draws_before"] += 1
                    record["rng_draws_after"] += 1
        self.reject_pair(edit, "command/observation RNG boundary differs")

    def test_paired_command_name_is_rejected(self):
        self.reject_pair(lambda r: next(v for v in r if v.get("name") == "free-alternative-place").update(name="other"),
                         "command schedule/route differs")

    def test_paired_command_time_is_rejected(self):
        self.reject_pair(lambda r: next(v for v in r if v.get("name") == "patrol").update(time_ms=201),
                         "command frame/time mismatch")

    def test_paired_revision_and_dirty_identity_are_rejected(self):
        for field, value in (("revision", "b" * 40), ("dirty", 1)):
            with self.subTest(field=field):
                self.reject_pair(lambda r: r[0].__setitem__(field, value), "stale or dirty compiled source identity")

    def test_paired_measured_intervention_schedule_is_rejected(self):
        self.reject_pair(lambda r: r[-1]["feedback_result"].__setitem__("first_bad", 1010),
                         "measured bad-place intervention schedule differs")

    def test_paired_terminal_health_is_rejected(self):
        self.reject_pair(lambda r: r[-1]["feedback_result"].__setitem__("final_health_bits", 0),
                         "terminal health differs")

    def test_paired_header_contracts_are_rejected(self):
        faults = (("fixture", "other", "scenario/context"), ("session_context", "other", "scenario/context"),
                  ("seed", 85, "RNG/seed/step"), ("target_speed_bits", 0, "declared target speed"),
                  ("configuration", "Release", "build mismatch"), ("build_exe", False, "role/cheats"),
                  ("audio", "other", "fixture execution policy"), ("viewport", [1, 1], "render/selection policy"))
        for field, value, message in faults:
            with self.subTest(field=field):
                self.reject_pair(lambda r: r[0].__setitem__(field, value), message)
        self.reject_pair(lambda r: r[0].pop("zero_x_trace"), "missing effective profile field zero_x_trace")

    def test_paired_command_input_and_patrol_schedule_are_rejected(self):
        self.reject_pair(lambda r: r[1].update(world=[0, 0]), "setup command inputs differ")
        self.reject_pair(lambda r: next(v for v in r if v.get("name") == "patrol").update(to=[0, 0]),
                         "Patrol input/boundary differs")
        self.reject_pair(lambda r: next(v for v in r if v.get("name") == "free-alternative-place").update(world=[0, 0]),
                         "intervention input differs")

    def test_obstructed_intervention_schedule_is_rejected_independently(self):
        description = ("obstructed", "pm", 16, "moving", "main-case")
        pair = [recording(description), recording(description)]
        for records in pair:
            command = next(r for r in records if r.get("name") == "cross-firing-line")
            command["frame"] += 1
            command["time_ms"] += 16
        self.assertIsNone(CHECK.TRACES.compare_records(*pair))
        for records in pair:
            with self.assertRaisesRegex(ValueError, "obstruction command schedule differs"):
                CHECK.validate(records, description, arguments())


class CorpusEntryPoint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        for description, names in CHECK.pairs():
            text = dump(recording(description))
            for name in names:
                (cls.directory / name).write_text(text, encoding="utf8")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def main(self, revision=REVISION):
        argv = ["check_fire_feedback_traces.py", str(self.directory), "--configuration", "Debug", "--compiler", "MinGW",
                "--target", "EXE", "--cheats", "false", "--revision", revision]
        with mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()) as output:
            CHECK.main()
        return output.getvalue()

    @contextlib.contextmanager
    def edited_pair(self, group, edit, both=True):
        names = list(CHECK.pairs())[group][1]
        paths = [self.directory / name for name in names[:2 if both else 1]]
        originals = [p.read_bytes() for p in paths]
        try:
            for path in paths:
                records = [json.loads(line) for line in path.read_text(encoding="utf8").splitlines()]
                edit(records)
                path.write_text(dump(records), encoding="utf8")
            yield
        finally:
            for path, original in zip(paths, originals):
                path.write_bytes(original)

    def test_main_accepts_all_48_files_24_pairs_and_real_controls(self):
        self.assertIn("48 real recordings, 24 same-context pairs and 156 representative controls", self.main())

    def test_main_requires_full_revision(self):
        with self.assertRaisesRegex(ValueError, "require full source revision"):
            self.main("short")

    def test_main_rejects_wrong_revision_in_both_files(self):
        with self.edited_pair(0, lambda r: r[0].update(revision="b" * 40)):
            with self.assertRaisesRegex(ValueError, "stale or dirty compiled source identity"):
                self.main()

    def test_main_requires_exact_inventory(self):
        extra = self.directory / "fire-ineffective-moving-pm-10-4.jsonl"
        extra.write_text("{}\n", encoding="utf8")
        try:
            with self.assertRaisesRegex(ValueError, "expected 48 fire-feedback files.*extra="):
                self.main()
        finally:
            extra.unlink()
        path = self.directory / next(CHECK.pairs())[1][0]
        original = path.read_bytes()
        path.unlink()
        try:
            with self.assertRaisesRegex(ValueError, "expected 48 fire-feedback files.*missing="):
                self.main()
        finally:
            path.write_bytes(original)

    def test_main_rejects_consistent_pair_with_other_corpus_profile(self):
        with self.edited_pair(1, lambda r: r[0].update(compiler_version="other")):
            with self.assertRaisesRegex(ValueError, "corpus execution profiles differ"):
                self.main()

    def test_main_compares_replay_observations(self):
        with self.edited_pair(0, lambda r: r[3].update(war=True), both=False), mock.patch.object(CHECK, "sensitivity", return_value=0):
            with self.assertRaisesRegex(ValueError, "snapshot boundary 0.*war"):
                self.main()


if __name__ == "__main__":
    unittest.main()
