"""Synthetic recording contracts, not reference gameplay trajectories."""
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import check_player_target_traces as CHECK


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def contract_episode(family="repair", target="patient", ending="complete", step=16, route="keyboard", rng="pm"):
    """Use simple synthetic timings inside the existing source invariants."""
    repair = family == "repair"
    spec = dict(family=family, target=target, ending=ending, step=step, route=route, rng=rng)
    horizon = 2000 if repair else 30000
    intervention = ((300 if repair else 1000) + step - 1) // step * step
    request, dying, retired = 2000, 2000 + step, 2000 + 2 * step
    dispatched = interrupted = False
    flags = 0
    header = dict(type="header", schema=1,
                  fixture="repair-target-lifecycle-v1" if repair else "bomb-target-lifecycle-v1",
                  recording="player-target-lifecycle-v1", revision="a" * 40, dirty=0,
                  configuration="Debug", compiler="MinGW", compiler_version="fixture", precision_control=0,
                  assert_off=False, build_exe=True, cheats=False, delayed_damage="Corrected", group_counts="Corrected",
                  place_containment="Corrected", zero_x_trace="Corrected", target_kind=target, ending=ending,
                  route=route, rng="ParkMiller" if rng == "pm" else "LegacyCRT", seed=80, step_ms=step,
                  max_frames=1000 if repair else 4000, horizon_ms=horizon, initially_selected=True,
                  audio="unavailable", render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720])
    records = [header]

    def snapshot(phase, frame):
        time = frame * step
        heals = min(5, (intervention if interrupted and ending == "remove" else time) // 100) if repair else 0
        detonated = not repair and ending != "stop"
        actor_present = not detonated or time < retired
        actor_live = actor_present and (not detonated or time < dying)
        must_die = detonated and time >= request
        peer_present = not (interrupted and ending == "remove")
        peer_health = (30 + 4 * heals) / 10 if repair else (4.6 if target == "friendly" else 4.0) if (
            detonated and time >= retired) else 5.0
        damage = int(not repair and peer_present and peer_health != 5)
        state_order = -1 if not dispatched else 5 if repair else 6
        state_target = 0 if not dispatched or target == "ground" else 2 if repair else 3
        if interrupted and ending == "remove":
            state_target = 0
            if repair and time >= intervention + 100 + step:
                state_order = 0
        if (interrupted and ending == "stop") or (repair and heals == 5):
            state_order, state_target = 0, 0
        if not repair and must_die:
            state_order, state_target = 0, 0
        destination = [-1, -1] if not dispatched or repair else [48, 48]
        stop_destination = destination if interrupted and ending == "stop" and not repair else [-1, -1]
        fire = repair and dispatched and heals < 5 and not (interrupted and ending == "remove")
        full_orders = [[5, 0, 0, 0, 0, 2, 0]] if fire else []
        actor_x, actor_y = (100, 200) if repair or time == 0 else (500, 500) if time >= request else (200, 200)
        if ending == "stop" and not repair:
            actor_x, actor_y = (100, 200) if time == 0 else (200, 200)
        robots = []
        for entity in range(1, 5):
            alive = actor_live if entity == 1 else peer_present if entity == (2 if repair else 3) else True
            robot = dict(id=entity, alive=alive)
            if alive:
                health = peer_health if entity == (2 if repair else 3) else 100
                robot.update(x_bits=bits(actor_x if entity == 1 else 500),
                             y_bits=bits(actor_y if entity == 1 else 500), health_bits=bits(health),
                             side=1 if entity < 3 else 2, state=2, in_place=False, group=0,
                             order=state_order if entity in (1, 2) else -1,
                             target=state_target if entity in (1, 2) else 0,
                             target_is_null=state_target == 0 if entity in (1, 2) else True,
                             destination=destination if entity in (1, 2) else [-1, -1],
                             queue=[[o[0], o[1], o[2], o[3], o[6]] for o in full_orders] if entity == 1 else [])
                if entity == 2 and not actor_live:
                    robot.update(order=0, target=0, target_is_null=True)
            robots.append(robot)
        actor = dict(present=actor_present, live=actor_live)
        if actor_present:
            actor.update(state=2 if actor_live else 5, must_die=must_die)
        if actor_live:
            actor.update(x_bits=bits(actor_x), y_bits=bits(actor_y), health_bits=bits(100),
                         environment_target=2 if fire else 0, fire_order=fire)
        peer = dict(id=2 if repair else 3, present=peer_present, live=peer_present)
        if peer_present:
            peer.update(state=2, health_bits=bits(peer_health), max_health_bits=bits(5), need_repair=peer_health < 5)
        selected = [dict(id=1, live=actor_live), dict(id=2, live=robots[1]["alive"])]
        if actor_live:
            selected[0]["orders"] = full_orders
        if robots[1]["alive"]:
            selected[1]["orders"] = []
        value = dict(preorder_flags=flags, actor_core=int(actor_present), actor=actor, peer=peer,
                     selected_orders=selected, heals=heals, damage_events=damage,
                     interrupted_at=intervention if interrupted else 0,
                     requested_at=request if detonated and time >= request else 0,
                     dying_at=dying if detonated and time >= dying else 0,
                     retired_at=retired if detonated and time >= retired else 0,
                     destination=destination, stop_destination=stop_destination)
        records.append(dict(type="snapshot", phase=phase,
                            state=dict(frame=frame, time_ms=time, rng_draws=123, robots=robots, player_groups=[]),
                            target_lifecycle=value))

    routed = "button" if route == "button" else "key"
    routed_key = lambda letter: ord(letter) if route == "keyboard" else 0
    if repair:
        inputs = [("repair-preorder", routed, routed_key("R"), 0, [-1, -1], True),
                  ("cancel-preorder", "key", 88, 0, [-1, -1], True),
                  ("late-patient-click", "click", 0, 2, [-1, -1], False),
                  ("repair-preorder", routed, routed_key("R"), 0, [-1, -1], True),
                  ("invalid-ground", "click", 0, 0, [500, 300], False),
                  ("enemy-click", "click", 0, 3, [-1, -1], False),
                  ("patient-click", "click", 0, 2, [-1, -1], True)]
    else:
        inputs = [("bomb-preorder", routed, routed_key("E"), 0, [-1, -1], True),
                  ("cancel-preorder", "key", 88, 0, [-1, -1], True),
                  ("late-ground-click", "click", 0, 0, [500, 500], False),
                  ("bomb-preorder", routed, routed_key("E"), 0, [-1, -1], True),
                  ("bomb-target-click", "click", 0, 0 if target == "ground" else 3,
                   [500, 500] if target == "ground" else [-1, -1], True)]

    def command(item, frame):
        nonlocal flags, dispatched, interrupted
        name, delivery, key, selected_target, point, accepted = item
        if name in ("repair-preorder", "bomb-preorder"):
            flags = (CHECK.REPAIR if repair else CHECK.BOMB) | CHECK.ORDERING
        elif name in ("cancel-preorder", "patient-click", "bomb-target-click"):
            flags = 0
        if name in ("patient-click", "bomb-target-click"):
            dispatched = True
        if name in ("remove-patient", "remove-target", "replace-stop"):
            interrupted = True
        records.append(dict(type="command", name=name, frame=frame, time_ms=frame * step, input=delivery,
                            key=key, target=selected_target, point=point, accepted=accepted,
                            rng_draws_before=123, rng_draws_after=123))
        snapshot("command", frame)

    snapshot("initial", 0)
    for item in inputs:
        command(item, 0)
    for frame in range(1, horizon // step + 1):
        snapshot("tick", frame)
        if ending != "complete" and frame * step == intervention:
            item = (("remove-patient" if repair else "remove-target", "delete", 0, 2 if repair else 3,
                     [-1, -1], True) if ending == "remove" else
                    ("replace-stop", routed, routed_key("S"), 0, [-1, -1], True))
            command(item, frame)
    records.append(dict(type="complete", frame=horizon // step, time_ms=horizon,
                        result=copy.deepcopy(records[-1]["target_lifecycle"])))
    return records, spec


class TargetTraceContracts(unittest.TestCase):
    def validated(self, records, spec):
        parsed = CHECK.TRACES.read_records(io.StringIO("\n".join(map(json.dumps, records))))
        CHECK.validate(parsed, spec)
        return parsed

    def test_finite_inventory_preserves_only_accepted_variants(self):
        items = list(CHECK.inventory())
        self.assertEqual(len(items), 80)
        self.assertEqual(len({name for name, _ in items}), 80)
        self.assertEqual(sum(spec["family"] == "repair" for _, spec in items), 24)
        self.assertEqual(sum(spec["ending"] == "stop" for _, spec in items), 24)
        self.assertFalse(any(spec["target"] == "ground" and spec["ending"] == "remove" for _, spec in items))
        self.assertFalse(any(spec["target"] == "friendly" and spec["ending"] == "stop" for _, spec in items))

    def test_source_ending_and_target_contracts_accept_both_routes(self):
        for family, target, ending in CHECK.VARIANTS:
            for route in ("keyboard", "button"):
                with self.subTest(family=family, target=target, ending=ending, route=route):
                    records, spec = contract_episode(family, target, ending, route=route)
                    CHECK.validate(records, spec)

    def test_both_rng_and_step_headers_are_retained(self):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                with self.subTest(rng=rng, step=step):
                    self.validated(*contract_episode(step=step, rng=rng))

    def test_correlated_missing_custom_fields_need_semantic_validation(self):
        original, spec = contract_episode()
        for field in original[1]["target_lifecycle"]:
            records = copy.deepcopy(original)
            del records[1]["target_lifecycle"][field]
            parsed = CHECK.TRACES.read_records(io.StringIO("\n".join(map(json.dumps, records))))
            self.assertIsNone(CHECK.TRACES.compare_records(parsed, copy.deepcopy(parsed)))
            with self.subTest(field=field), self.assertRaisesRegex(CHECK.TRACES.TraceError, "observations missing"):
                CHECK.validate(parsed, spec)

    def test_live_and_dead_object_fields_have_distinct_lifetimes(self):
        records, spec = contract_episode("bomb", "enemy", "complete")
        dead = next(r for r in records if r["type"] == "snapshot" and
                    r["target_lifecycle"]["actor"]["present"] and not r["target_lifecycle"]["actor"]["live"])
        self.assertEqual(dead["target_lifecycle"]["actor_core"], 1)
        for field, value in (("health_bits", bits(100)), ("environment_target", 0), ("fire_order", False)):
            changed = copy.deepcopy(dead)
            changed["target_lifecycle"]["actor"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(CHECK.TRACES.TraceError, "dead actor"):
                CHECK.observation(changed, spec)
        changed = copy.deepcopy(dead)
        changed["target_lifecycle"]["selected_orders"][0]["orders"] = []
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "dead actor"):
            CHECK.observation(changed, spec)

    def test_full_orders_require_aim_mode_and_genuine_scalar_types(self):
        records, spec = contract_episode()
        record = next(r for r in records if r["type"] == "snapshot" and
                      r["target_lifecycle"]["actor"].get("fire_order"))
        for index in range(7):
            for invalid in (True, None, "0", 0.0):
                changed = copy.deepcopy(record)
                changed["target_lifecycle"]["selected_orders"][0]["orders"][0][index] = invalid
                with self.subTest(index=index, value=invalid), self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.observation(changed, spec)
        changed = copy.deepcopy(record)
        changed["target_lifecycle"]["selected_orders"][0]["orders"][0].pop(4)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "seven-field"):
            CHECK.observation(changed, spec)

    def test_player_order_domains_reject_correlated_out_of_range_values(self):
        original, spec = contract_episode()
        positive = copy.deepcopy(original[1]["state"])
        positive["robots"][0]["order"] = 9
        self.assertEqual(positive["robots"][2]["order"], -1)
        positive["player_groups"] = [dict(slot=0, stored_count=1, members=1, order=9, target=0)]
        CHECK.session_state(positive)
        for field in ("actor", "group"):
            records = copy.deepcopy(original)
            state = records[1]["state"]
            if field == "actor":
                state["robots"][0]["order"] = 999
            else:
                state["player_groups"] = [dict(slot=0, stored_count=1, members=1, order=999, target=0)]
            parsed = CHECK.TRACES.read_records(io.StringIO("\n".join(map(json.dumps, records))))
            self.assertIsNone(CHECK.TRACES.compare_records(parsed, copy.deepcopy(parsed)))
            with self.subTest(field=field), self.assertRaisesRegex(CHECK.TRACES.TraceError, "order|group"):
                CHECK.validate(parsed, spec)

    def test_missing_tick_command_and_false_rng_boundary_are_rejected(self):
        original, spec = contract_episode()
        for change in ("tick", "command", "rng", "preorder"):
            records = copy.deepcopy(original)
            if change == "tick":
                records.remove(next(r for r in records if r["type"] == "snapshot" and r.get("phase") == "tick"))
            elif change == "command":
                del records[2]
            elif change == "rng":
                records[2]["rng_draws_before"] += 1
            else:
                records[3]["target_lifecycle"]["preorder_flags"] = 0
            with self.subTest(change=change), self.assertRaises(CHECK.TRACES.TraceError):
                self.validated(records, spec)

    def test_completion_cannot_hide_correlated_bad_health_counter(self):
        records, spec = contract_episode()
        for record in records:
            if record["type"] == "snapshot":
                record["target_lifecycle"]["heals"] = 0
        records[-1]["result"]["heals"] = 0
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "counter differs"):
            self.validated(records, spec)

    def test_dip_is_not_core_retirement(self):
        records, spec = contract_episode("bomb", "enemy", "complete")
        for record in records:
            if record["type"] == "snapshot" and record["target_lifecycle"]["dying_at"]:
                record["target_lifecycle"]["retired_at"] = record["target_lifecycle"]["dying_at"]
        records[-1]["result"] = copy.deepcopy(records[-2]["target_lifecycle"])
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "milestones"):
            self.validated(records, spec)

    def test_truncated_footer_and_unknown_record_types_are_rejected(self):
        original, spec = contract_episode()
        for records in (original[:-1], original + [dict(original[-1])],
                        original[:-1] + [dict(original[-1], type="milestones")]):
            with self.subTest(tail=records[-1]["type"]), self.assertRaises(CHECK.TRACES.TraceError):
                self.validated(records, spec)

    def test_strict_profile_and_independent_reference_revision(self):
        records, _ = contract_episode()
        header = records[0]
        expected = CHECK.profile("a" * 40, "Debug", "MinGW", "EXE", False)
        CHECK.validate_profile(header, expected, "a" * 40)
        historical = dict(header, revision="b" * 40)
        CHECK.validate_profile(historical, expected, None)
        CHECK.validate_profile(historical, expected, "b" * 40)
        for changes in ({"revision": "b" * 40}, {"dirty": False}, {"dirty": 1}, {"build_exe": 1}, {"cheats": 0}):
            with self.subTest(changes=changes), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate_profile(dict(header, **changes), expected, "a" * 40)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.validate_profile(historical, expected, "c" * 40)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.profile(revision="a" * 40)

    def test_empty_and_wrong_finite_inventory_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "80 files"):
                CHECK.check(root)
            for name, _ in CHECK.inventory():
                (root / name).touch()
            with mock.patch.object(CHECK, "load", return_value=[]), mock.patch.object(
                    CHECK, "controls", return_value=4) as controls, contextlib.redirect_stdout(io.StringIO()):
                CHECK.check(root)
                self.assertEqual(controls.call_count, 2)
            (root / "player-target-unrelated.jsonl").touch()
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "80 files"):
                CHECK.check(root)

    def test_representative_controls_are_bounded_and_need_the_semantic_guard(self):
        for family, target in (("repair", "patient"), ("bomb", "enemy")):
            records, spec = contract_episode(family, target, step=10)
            with self.subTest(family=family):
                self.assertEqual(CHECK.controls(records, spec), 4)
                with mock.patch.object(CHECK, "observation"), self.assertRaisesRegex(
                        CHECK.TRACES.TraceError, "omission of core lifetime"):
                    CHECK.controls(records, spec)

    def test_reference_cli_keeps_separate_revision_and_full_file_checks(self):
        records, spec = contract_episode()
        name = next(name for name, identity in CHECK.inventory() if identity == spec)
        with tempfile.TemporaryDirectory() as temporary:
            candidate, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            candidate.mkdir()
            reference.mkdir()
            (candidate / name).write_text("\n".join(map(json.dumps, records)), encoding="utf-8")
            old = copy.deepcopy(records)
            old[0]["revision"] = "b" * 40
            (reference / name).write_text("\n".join(map(json.dumps, old)), encoding="utf-8")
            arguments = [str(candidate), "--revision", "a" * 40, "--configuration", "Debug", "--compiler", "MinGW",
                         "--target", "EXE", "--cheats", "false", "--reference-directory", str(reference)]
            # The production inventory remains 80; narrow only this full CLI plumbing test.
            with mock.patch.object(CHECK, "inventory", side_effect=lambda: iter([(name, spec)])), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(0, CHECK.main([*arguments, "--reference-revision", "b" * 40]))
                self.assertEqual(1, CHECK.main([*arguments, "--reference-revision", "c" * 40]))
                self.assertEqual(1, CHECK.main([str(candidate), "--reference-revision", "b" * 40]))
                self.assertEqual(1, CHECK.main([str(candidate), "--revision", "a" * 40]))
                for changes in ({"dirty": 1}, {"revision": "short"}, {"revision": "unknown"},
                                {"configuration": "Release"}, {"build_exe": False}, {"cheats": True}):
                    invalid = copy.deepcopy(old)
                    invalid[0].update(changes)
                    (reference / name).write_text("\n".join(map(json.dumps, invalid)), encoding="utf-8")
                    with self.subTest(reference=changes), mock.patch.object(
                            CHECK.TRACES, "compare_records", wraps=CHECK.TRACES.compare_records) as compare:
                        self.assertEqual(1, CHECK.main(arguments))
                        compare.assert_not_called()


if __name__ == "__main__":
    unittest.main()
