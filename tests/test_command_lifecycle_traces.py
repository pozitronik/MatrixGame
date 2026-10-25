"""Independent malformed-data checks for the finite command-lifecycle consumer."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location("command_lifecycle_traces",
                                          Path(__file__).with_name("check_command_lifecycle_traces.py"))
c2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c2)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def actor(entity):
    return dict(id=entity, present=True, alive=True, state=2, chassis=2, x_bits=bits(120), y_bits=bits(500),
                group=0, place=-1, map=[10, 48], in_place=True, has_base=False,
                maximum_speed_bits=bits(.6), move_destination=None, queue=[])


def snapshot(frame, step, phase, *, dead=False, retired=False, selection=None):
    actors = [actor(entity) for entity in range(1, 5)]
    if retired:
        actors[0] = dict(id=1, present=False)
    elif dead:
        actors[0] = dict(id=1, present=True, alive=False, state=5)
    robots = []
    for entity in range(1, 5):
        value = dict(id=entity, alive=not (entity == 1 and dead))
        if value["alive"]:
            value.update(x_bits=bits(120), y_bits=bits(500), side=1 if entity <= 2 else 2,
                         state=2, health_bits=bits(100), in_place=True, group=0, order=1, target=0,
                         target_is_null=True, destination=[10, 48], queue=[])
        robots.append(value)
    group = dict(slot=0, stored_count=1, members=1, order=1, target=0)
    return dict(type="snapshot", state=dict(frame=frame, time_ms=frame * step, rng_draws=1,
                robots=robots, player_groups=[group]), c2=dict(phase=phase, actors=actors,
                selection=selection if selection is not None else ([2] if dead else [1, 2]),
                groups=[dict(group, destination=[10, 48])], carrier_present=False, carried_actor=0,
                watched_core=1 if dead and not retired else 0, enemy_kills=1 if dead else 0))


def dying_recording(step=10):
    profile = dict(family="dying", rng="ParkMiller", step=step, route=-1, flag=False)
    header = dict(type="header", schema=1, fixture="dying-command-lifecycle-v1",
                  c2_observation="command-lifecycle-v1", family="dying", rng="ParkMiller", seed=79,
                  step_ms=step, max_frames=6000, initially_selected=False, audio="unavailable",
                  navigation_layout="open-v1", move_route=-1, flag=False, extra_actor=0, trace=False,
                  configuration="Release", compiler="MinGW", precision_control=131072,
                  revision="candidate", dirty=0, build_exe=True, cheats=False)
    commands, end = c2.schedule(profile)
    hit = commands[1][1]
    retired_at = commands[-1][1]
    records = [header, snapshot(0, step, "session-ready", selection=[]),
               snapshot(0, step, "selection", selection=[1, 2])]
    dead = False
    for frame in range(end // step + 1):
        time = frame * step
        if frame:
            records.append(snapshot(frame, step, "tick", dead=dead, retired=time >= retired_at))
        for name, deadline in commands:
            if deadline != time:
                continue
            if name == "lethal-damage":
                dead = True
            if time == retired_at:
                records.append(snapshot(frame, step, "selection", dead=True, retired=True, selection=[2]))
            records.append(dict(type="command", frame=frame, time_ms=time, name=name,
                                rng_draws_before=1, rng_draws_after=1))
            records.append(snapshot(frame, step, name, dead=dead, retired=time >= retired_at))
    records.append(snapshot(end // step, step, "final", dead=True, retired=True))
    records.append(dict(type="complete", frame=end // step, time_ms=end, c2=dict(intermediate_ms=-1, ready_ms=-1)))
    return profile, records


def movement_boundaries(family, step):
    profile = dict(family=family, rng="ParkMiller", step=step, route=0, flag=family == "carried")
    commands, _ = c2.schedule(profile)

    def boundary(time, phase, order, destination, position, map_position, in_place):
        record = snapshot(time // step, step, phase)
        record["c2"]["actors"].append(actor(5))
        for entity in ((1, 5) if family == "mixed" else (5,)):
            value = record["c2"]["actors"][entity - 1]
            value.update(group=1, x_bits=bits(position[0]), y_bits=bits(position[1]),
                         map=map_position.copy(), in_place=in_place)
        record["c2"]["groups"] = [dict(slot=0, stored_count=1, members=1, order=1, target=0,
                                        destination=[8, 48]),
                                  dict(slot=1, stored_count=1, members=1, order=order, target=0,
                                       destination=destination.copy())]
        return record

    if family == "carried":
        last = commands[-1][1]
        position, cell, arrived = ((480, 460), [46, 44], False) if step == 10 else ((40, 40), [2, 2], True)
        records = [boundary(0, "unrelated-move", 1, [48, 8], (100, 100), [0, 0], False),
                   boundary(0, commands[1][0], 1, [48, 8], (100, 100), [0, 0], False),
                   boundary(0, "stop-key", 0, [0, 0], (100, 100), [0, 0], False),
                   boundary(30000, "tick", 0, [0, 0], position, cell, arrived),
                   boundary(last, "tick", 0, [0, 0], position, cell, arrived),
                   boundary(last, commands[-1][0], 1, [10, 48], (120, 500), [10, 48], True)]
    else:
        stop, last = commands[2][1], commands[-1][1]
        records = [boundary(0, "unrelated-move", 1, [48, 28], (100, 300), [8, 28], False),
                   boundary(0, commands[1][0], 1, [48, 28], (100, 300), [8, 28], False),
                   boundary(stop, "stop-key", 0, [12, 24], (120, 300), [10, 28], False),
                   boundary(stop + 6000, "tick", 0, [12, 24], (150, 300), [13, 28], False),
                   boundary(last, "tick", 0, [12, 24], (150, 300), [13, 28], False),
                   boundary(last, commands[-1][0], 1, [48, 8], (500, 100), [48, 8], True)]
    return profile, commands, records


class CommandLifecycleTraces(unittest.TestCase):
    def test_mixed_progress_stop_and_stationary_contract(self):
        for step in (10, 16):
            profile, commands, records = movement_boundaries("mixed", step)
            c2.movement_contract(records, profile, commands, -1)
            faults = [lambda r: r[2]["c2"]["actors"][4].update(x_bits=bits(100)),
                      lambda r: r[3]["c2"]["actors"][4].update(x_bits=bits(500)),
                      lambda r: r[4]["c2"]["actors"][4].update(y_bits=bits(301)),
                      lambda r: r[3]["c2"]["groups"][1].update(order=1)]
            for fault in faults:
                changed = copy.deepcopy(records)
                fault(changed)
                with self.assertRaises(c2.trace.TraceError):
                    c2.movement_contract(changed, profile, commands, -1)

    def test_carried_cancel_preserves_distinct_step_outcomes(self):
        for step in (10, 16):
            profile, commands, records = movement_boundaries("carried", step)
            c2.movement_contract(records, profile, commands, 200)
            faults = [lambda r: r[3]["c2"]["actors"][4].update(move_destination=[48, 8]),
                      lambda r: r[3]["c2"]["actors"][4].update(map=[48, 8]),
                      lambda r: r[4]["c2"]["actors"][4].update(y_bits=bits(999)),
                      lambda r: r[3]["c2"]["groups"][1].update(destination=[48, 8]),
                      lambda r: r[1]["c2"]["groups"][0].update(destination=[10, 48])]
            for fault in faults:
                changed = copy.deepcopy(records)
                fault(changed)
                with self.assertRaises(c2.trace.TraceError):
                    c2.movement_contract(changed, profile, commands, 200)

    def test_finite_inventory_preserves_existing_episodes(self):
        inventory = list(c2.inventory())
        self.assertEqual(len(inventory), 76)
        self.assertEqual(len({name for name, _ in inventory}), 76)
        self.assertEqual({family: sum(p["family"] == family for _, p in inventory)
                          for family in ("mixed", "spawning", "carried", "dying")},
                         dict(mixed=24, spawning=24, carried=24, dying=4))

    def test_existing_relative_deadlines_are_not_resampled(self):
        for step, mixed, canceled, dying in ((10, 24000, 47000, 22500), (16, 24016, 47008, 22512)):
            profile = dict(family="mixed", step=step, route=0, flag=False)
            self.assertEqual(c2.schedule(profile)[1], mixed)
            self.assertEqual(c2.schedule(dict(profile, family="carried", flag=True))[1], canceled)
            self.assertEqual(c2.schedule(dict(profile, family="dying", route=-1))[1], dying)

    def test_positive_full_schema_and_dying_contract(self):
        for step in (10, 16):
            profile, records = dying_recording(step)
            parsed = c2.trace.read_records(io.StringIO("\n".join(json.dumps(r) for r in records)), "synthetic")
            self.assertEqual(c2.validate(parsed, profile, revision="candidate", configuration="Release",
                                         compiler="MinGW", target="EXE", cheats=False, traced=False), [])

    def test_scalar_order_parameters_reject_bool_float_and_string(self):
        for width in (5, 7):
            good = [1, 0, 0, 0, 0] if width == 5 else [1, 0, 0, 0, 0, 0, 0]
            c2.queue([good], width, 5)
            for index in range(width):
                for bad in (True, 1.0, "0"):
                    with self.subTest(width=width, index=index, bad=bad):
                        row = good.copy()
                        row[index] = bad
                        with self.assertRaises(c2.trace.TraceError):
                            c2.queue([row], width, 5)

    def test_queue_shapes_bits_signed_modes_and_identities(self):
        good = [1, 0, 0, 0xffffffff, 0, -(1 << 31), -1]
        c2.queue([good], 7, 5)
        for row in (good[:-1], good + [0], [1, 0, -1, 0, 0, 0, 0],
                    [1, 0, 0, 0, 1 << 32, 0, 0], [1, 0, 0, 0, 0, 1 << 31, 0],
                    [1, 0, 0, 0, 0, 0, 6]):
            with self.assertRaises(c2.trace.TraceError):
                c2.queue([row], 7, 5)
        for container in (None, {}, "queue", [None]):
            with self.assertRaises(c2.trace.TraceError):
                c2.queue(container, 7, 5)

    def test_group_orders_and_physical_orders_use_separate_domains(self):
        record = snapshot(0, 10, "tick")
        record["state"]["robots"][0]["order"] = 9
        record["state"]["player_groups"][0]["order"] = 9
        record["c2"]["groups"][0]["order"] = 9
        c2.observation(record, 10)
        for width in (5, 7):
            row = [0] * width
            for index in (0, 1):
                for invalid in (-1, 9):
                    changed = row.copy()
                    changed[index] = invalid
                    with self.assertRaises(c2.trace.TraceError):
                        c2.queue([changed], width, 5 if width == 7 else 4)
            with self.assertRaises(c2.trace.TraceError):
                c2.queue([row] * 6, width, 5)
        for path in (("state", "robots", 0), ("state", "player_groups", 0), ("c2", "groups", 0)):
            changed = copy.deepcopy(record)
            value = changed
            for key in path:
                value = value[key]
            value["order"] = 10
            with self.assertRaises(c2.trace.TraceError):
                c2.observation(changed, 10)

    def test_required_custom_fields_are_checked_independently(self):
        value = actor(5)
        c2.actor(value)
        for key in value:
            with self.subTest(key=key):
                bad = value.copy()
                bad.pop(key)
                with self.assertRaises(c2.trace.TraceError):
                    c2.actor(bad)

    def test_retired_actor_cannot_retain_a_queue_or_coordinates(self):
        c2.actor(dict(id=1, present=False))
        for key, value in (("queue", []), ("x_bits", 0), ("alive", False)):
            with self.assertRaises(c2.trace.TraceError):
                c2.actor(dict(id=1, present=False, **{key: value}))

    def test_dying_actor_retains_only_initialized_lifetime_state(self):
        value = dict(id=1, present=True, alive=False, state=5)
        c2.actor(value)
        for key, invalid in (("chassis", 2), ("chassis", 1065017672), ("queue", []), ("has_base", False)):
            with self.assertRaises(c2.trace.TraceError):
                c2.actor(dict(value, **{key: invalid}))
        with self.assertRaises(c2.trace.TraceError):
            c2.actor(dict(value, alive=True))

    def test_full_recording_rejects_correlated_missing_observation(self):
        profile, records = dying_recording()
        for record in records:
            if record["type"] == "snapshot":
                record["c2"].pop("carrier_present")
        self.assertIsNone(c2.trace.compare_records(records, copy.deepcopy(records)))
        with self.assertRaisesRegex(c2.trace.TraceError, "carrier"):
            c2.validate(records, profile)

    def test_full_recording_rejects_missing_ticks_commands_and_retirement(self):
        profile, records = dying_recording()
        faults = [lambda r: r.pop(next(i for i, x in enumerate(r) if x.get("c2", {}).get("phase") == "tick")),
                  lambda r: r.pop(next(i for i, x in enumerate(r) if x.get("name") == "lethal-damage")),
                  lambda r: r[-2]["c2"].update(watched_core=1),
                  lambda r: r[-2]["c2"].update(enemy_kills=0),
                  lambda r: r[-1]["c2"].update(ready_ms=123)]
        for fault in faults:
            changed = copy.deepcopy(records)
            fault(changed)
            with self.assertRaises(c2.trace.TraceError):
                c2.validate(changed, profile)

    def test_header_profile_and_boolean_types_are_not_coerced(self):
        profile, records = dying_recording()
        for key, value in (("seed", 80), ("flag", 0), ("trace", 0), ("move_route", -1.0),
                           ("extra_actor", 5), ("dirty", True), ("revision", "other")):
            changed = copy.deepcopy(records[0])
            changed[key] = value
            with self.assertRaises(c2.trace.TraceError):
                c2.validate([changed], profile, revision="candidate")

    def test_truncated_or_duplicate_tail_is_not_a_completed_episode(self):
        _, records = dying_recording()
        for changed in (records[:-1], records + [records[-1]]):
            with self.assertRaises(c2.trace.TraceError):
                c2.trace.read_records(io.StringIO("\n".join(json.dumps(r) for r in changed)), "synthetic")

    def test_directory_entry_rejects_equal_bad_reference_before_comparison(self):
        profile, records = dying_recording()
        for record in records:
            if record["type"] == "snapshot":
                record["c2"]["actors"][1].pop("move_destination")
        name = "player-command-dying-pm-10.jsonl"
        with tempfile.TemporaryDirectory() as temp:
            candidate = Path(temp) / "candidate"
            reference = Path(temp) / "reference"
            candidate.mkdir()
            reference.mkdir()
            data = "\n".join(json.dumps(r) for r in records)
            for directory in (candidate, reference):
                (directory / name).write_text(data, encoding="utf-8")
            with mock.patch.object(c2, "inventory", return_value=iter([(name, profile)])), \
                    mock.patch.object(c2.trace, "compare_records", side_effect=AssertionError("must validate first")):
                with self.assertRaisesRegex(c2.trace.TraceError, "Move destination"):
                    c2.check_directory(candidate, reference=reference)


if __name__ == "__main__":
    unittest.main()
