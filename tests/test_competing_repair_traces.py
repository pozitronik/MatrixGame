"""Synthetic schema and semantic failures for the finite competing-repair reader."""
import argparse
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "competing_repair", Path(__file__).with_name("check_competing_repair_traces.py"))
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
REVISION = "1" * 40


def arguments(**changes):
    values = dict(configuration="Debug", compiler="MinGW", target="EXE", cheats=False, trace=False,
                  revision=REVISION, reference_revision=None)
    values.update(changes)
    return argparse.Namespace(**values)


def actor(index, initial=False):
    return dict(id=index, present=True, alive=True, state=2, x_bits=CHECK.float_bits(100),
                y_bits=CHECK.float_bits(100), health_bits=CHECK.float_bits(3.2 if index >= 5 else 100),
                group=-1 if initial or index >= 3 else 0, place=-1, in_place=False,
                order=-1 if initial or index >= 3 else 1,
                destination=[-1, -1] if initial or index >= 3 else [48, 18], queue=[])


def synthetic_trace(family="retirement", step=16):
    """Independent synthetic schedules retain the real whole-episode horizon."""
    replacement, near = family != "retirement", family == "replace-near"
    header = dict(type="header", schema=1, fixture="competing-moving-repair-v2", revision=REVISION,
                  dirty=0, trace=False, rng="ParkMiller", seed=84, step_ms=step, max_frames=3000,
                  precision_control=0, initially_selected=True, build_exe=True, cheats=False,
                  delayed_damage="Corrected", group_counts="Corrected", place_containment="Corrected",
                  zero_x_trace="Corrected", assert_off=False, configuration="Debug", compiler="MinGW",
                  compiler_version="synthetic", navigation_layout="open-v1", audio="unavailable",
                  render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720],
                  replacement=replacement, competitor_in_range=near)
    yield header
    threshold = 3 if near else 2
    final = (25000 + step - 1) // step
    counts = [0, 0]
    changed = False

    def snapshot(phase, frame, tick_target=0):
        initial = phase == "initial"
        actors = [actor(index, initial) for index in range(1, 7)]
        for index in (0, 1):
            if changed and replacement:
                actors[index]["destination"] = [28, 48]
            if frame == final:
                actors[index]["in_place"] = True
        actors[0]["x_bits"] = CHECK.float_bits(150 if near else 110) if frame else CHECK.float_bits(100)
        if frame == final:
            actors[0]["x_bits"] = CHECK.float_bits(300 if replacement else 500)
            actors[0]["y_bits"] = CHECK.float_bits(500 if replacement else 200)
        health = [CHECK.float_bits(min(50, 32 + count * 4) / 10) for count in counts]
        for index in range(2):
            actors[index + 4]["health_bits"] = health[index]
        if changed and not replacement:
            actors[4] = dict(id=5, present=False)
        target = 0 if initial or frame == final or (changed and not replacement) else 6 if changed and near else 5
        robots = []
        for value in actors[:4]:
            robot = {k: copy.deepcopy(v) for k, v in value.items() if k not in {"present", "place"}}
            robot.update(side=1 if value["id"] <= 2 else 2, target=0, target_is_null=True)
            robots.append(robot)
        groups = [] if initial else [dict(slot=0, stored_count=2, members=2, order=1, target=0)]
        return dict(type="snapshot", phase=phase, state=dict(frame=frame, time_ms=frame * step,
                    rng_draws=10 if initial else 11, robots=robots, player_groups=groups), repairer=1,
                    target=target, tick_target=tick_target, changed=changed,
                    first_core=0 if changed and not replacement else 5,
                    pulse_counts=list(counts), cached_health=health,
                    logic_order=[1, 2, 3, 4, 6] if changed and not replacement else [1, 2, 3, 4, 6, 5], actors=actors)

    def command(name, frame):
        return dict(type="command", name=name, frame=frame, time_ms=frame * step,
                    rng_draws_before=10 if name == "move" else 11, rng_draws_after=11,
                    **({"patient": 5} if name == "retire-patient"
                       else {"world": [500, 200] if name == "move" else [300, 500]}))

    yield snapshot("initial", 0)
    yield command("move", 0)
    yield snapshot("after-move", 0, 5)
    for frame in range(1, final + 1):
        receiver = 0
        if frame <= threshold:
            receiver = 5
        elif not replacement and frame <= threshold + 5:
            receiver = 6
        elif replacement and near:
            receiver = 6 if frame <= threshold + 5 else 5 if frame <= 10 else 0
        elif replacement:
            receiver = 5 if frame <= 5 else 6 if frame <= 10 else 0
        if receiver:
            counts[receiver - 5] += 1
        if frame == threshold:
            yield snapshot("before-change", frame, receiver)
            yield command("replace-move" if replacement else "retire-patient", frame)
            changed = True
            yield snapshot("after-change", frame, receiver)
        yield snapshot("tick", frame, receiver)
    yield snapshot("final", final)
    yield dict(type="complete", frame=final, time_ms=final * step, rng_draws=11)


class CompetingRepairTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in synthetic_trace())

    def reject_observation(self, edit):
        record = copy.deepcopy(self.records[1])
        edit(record)
        with self.assertRaises((ValueError, KeyError, TypeError)):
            CHECK.observations(record)

    def test_complete_existing_horizons_and_families(self):
        for family in ("retirement", "replace-far", "replace-near"):
            for step in (10, 16):
                with self.subTest(family=family, step=step):
                    records = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in synthetic_trace(family, step))
                    CHECK.validate(records, (family, "pm", step), arguments(), REVISION)

    def test_actual_compiler_version_types(self):
        for compiler, good, bad in (("MinGW", "13.2.0", ("", 1938, True, None)),
                                    ("MSVC", 1938, ("1938", 0, True, 2**31))):
            header = dict(self.records[0], compiler=compiler, compiler_version=good)
            CHECK.identity(header, arguments(compiler=compiler), REVISION)
            for value in bad:
                with self.subTest(compiler=compiler, value=value), self.assertRaises(ValueError):
                    CHECK.identity(dict(header, compiler_version=value), arguments(compiler=compiler), REVISION)

    def test_full_order_scalar_domains(self):
        base = [5, 3, 0, 0xffffffff, 1, -(2**31), 6]
        CHECK.queue([base], 7, 6)
        for index in range(7):
            for value in (True, 1.5, "1"):
                item = list(base)
                item[index] = value
                with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                    CHECK.queue([item], 7, 6)
        for index, values in ((0, (-1, 9)), (1, (-1, 9)), (2, (-1, 2**32)), (3, (-1, 2**32)),
                              (4, (-1, 2**32)), (5, (-(2**31)-1, 2**31)), (6, (-2, 7))):
            for value in values:
                item = list(base)
                item[index] = value
                with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                    CHECK.queue([item], 7, 6)
        for value in ([base] * 6, [base[:-1]], [base + [0]], "queue"):
            with self.assertRaises(ValueError):
                CHECK.queue(value, 7, 6)

    def test_core_queue_and_group_order_domains_differ(self):
        record = copy.deepcopy(self.records[1])
        record["actors"][0]["order"] = record["state"]["robots"][0]["order"] = 9
        CHECK.observations(record)
        for width in (5, 7):
            with self.assertRaises(ValueError):
                CHECK.queue([[9, 0] + [0] * (width - 2)], width, 4 if width == 5 else 6)
        CHECK.queue([[8, 8, 0, 2**32 - 1, 4]], 5, 4)

    def test_extra_patient_object_projection(self):
        for patient in (5, 6):
            record = copy.deepcopy(self.records[1])
            record["actors"][0]["queue"] = [[5, 3, 0, 0, 0, 0, patient]]
            record["state"]["robots"][0]["queue"] = [[5, 3, 0, 0, -1]]
            CHECK.observations(record)

    def test_missing_and_malformed_observations(self):
        for key in ("actors", "logic_order", "pulse_counts", "cached_health", "first_core", "changed", "tick_target"):
            with self.subTest(key=key):
                self.reject_observation(lambda r, key=key: r.pop(key))
        for field in ("x_bits", "y_bits", "health_bits", "group", "place", "in_place", "order", "destination", "queue"):
            with self.subTest(field=field):
                self.reject_observation(lambda r, field=field: r["actors"][0].pop(field))
        self.reject_observation(lambda r: r["actors"].pop())
        self.reject_observation(lambda r: r["actors"][0].update(present=False))
        self.reject_observation(lambda r: r["actors"][0].update(alive=False))
        self.reject_observation(lambda r: r["actors"][0].update(health_bits=2**32))
        self.reject_observation(lambda r: r["actors"][0].update(destination=[True, 0]))
        self.reject_observation(lambda r: r.update(first_core=True))

    def test_correlated_damage_rejected_before_comparison(self):
        record = copy.deepcopy(self.records[1])
        record["actors"][0]["queue"] = [[1, 0, 0, 0, "stale", 0, 0]]
        record["state"]["robots"][0]["queue"] = [[1, 0, 0, 0, 0]]
        left = list(self.records)
        left[1] = record
        right = copy.deepcopy(left)
        self.assertIsNone(CHECK.TRACES.compare_records(left, right))
        with patch.object(CHECK, "queue", lambda *args: None):
            CHECK.validate(left, ("retirement", "pm", 16), arguments(), REVISION)
        for damaged in (left, right):
            with self.assertRaises(ValueError):
                CHECK.validate(damaged, ("retirement", "pm", 16), arguments(), REVISION)

    def test_meaningful_observations_are_compared(self):
        for field in ("target", "tick_target", "first_core", "changed", "logic_order", "pulse_counts", "cached_health"):
            changed = list(self.records)
            changed[1] = copy.deepcopy(changed[1])
            changed[1][field] = None
            self.assertIsNotNone(CHECK.TRACES.compare_records(self.records, changed))
        for index in range(7):
            left = [dict(type="snapshot", state={}, actors=[dict(queue=[[0]*7])])]
            right = copy.deepcopy(left)
            right[0]["actors"][0]["queue"][0][index] = 1
            self.assertIsNotNone(CHECK.TRACES._first_difference(left, right))

    def test_tail_and_duplicate_key_rejection(self):
        lines = [json.dumps(r) + "\n" for r in self.records]
        for bad in (lines[:-1], lines + lines[-1:], lines[:-1] + [json.dumps(dict(self.records[-1], frame=0))]):
            with self.assertRaises(ValueError):
                CHECK.TRACES.read_records(bad)
        with self.assertRaises(ValueError):
            CHECK.TRACES.read_records([lines[0].replace('"schema": 1', '"schema": 1, "schema": 1')] + lines[1:])

    def test_pulse_schedule_and_retirement_guards(self):
        indices = [next(i for i, r in enumerate(self.records) if r.get("phase") == phase)
                   for phase in ("before-change", "after-change", "final")]
        for index, edit in ((indices[0], lambda r: r.update(target=6)),
                            (indices[1], lambda r: r.update(first_core=5)),
                            (indices[2], lambda r: r.update(pulse_counts=[2, 4]))):
            records = list(self.records)
            records[index] = copy.deepcopy(records[index])
            edit(records[index])
            with self.assertRaises(ValueError):
                CHECK.validate(records, ("retirement", "pm", 16), arguments(), REVISION)

    def test_clean_historical_reference_identity(self):
        header = dict(self.records[0], revision="2" * 40)
        CHECK.identity(header, arguments())
        CHECK.identity(header, arguments(), "2" * 40)
        for changes in (dict(dirty=1), dict(revision="short")):
            with self.assertRaises(ValueError):
                CHECK.identity(dict(header, **changes), arguments())
        with self.assertRaises(ValueError):
            CHECK.identity(header, arguments(), REVISION)
        CHECK.identity(dict(header, dirty=1, revision="diagnostic"), arguments(revision=None))

    def test_unpinned_reference_still_requires_clean_full_identity(self):
        for changes in (dict(dirty=1), dict(revision="short")):
            with self.assertRaises(ValueError):
                CHECK.identity(dict(self.records[0], **changes), arguments(revision=None), reference=True)
        CHECK.identity(dict(self.records[0], revision="2" * 40), arguments(revision=None), reference=True)

    def test_cli_rejects_missing_inventory_and_invalid_pins(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stderr(io.StringIO()):
            args = [directory, "--configuration", "Debug", "--compiler", "MinGW", "--target", "EXE",
                    "--cheats", "OFF", "--trace", "OFF"]
            for extra in ([], ["--revision", "short"], ["--reference-revision", REVISION]):
                self.assertEqual(CHECK.main(args + extra), 1)


if __name__ == "__main__":
    unittest.main()
