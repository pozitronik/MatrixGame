"""Contract checks for retained selection observations, independent of replay equality."""
import copy
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location("selection_traces", Path(__file__).resolve().parents[1] / "tools/check-selection-traces.py")
selection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection)


def recording(profile):
    step = profile["step"]
    ticks = 6000 // step
    header = dict(type="header", schema=1, fixture=selection.FIXTURE, rng=profile["rng"], seed=80,
                  step_ms=step, configuration="Release", compiler="MinGW", revision="candidate",
                  precision_control=131072, dirty=0, building_first=profile["building_first"], click=profile["click"],
                  actor_ids={"robot": 1, "building": 5, "cannon": 6}, audio="unavailable",
                  build_exe=True, cheats=False, trace=False,
                  area=[100, 200, 101, 201] if profile["click"] else [70, 170, 130, 230])
    records = [header]

    def snapshot(phase, frame=0):
        initial = phase == "initial"
        live = phase == "live-selected"
        building = phase in ("building-selected-dying", "retirement-tick", "building-selected-retired")
        alive = phase in selection.PHASES[:4]
        retired = frame >= 100 or phase in selection.PHASES[6:]
        robots = []
        for entity in range(1, 5):
            robot = dict(id=entity, alive=entity != 1 or alive)
            if robot["alive"]:
                robot.update(x_bits=0, y_bits=0, side=1 if entity < 3 else 2, state=2, health_bits=0,
                             in_place=False, group=-1, order=-1, target=0, target_is_null=True, destination=[-1, -1], queue=[])
            robots.append(robot)
        state = dict(frame=frame, time_ms=frame * step, rng_draws=1, robots=robots, player_groups=[])
        observed = dict(phase=phase, kind=6 if initial else 1 if live else 2 if building else 4,
                        active=5 if building else 0, current_object=1 if initial or live else 0, current_index=0,
                        ordered_members=[1, 2] if initial else [1] if live else [], temporary_members=[1, 2] if initial else [],
                        core_object=0 if retired else 1, actor_state=-1 if retired else 2 if alive else 5,
                        enemy_kills=0 if alive else 1)
        records.append(dict(type="snapshot", state=state, selection=observed))

    def command(index):
        mixed = [6, 5, 1] if profile["building_first"] else [1, 5, 6]
        visible = [[], [6], mixed, [], mixed, [], [6, 5] if profile["building_first"] else [5, 6], []]
        frame = 0 if index < 5 else ticks
        record = dict(type="command", name=selection.COMMANDS[index], frame=frame, time_ms=frame * step, visible=visible[index])
        if index in (2, 4, 6):
            record["area"] = header["area"]
        elif index == 1:
            record["area"] = [80, 100, 120, 140]
        records.append(record)

    snapshot("initial")
    for index in range(5):
        command(index)
        snapshot(selection.PHASES[index + 1])
    for frame in range(1, ticks + 1):
        snapshot("retirement-tick", frame)
    for index in range(5, 8):
        command(index)
        snapshot(selection.PHASES[index + 1], ticks)
    records.append(dict(type="complete", frame=ticks, time_ms=6000))
    return records


def write_recording(path, records):
    path.write_text("".join(json.dumps(record) + "\n" for record in records), encoding="utf-8")


