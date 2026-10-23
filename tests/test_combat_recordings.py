"""Synthetic recording contracts; production combat outcomes remain C++ assertions."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location("combat_recordings", Path(__file__).resolve().parents[1] / "tools/check-combat-recordings.py")
combat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(combat)


def recording(profile):
    family, step = profile["family"], profile["step"]
    turret = family == "cannon-retarget"
    removed = 100 if turret else (1600 if family == "plasma-flight" else 1000) // step
    final = 200 if turret else removed + 2000 // step
    header = dict(type="header", schema=1, fixture="synthetic-armed-boxes-v2", combat_observation="status-health-v1",
                  scenario=family, rng=profile["rng"], seed=82, step_ms=step, initially_selected=not turret,
                  max_frames=3000 if turret else 1000, audio="unavailable", compiler="MinGW", compiler_version="13.2.0",
                  configuration="Release", precision_control=131072, build_exe=True, cheats=False, assert_off=True,
                  revision="candidate", dirty=0, render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720],
                  delayed_damage="Corrected", group_counts="Corrected", place_containment="Corrected", zero_x_trace="Corrected")
    records = [header]
    for frame in range(final + 1):
        if frame == removed or (not turret and frame == 0):
            name = "remove-target(1)" if turret else "context-target(3)" if frame == 0 else "remove-shooter(1)"
            records.append(dict(type="command", frame=frame, name=name, rng_draws_before=0, rng_draws_after=0,
                                accepted=True, resulting_order=-1, resulting_target=0, resulting_destination=[-1, -1]))
        robots = []
        for entity in range(1, 5):
            robot = dict(id=entity, alive=entity != 1 or frame < removed)
            if robot["alive"]:
                robot.update(x_bits=0, y_bits=0, health_bits=1084227584, side=1 if entity < 3 else 2, state=2,
                             in_place=False, group=-1, order=-1, target=0, target_is_null=True, destination=[-1, -1], queue=[])
            robots.append(robot)
        state = dict(frame=frame, time_ms=frame * step, rng_draws=0, robots=robots, player_groups=[])
        observation = dict(health_bits=1084227584, ablaze_ttl=0, shorted_ttl=0, robot_state=2)
        if turret:
            observation.update(cannon_target_is_patient=False, yaw_bits=0, pitch_bits=0, turret_yaw_bits=0, cannon_health_bits=1084227584)
        elif frame < removed:
            observation["heat"] = 0
        records.append(dict(type="snapshot", state=state, combat=observation))
    records.append(dict(type="complete", frame=final, time_ms=final * step))
    return records


def write_corpus(directory):
    for name, profile in combat.inventory():
        text = "\n".join(json.dumps(record) for record in recording(profile)) + "\n"
        for replay in (0, 1):
            (directory / f"{name}-{replay}.jsonl").write_text(text, encoding="utf-8")


class CombatRecordingTests(unittest.TestCase):
    profile = dict(family="laser-attack", rng="ParkMiller", step=16)

    def checked(self, records, **profile):
        parsed = combat.trace.read_records(io.StringIO("\n".join(json.dumps(r) for r in records)))
        combat.validate(parsed, **(profile or self.profile))

    def test_all_eight_profiles(self):
        for _, profile in combat.inventory():
            with self.subTest(profile=profile):
                self.checked(recording(profile), **profile)

    def test_header_fixture_and_declared_profile(self):
        for key, bad in (("fixture", "wrong"), ("scenario", "plasma-flight"), ("seed", 83), ("rng", "LegacyCRT"),
                         ("step_ms", 10), ("audio", "available"), ("initially_selected", False)):
            records = recording(self.profile)
            records[0][key] = bad
            with self.subTest(key=key), self.assertRaises(combat.trace.TraceError):
                self.checked(records)

    def test_compiled_identity(self):
        records = recording(self.profile)
        identity = dict(revision="candidate", configuration="Release", compiler="MinGW", target="EXE", cheats=False)
        combat.validate(records, **self.profile, **identity)
        for key, bad in (("revision", "other"), ("configuration", "Debug"), ("compiler", "MSVC"), ("target", "DLL"), ("cheats", True)):
            with self.subTest(key=key), self.assertRaises(combat.trace.TraceError):
                combat.validate(records, **self.profile, **{**identity, key: bad})
        records[0]["dirty"] = 1
        with self.assertRaisesRegex(combat.trace.TraceError, "dirty"):
            combat.validate(records, **self.profile, **identity)

    def test_equal_pair_cannot_omit_session_or_combat_observations(self):
        for field in ("queue", "x_bits", "player_groups", "health_bits", "heat", "ablaze_ttl"):
            left = recording(self.profile)
            first = next(record for record in left if record["type"] == "snapshot")
            if field in ("queue", "x_bits"):
                del first["state"]["robots"][0][field]
            elif field == "player_groups":
                del first["state"][field]
            else:
                del first["combat"][field]
            right = copy.deepcopy(left)
            self.assertIsNone(combat.trace.compare_records(left, right))
            for records in (left, right):
                with self.subTest(field=field), self.assertRaises(combat.trace.TraceError):
                    self.checked(records)

    def test_turret_observations_are_required(self):
        profile = dict(family="cannon-retarget", rng="ParkMiller", step=10)
        for key in combat.TURRET_FIELDS:
            records = recording(profile)
            del next(r for r in records if r["type"] == "snapshot")["combat"][key]
            with self.subTest(key=key), self.assertRaises(combat.trace.TraceError):
                self.checked(records, **profile)

    def test_command_result_and_rng_are_required(self):
        for key, bad in (("accepted", False), ("resulting_order", True), ("resulting_destination", []),
                         ("rng_draws_before", -1), ("rng_draws_after", 1)):
            records = recording(self.profile)
            records[1][key] = bad
            with self.subTest(key=key), self.assertRaises(combat.trace.TraceError):
                self.checked(records)

    def test_command_cannot_follow_its_observation(self):
        records = recording(self.profile)
        records[1], records[2] = records[2], records[1]
        with self.assertRaisesRegex(combat.trace.TraceError, "ordering"):
            self.checked(records)

    def test_patient_identity_and_retirement(self):
        for edit in (lambda r: r["state"]["robots"][0].update(alive=False),
                     lambda r: r["combat"].update(health_bits=0)):
            records = recording(self.profile)
            edit(next(r for r in records if r["type"] == "snapshot"))
            with self.assertRaises(combat.trace.TraceError):
                self.checked(records)

    def test_missing_boundary_or_complete_and_invalid_tail(self):
        for change in ("snapshot", "complete", "tail"):
            records = recording(self.profile)
            if change == "snapshot":
                records.pop(3)
            elif change == "complete":
                records.pop()
            else:
                records.append(copy.deepcopy(records[-1]))
            with self.subTest(change=change), self.assertRaises(combat.trace.TraceError):
                self.checked(records)

    def test_empty_inventory_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(combat.trace.TraceError, "inventory"):
                combat.check_directory(Path(directory))

    def test_complete_directory_and_equally_stripped_pair(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            write_corpus(directory)
            self.assertEqual(combat.check_directory(directory), 16)
            for replay in (0, 1):
                path = directory / f"laser-attack-pm-16-{replay}.jsonl"
                records = combat.read(path)
                del next(record for record in records if record["type"] == "snapshot")["state"]["robots"][0]["queue"]
                path.write_text("\n".join(json.dumps(record) for record in records), encoding="utf-8")
            with self.assertRaisesRegex(combat.trace.TraceError, "Session observations"):
                combat.check_directory(directory)

    def test_different_corpus_profile_cannot_hide_in_equal_pair(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            write_corpus(directory)
            for replay in (0, 1):
                path = directory / f"plasma-flight-pm-10-{replay}.jsonl"
                records = combat.read(path)
                records[0]["precision_control"] = 0
                path.write_text("\n".join(json.dumps(record) for record in records), encoding="utf-8")
            with self.assertRaisesRegex(combat.trace.TraceError, "corpus profile differs: precision_control"):
                combat.check_directory(directory)

    def test_retained_reference_reports_first_changed_field(self):
        with tempfile.TemporaryDirectory() as name:
            reference, candidate = Path(name) / "reference", Path(name) / "candidate"
            reference.mkdir()
            candidate.mkdir()
            write_corpus(reference)
            write_corpus(candidate)
            self.assertEqual(combat.check_directory(candidate, reference=reference), 16)
            for replay in (0, 1):
                path = candidate / f"laser-attack-pm-10-{replay}.jsonl"
                records = combat.read(path)
                first = next(record for record in records if record["type"] == "snapshot")
                first["state"]["robots"][0]["x_bits"] = 1065353216
                path.write_text("\n".join(json.dumps(record) for record in records), encoding="utf-8")
            with self.assertRaisesRegex(combat.trace.TraceError, r"reference divergence:.*x_bits"):
                combat.check_directory(candidate, reference=reference)


if __name__ == "__main__":
    unittest.main()
