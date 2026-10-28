"""Synthetic reader contracts; these are not gameplay reference trajectories."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import check_war_detour_traces as CHECK


def episode(change="control", role="first", step=10, rng="pm", caller=None):
    """Use simple source-shaped milestones; real engine files supply the trajectory positives."""
    caller = caller or change
    spec = dict(caller=caller, role=role, change=change, step=step, rng=rng)
    header = dict(type="header", schema=1, fixture="reachable-war-detours-v1", recording="successful-war-detour-v1",
                  navigation_layout="open-v1", revision="a" * 40, dirty=0, configuration="Debug", compiler="MinGW",
                  compiler_version="13.2.0", precision_control=131072, assert_off=False, build_exe=True,
                  cheats=False, trace=False, delayed_damage="Corrected", group_counts="Corrected",
                  place_containment="Corrected", zero_x_trace="Corrected", audio="unavailable",
                  render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720], initially_selected=True,
                  caller=caller, role=role, change=change, rng="ParkMiller" if rng == "pm" else "LegacyCRT",
                  seed=84, step_ms=step, max_frames=24000 // step, change_time_ms=800,
                  initial_leg=[48, 8], patrol_to=[48, 38])
    rounded = lambda time: (time + step - 1) // step * step
    war, detour, reached = rounded(100), rounded(200), rounded(1200)
    death = rounded(10200)
    arrival = rounded(11000 if change == "control" else 5000)
    resumed = death + step if change == "control" else reached + step if change == "retirement" else 800 + step
    records = [header]

    def snapshot(phase, frame, setup=2, intervention=False):
        time = frame * step
        changed = change != "control" and (time > 800 or intervention)
        removed = change == "retirement" and changed
        dip = change == "control" and time == death
        retired = removed or change == "control" and time > death
        live = not (dip or retired)
        pulse_time = min(time, 800) if change != "control" else time
        pulses = min(100, max(0, (pulse_time - 200) // 100))
        health = CHECK.float_bits((200 - 2 * pulses) / 10)
        replaced = change == "replacement" and changed
        on_detour = setup == 2 and time >= detour and not replaced
        moving = setup > 0 and (time < reached or time >= resumed or replaced)
        move = [48, 38] if replaced else [48, 8] if not on_detour or time >= resumed else [16, 22]
        if not moving:
            move = [-1, -1]
        firing = setup == 2 and pulses > 0 and live and not replaced
        environment = 3 if setup == 2 and time >= war and live and not replaced else 0
        position = [100, 200] if phase != "tick" else [495, 100] if time == arrival and not replaced else (
            [500, 400] if time == arrival else [180, 240] if reached <= time < resumed else [120, 200])
        queue = []
        if moving:
            queue.append([1, 0, CHECK.float_bits(move[0]), CHECK.float_bits(move[1]), 0, 0, 0])
        if firing:
            queue.append([5, 3, CHECK.float_bits(240), CHECK.float_bits(280), 0, 0, 0])
        robots, full = [], []
        for identity in range(1, 5):
            alive = live if identity == 3 else True
            robot = dict(id=identity, alive=alive)
            orders = queue if identity == 1 else []
            if alive:
                point = position if identity == 1 else [100, 100] if identity == 2 else (
                    [240, 280] if identity == 3 else [600, 600])
                robot.update(x_bits=CHECK.float_bits(point[0]), y_bits=CHECK.float_bits(point[1]),
                             health_bits=health if identity == 3 else CHECK.float_bits(100),
                             side=1 if identity < 3 else 2,
                             state=2, in_place=identity == 1 and replaced and time == arrival,
                             group=1 if identity == 1 and setup else -1,
                             order=(1 if setup == 1 or replaced else 4) if identity == 1 and setup else -1,
                             target=0, target_is_null=True,
                             destination=([48, 8] if setup == 1 else [48, 38]) if identity == 1 and setup else [-1, -1],
                             queue=[[o[0], o[1], o[2], o[3], o[6]] for o in orders])
            robots.append(robot)
            entry = dict(id=identity, live=alive)
            if alive:
                entry["orders"] = copy.deepcopy(orders)
            full.append(entry)
        core = dict(present=not retired, id=0 if retired else 3, live=live)
        if not retired:
            core.update(state=5 if dip else 2, must_die=dip)
            if live:
                core["health_bits"] = health
        state = dict(frame=frame, time_ms=time, rng_draws=1, robots=robots, player_groups=[])
        value = dict(moving=moving, move=move, fire_order=firing, environment_target=environment,
                     attack_target=environment, previous_firing_target=3 if pulses else 0, pulses=pulses,
                     last_health_bits=health, firing_place=[16, 22] if time >= detour else [-1, -1],
                     target_core=core, full_orders=full)
        record = dict(type="snapshot", phase=phase, state=state, war_detour=value)
        if phase == "tick":
            record["detail"] = dict(time_ms=time, war=int(war <= time < death and not replaced and not removed),
                                    place=1, failed_at=-1, enemy_count=int(live and bool(environment)),
                                    target=int(environment == 3), attack_target=int(environment == 3),
                                    core_live=int(not retired), patrol_from=[48, 8], move=move)
        records.append(record)

    def command(name, frame):
        patrol, remove = name == "setup-patrol", name == "target-retirement"
        records.append(dict(type="command", name=name, frame=frame, time_ms=frame * step,
                            route="player-group" if patrol else "delete" if remove else "context-ground",
                            target=3 if remove else 0,
                            point=[48, 38] if patrol else [-1, -1] if remove else
                            [500, 100] if name == "setup-move" else [500, 400], rng_draws_before=1, rng_draws_after=1))
        snapshot("command", frame, setup=1 if name == "setup-move" else 2, intervention=frame > 0)
    snapshot("initial", 0, setup=0)
    command("setup-move", 0)
    command("setup-patrol", 0)
    for frame in range(1, arrival // step + 1):
        if (frame - 1) * step == 800 and change != "control":
            command("target-retirement" if change == "retirement" else "replacement-move", frame - 1)
        snapshot("tick", frame)
    result = dict(war=war, detour=detour, reached_firing_place=-1 if change == "replacement" else reached,
                  first_hit=rounded(300),
                  terminal=death if change == "control" else 800 + step if change == "retirement" else -1,
                  changed=-1 if change == "control" else 800, resumed=resumed, arrival=arrival,
                  pulses=records[-1]["war_detour"]["pulses"],
                  last_health_bits=records[-1]["war_detour"]["last_health_bits"], firing_place=[16, 22])
    records.append(dict(type="complete", frame=arrival // step, time_ms=arrival, rng_draws=1, milestones=result))
    return records, spec


class WarDetourTraceContracts(unittest.TestCase):
    def validated(self, records, spec):
        parsed = CHECK.TRACES.read_records(io.StringIO("\n".join(map(json.dumps, records))))
        CHECK.validate(parsed, spec)
        return parsed

    def test_exact_existing_inventory_and_unique_contexts(self):
        items = list(CHECK.inventory())
        self.assertEqual(32, len(items))
        self.assertEqual(32, len({name for name, _ in items}))
        self.assertEqual({"control": 8, "retirement": 12, "replacement": 12},
                         {caller: sum(s["caller"] == caller for _, s in items) for caller in CHECK.CHANGES})
        self.assertEqual(8, sum(s["role"] == "intact" for _, s in items))
        self.assertFalse(any(s["role"] == "intact" and s["caller"] == "control" for _, s in items))

    def test_source_shaped_endings_and_schedules_are_accepted(self):
        for change in CHECK.CHANGES:
            for step in (10, 16):
                with self.subTest(change=change, step=step):
                    self.validated(*episode(change, step=step))

    def test_compiler_versions_and_trace_are_source_typed(self):
        records, spec = episode()
        for compiler, version in (("MSVC", 1944), ("MinGW", "13.2.0")):
            header = dict(records[0], compiler=compiler, compiler_version=version, trace=True)
            CHECK.header_contract(header, spec)
            for invalid in (None, True, False, 1944.0, "1944" if compiler == "MSVC" else 1944):
                with self.subTest(compiler=compiler, value=invalid), self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.header_contract(dict(header, compiler_version=invalid), spec)
        for value in (None, 0, "false"):
            with self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.header_contract(dict(records[0], trace=value), spec)

    def test_normalized_core_lifetimes_do_not_read_dip_order_storage(self):
        records, _ = episode()
        dip = next(r for r in records if r.get("war_detour", {}).get("target_core", {}).get("state") == 5)
        CHECK.observation(dip)
        retired = next(r for r in records if r.get("war_detour", {}).get("target_core", {}).get("present") is False)
        CHECK.observation(retired)
        for original, field in ((dip, "health_bits"), (retired, "state")):
            changed = copy.deepcopy(original)
            changed["war_detour"]["target_core"][field] = 0
            with self.subTest(field=field), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.observation(changed)
        changed = copy.deepcopy(dip)
        changed["war_detour"]["full_orders"][2]["orders"] = []
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "dead actor"):
            CHECK.observation(changed)

    def test_all_full_order_fields_have_genuine_scalar_types(self):
        records, _ = episode()
        original = next(r for r in records if r.get("war_detour", {}).get("fire_order"))
        for index in range(7):
            for invalid in (True, None, "0", 0.0):
                changed = copy.deepcopy(original)
                changed["war_detour"]["full_orders"][0]["orders"][-1][index] = invalid
                with self.subTest(index=index, invalid=invalid), self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.observation(changed)
        changed = copy.deepcopy(original)
        changed["war_detour"]["full_orders"][0]["orders"][-1].pop(4)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "seven-field"):
            CHECK.observation(changed)

    def test_new_observations_join_the_existing_replay_graph(self):
        records, spec = episode()
        changed = copy.deepcopy(records)
        frame = next(r for r in changed if r.get("war_detour", {}).get("fire_order"))
        frame["war_detour"]["full_orders"][0]["orders"][-1][4] = CHECK.float_bits(1)
        CHECK.observation(frame)
        self.assertIsNotNone(CHECK.TRACES._first_difference(CHECK.projection(records), CHECK.projection(changed)))
        self.assertEqual([r["state"] for r in records if r["type"] == "snapshot"],
                         [r["state"] for r in changed if r["type"] == "snapshot"])
        changed[0]["role"] = "replay"
        replay_spec = dict(spec, role="replay")
        CHECK.validate(changed, replay_spec)
        first_name, replay_name = "war-detour-control-pm-10-first.jsonl", "war-detour-control-pm-10-replay.jsonl"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / first_name).touch()
            (root / replay_name).touch()
            with mock.patch.object(CHECK, "inventory", side_effect=lambda: iter((
                    (first_name, spec), (replay_name, replay_spec)))), mock.patch.object(
                    CHECK, "load", side_effect=lambda path, _: records if path.name == first_name else changed), \
                    mock.patch.object(CHECK, "controls", return_value=0), self.assertRaisesRegex(
                    CHECK.TRACES.TraceError, "original replay differs"):
                CHECK.check(root)

    def test_correlated_observation_omission_needs_semantic_guard(self):
        original, spec = episode()
        for field in original[1]["war_detour"]:
            records = copy.deepcopy(original)
            del records[1]["war_detour"][field]
            self.assertIsNone(CHECK.TRACES.compare_records(records, copy.deepcopy(records)))
            with self.subTest(field=field), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate(records, spec)

    def test_missing_command_tick_and_changed_rng_are_refused(self):
        original, spec = episode()
        for action in ("command", "tick", "rng", "footer_rng"):
            records = copy.deepcopy(original)
            if action == "command":
                del records[2]
            elif action == "tick":
                records.remove(next(r for r in records if r.get("phase") == "tick"))
            elif action == "rng":
                records[2]["rng_draws_before"] += 1
            else:
                records[-1]["rng_draws"] = True
            with self.subTest(action=action), self.assertRaises(CHECK.TRACES.TraceError):
                self.validated(records, spec)

    def test_physical_detour_and_milestones_cannot_be_replaced_by_flags(self):
        original, spec = episode()
        for field in ("reached_firing_place", "resumed", "pulses"):
            records = copy.deepcopy(original)
            records[-1]["milestones"][field] = -1
            with self.subTest(field=field), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate(records, spec)

    def test_representative_controls_are_bounded(self):
        for change in ("control", "retirement"):
            records, spec = episode(change)
            self.assertEqual(4, CHECK.controls(records, spec))
            with mock.patch.object(CHECK, "observation"), self.assertRaisesRegex(
                    CHECK.TRACES.TraceError, "semantic fault control was accepted"):
                CHECK.controls(records, spec)

    def test_intervention_requires_the_original_incomplete_physical_leg(self):
        records, spec = episode("retirement")
        record = next(r for r in records if r.get("phase") == "tick" and r["state"]["time_ms"] == 800)
        robot = record["state"]["robots"][0]
        robot.update(x_bits=CHECK.float_bits(180), y_bits=CHECK.float_bits(240))
        robot["queue"] = [order for order in robot["queue"] if order[0] != 1]
        value = record["war_detour"]
        value.update(moving=False, move=[-1, -1])
        value["full_orders"][0]["orders"] = [o for o in value["full_orders"][0]["orders"] if o[0] != 1]
        record["detail"]["move"] = [-1, -1]
        CHECK.observation(record)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "800 ms intervention preconditions"):
            CHECK.validate(records, spec)

    def test_strict_profile_requires_complete_genuine_flags_and_clean_identity(self):
        records, _ = episode()
        expected = CHECK.profile("a" * 40, "Debug", "MinGW", "EXE", False, False)
        CHECK.validate_profile(records[0], expected, "a" * 40)
        CHECK.validate_profile(dict(records[0], revision="b" * 40), expected, None)
        for change in ({"dirty": False}, {"dirty": 1}, {"trace": 0}, {"cheats": 0},
                       {"build_exe": 1}, {"revision": "unknown"}, {"revision": "b" * 40}):
            with self.subTest(change=change), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate_profile(dict(records[0], **change), expected, "a" * 40)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.profile(revision="a" * 40)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.profile("a" * 40, "Debug", "MinGW", "EXE", False)

    def test_missing_or_unknown_finite_files_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "32"):
                CHECK.check(root)
            for name, _ in CHECK.inventory():
                (root / name).touch()
            (root / "war-detour-unrelated.jsonl").touch()
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "32"):
                CHECK.check(root)

    def test_reference_profile_is_independent_clean_and_coherent(self):
        records, spec = episode()
        first, replay = ("war-detour-control-pm-10-first.jsonl", spec), (
            "war-detour-control-pm-10-replay.jsonl", dict(spec, role="replay"))
        with tempfile.TemporaryDirectory() as temporary:
            candidate, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            candidate.mkdir()
            reference.mkdir()
            for name, identity in (first, replay):
                data = copy.deepcopy(records)
                data[0]["role"] = identity["role"]
                (candidate / name).write_text("\n".join(map(json.dumps, data)), encoding="utf-8")
                data[0]["revision"] = "b" * 40
                (reference / name).write_text("\n".join(map(json.dumps, data)), encoding="utf-8")
            arguments = [str(candidate), "--revision", "a" * 40, "--configuration", "Debug", "--compiler", "MinGW",
                         "--target", "EXE", "--cheats", "false", "--trace", "false",
                         "--reference-directory", str(reference)]
            with mock.patch.object(CHECK, "inventory", side_effect=lambda: iter((first, replay))), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(0, CHECK.main(arguments))
                self.assertEqual(0, CHECK.main([*arguments, "--reference-revision", "b" * 40]))
                self.assertEqual(1, CHECK.main([*arguments, "--reference-revision", "c" * 40]))
                self.assertEqual(1, CHECK.main([str(candidate), "--reference-revision", "b" * 40]))
                path = reference / replay[0]
                original = path.read_text(encoding="utf-8")
                for change in ({"revision": "c" * 40}, {"revision": "unknown"}, {"dirty": 1},
                               {"configuration": "Release"}, {"trace": True}, {"compiler_version": "different"}):
                    data = [json.loads(line) for line in original.splitlines()]
                    data[0].update(change)
                    path.write_text("\n".join(map(json.dumps, data)), encoding="utf-8")
                    with self.subTest(change=change):
                        self.assertEqual(1, CHECK.main(arguments))


if __name__ == "__main__":
    unittest.main()