def write_corpus(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for name, profile in selection.inventory():
        for replay in (0, 1):
            write_recording(directory / f"{name}-{replay}.jsonl", recording(profile))


class SelectionTraceTests(unittest.TestCase):
    profile = dict(rng="ParkMiller", step=16, building_first=False, click=True)

    def checked(self, records, **profile):
        parsed = selection.trace.read_records(io.StringIO("\n".join(json.dumps(r) for r in records)))
        selection.validate(parsed, **(profile or self.profile))

    def test_all_profiles(self):
        for _, profile in selection.inventory():
            with self.subTest(profile=profile):
                self.checked(recording(profile), **profile)

    def test_profile_is_checked_without_replay(self):
        for key, bad in (("fixture", "wrong"), ("rng", "LegacyCRT"), ("seed", 81), ("click", False),
                         ("building_first", True), ("actor_ids", {"robot": 2}), ("audio", "available")):
            records = recording(self.profile)
            records[0][key] = bad
            with self.subTest(key=key), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_expected_build_identity(self):
        records = recording(self.profile)
        identity = dict(revision="candidate", configuration="Release", compiler="MinGW", target="EXE", cheats=False, traced=False)
        selection.validate(records, **self.profile, **identity)
        for key, wrong in (("revision", "other"), ("configuration", "Debug"), ("compiler", "MSVC"),
                           ("target", "DLL"), ("cheats", True), ("traced", True)):
            bad = {**identity, key: wrong}
            with self.subTest(key=key), self.assertRaises(selection.trace.TraceError):
                selection.validate(records, **self.profile, **bad)

    def test_revision_requires_clean_integer_dirty_marker(self):
        for dirty in (1, -1, True, False, None):
            records = recording(self.profile)
            records[0]["dirty"] = dirty
            with self.subTest(dirty=dirty), self.assertRaisesRegex(selection.trace.TraceError, r"header\.dirty"):
                selection.validate(records, **self.profile, revision="candidate")
        records = recording(self.profile)
        del records[0]["dirty"]
        with self.assertRaisesRegex(selection.trace.TraceError, r"header\.dirty"):
            selection.validate(records, **self.profile, revision="candidate")
        records[0]["dirty"] = 1
        self.checked(records)  # Unpinned source observations remain inspectable.

    def reject_correlated_change(self, change, message):
        """Both replay files have the same defect, so equality cannot reject it."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            write_corpus(directory)
            name, profile = next(selection.inventory())
            for replay in (0, 1):
                records = recording(profile)
                change(records)
                write_recording(directory / f"{name}-{replay}.jsonl", records)
            with self.assertRaisesRegex(selection.trace.TraceError, message):
                selection.check_directory(directory)

    def test_correlated_area_width_requires_selected_route(self):
        def widen(records):
            records[0]["area"][2] += 1
            for record in records:
                if record.get("name") in ("select-mixed-live", "select-mixed-dying", "select-after-retirement"):
                    record["area"] = records[0]["area"].copy()
        self.reject_correlated_change(widen, "selection area does not match route")

    def test_correlated_phase_name_requires_schedule(self):
        def rename(records):
            record = next(record for record in records if record.get("selection", {}).get("phase") == "cleared-initial")
            record["selection"]["phase"] = "renamed-cleared"
        self.reject_correlated_change(rename, "selection phase schedule differs")

    def test_correlated_tick_boundary_requires_exact_frame(self):
        def shift(records):
            state = next(record["state"] for record in records if record.get("selection", {}).get("phase") == "retirement-tick" and record["state"]["frame"] == 50)
            state["frame"] += 1
            state["time_ms"] += records[0]["step_ms"]
        self.reject_correlated_change(shift, "snapshot boundary differs")

    def test_correlated_dying_phase_requires_dying_state(self):
        def revive_state(records):
            next(record for record in records if record.get("selection", {}).get("phase") == "dying-unselected")["selection"]["actor_state"] = 2
        self.reject_correlated_change(revive_state, "dying actor lifetime differs")

    def test_correlated_final_phases_require_prior_retirement(self):
        def retain_core(records):
            for record in records:
                if record.get("selection", {}).get("phase") in ("retirement-tick", *selection.PHASES[6:]):
                    record["selection"].update(core_object=1, actor_state=5)
        self.reject_correlated_change(retain_core, "actor did not retire before final selection")

    def test_boolean_does_not_substitute_for_identity(self):
        for where in ("header", "command", "robot", "selection"):
            records = recording(self.profile)
            if where == "header":
                records[0]["actor_ids"]["robot"] = True
            elif where == "command":
                next(r for r in records if r.get("name") == "select-mixed-live")["visible"][0] = True
            elif where == "robot":
                records[1]["state"]["robots"][0]["id"] = True
            else:
                records[1]["selection"]["current_object"] = True
            with self.subTest(where=where), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_selection_fields_are_checked_without_replay(self):
        for key, bad in (("kind", 2), ("active", 5), ("current_object", 0), ("current_index", 1),
                         ("ordered_members", [2, 1]), ("temporary_members", []), ("core_object", 0),
                         ("actor_state", 5), ("enemy_kills", 1)):
            records = recording(self.profile)
            records[1]["selection"][key] = bad
            with self.subTest(key=key), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_visible_order_and_command_boundary(self):
        for key, bad in (("visible", [5, 1, 6]), ("frame", 1), ("time_ms", 1), ("area", [1, 2, 3, 4]), ("name", "other")):
            records = recording(self.profile)
            command = next(r for r in records if r.get("name") == "select-mixed-live")
            command[key] = bad
            with self.subTest(key=key), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_interleaved_command_order(self):
        records = recording(self.profile)
        records[2], records[3] = records[3], records[2]
        with self.assertRaisesRegex(selection.trace.TraceError, "ordering"):
            self.checked(records)

    def test_lifetime_cannot_reappear(self):
        records = recording(self.profile)
        tick = next(r for r in records if r.get("selection", {}).get("phase") == "retirement-tick" and r["state"]["frame"] == 101)
        tick["selection"].update(core_object=1, actor_state=5)
        with self.assertRaisesRegex(selection.trace.TraceError, "reappeared"):
            self.checked(records)

    def test_missing_tick_and_phase(self):
        for index in (1, 12):
            records = recording(self.profile)
            del records[index]
            with self.subTest(index=index), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_invalid_completion_and_tail(self):
        for change in ("truncate", "boundary", "extra"):
            records = recording(self.profile)
            if change == "truncate":
                records.pop()
            elif change == "boundary":
                records[-1]["frame"] += 1
            else:
                records.append(copy.deepcopy(records[-1]))
            with self.subTest(change=change), self.assertRaises(selection.trace.TraceError):
                self.checked(records)

    def test_pair_equality_is_additional_to_semantics(self):
        left = recording(self.profile)
        right = copy.deepcopy(left)
        right[1]["state"]["extra_observation"] = 1
        self.checked(right)
        self.assertIn("extra_observation", selection.trace.compare_records(left, right))

    def test_directory_requires_complete_inventory(self):
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaisesRegex(selection.trace.TraceError, "inventory"):
                selection.check_directory(Path(name))

    def test_main_accepts_complete_corpus_with_build_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            write_corpus(directory)
            output = io.StringIO()
            arguments = ["check-selection-traces.py", str(directory), "--revision", "candidate",
                         "--configuration", "Release", "--compiler", "MinGW", "--target", "EXE",
                         "--cheats", "false", "--trace", "false"]
            with mock.patch.object(selection.sys, "argv", arguments), contextlib.redirect_stdout(output):
                self.assertEqual(selection.main(), 0)
            self.assertEqual(output.getvalue(), "Validated 32 mixed-selection recordings and 16 exact session replay pairs.\n")

    def test_directory_rejects_replay_divergence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            write_corpus(directory)
            name, profile = next(selection.inventory())
            records = recording(profile)
            records[1]["state"]["extra_observation"] = 1
            write_recording(directory / f"{name}-1.jsonl", records)
            with self.assertRaisesRegex(selection.trace.TraceError, "replay divergence: .*extra_observation"):
                selection.check_directory(directory)

    def test_directory_rejects_reference_divergence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            write_corpus(directory)
            write_corpus(reference)
            self.assertEqual(selection.check_directory(directory, reference=reference), 32)
            name, profile = next(selection.inventory())
            records = recording(profile)
            records[1]["state"]["extra_observation"] = 1
            write_recording(reference / f"{name}-0.jsonl", records)
            with self.assertRaisesRegex(selection.trace.TraceError, "reference divergence: .*extra_observation"):
                selection.check_directory(directory, reference=reference)

    def test_directory_rejects_incomplete_reference_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            write_corpus(directory)
            write_corpus(reference)
            next(reference.glob("*.jsonl")).unlink()
            with self.assertRaisesRegex(selection.trace.TraceError, "reference inventory differs"):
                selection.check_directory(directory, reference=reference)

    def test_shared_session_observations_cannot_be_dropped(self):
        for field in ("robots", "player_groups", "x_bits", "queue"):
            records = recording(self.profile)
            state = records[1]["state"]
            if field in ("robots", "player_groups"):
                del state[field]
            else:
                del state["robots"][0][field]
            with self.subTest(field=field), self.assertRaises(selection.trace.TraceError):
                self.checked(records)


if __name__ == "__main__":
    unittest.main()
