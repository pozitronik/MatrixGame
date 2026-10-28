"""Synthetic semantic and identity controls for the bounded bomb-escape corpus."""

import argparse
import contextlib
import copy
import importlib.util
import io
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "bomb_escape_check", Path(__file__).with_name("check_bomb_escape_traces.py")
)
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
REVISION = "1" * 40


def arguments(**changes):
    values = dict(
        configuration="Debug",
        compiler="MinGW",
        target="EXE",
        cheats=False,
        trace=False,
        revision=REVISION,
        reference_revision=None,
    )
    values.update(changes)
    return argparse.Namespace(**values)


def header(family, detonate, containment, step, replay=0):
    return dict(
        type="header",
        schema=1,
        fixture="bomb-escape-journey-v1",
        revision=REVISION,
        dirty=0,
        rng="ParkMiller",
        seed=84,
        step_ms=step,
        max_frames=5000,
        precision_control=0,
        initially_selected=False,
        build_exe=True,
        cheats=False,
        trace=False,
        delayed_damage="Corrected",
        group_counts="Corrected",
        place_containment=containment.capitalize(),
        zero_x_trace="Corrected",
        assert_off=False,
        configuration="Debug",
        compiler="MinGW",
        compiler_version="synthetic",
        navigation_layout="open-v1",
        audio="unavailable",
        render="storage-device-no-draw-cursor-surrogate",
        viewport=[1280, 720],
        refuge=family,
        detonate=detonate,
        replay=replay,
    )


def synthetic_trace(family="unreachable", detonate=False, containment="corrected", step=10, replay=0):
    """Exercise the source's event relationships; this is data validation, not a simulated battle."""
    unreachable = family == "unreachable"
    redivert = family == "free" and not detonate
    aligned = lambda time: ((time + step - 1) // step) * step
    clear = aligned(1200) if unreachable else 8910 if redivert and step == 10 else 8976 if redivert else aligned(1000)
    damage = clear + (300 // step + 1) * step if detonate else 0
    retired = damage + 20 * step if detonate else clear
    arrived = aligned(2000 if unreachable else 10000 if redivert else 4000)
    end = max(aligned(arrived + 500), retired)
    refuge_time, first_return = aligned(2000), aligned(3000)
    goal = [20, 20] if unreachable else [8, 48] if family == "reserved" and containment == "corrected" else [48, 8]
    progress = dict.fromkeys(CHECK.EVENTS, 0)
    maximum = 0.0
    sample_frame = None
    bomb_sample = refuge_sample = None
    cleared = False
    yield header(family, detonate, containment, step, replay)

    def state(phase, now, *, post=False):
        nonlocal maximum, sample_frame, bomb_sample, refuge_sample
        initial = phase == "setup-ready"
        if not now:
            x, y = 180.0, 100.0
        elif unreachable:
            fraction = min(1.0, now / arrived)
            x, y = 180 + 40 * fraction, 100 + 120 * fraction
        elif now < aligned(1000):
            x, y = 180.0, 100.0
        elif now < refuge_time:
            x, y = (100.0, 450.0) if goal == [8, 48] else (450.0, 100.0)
        elif now < arrived:
            x, y = (100.0, 500.0) if goal == [8, 48] else (500.0, 100.0)
        else:
            x = y = 220.0
        x, y = CHECK.floating(CHECK.float_bits(x)), CHECK.floating(CHECK.float_bits(y))
        if not initial and (sample_frame != now or phase == "planned"):
            sample_frame = now
            bomb_sample = {"value_bits": CHECK.float_bits(math.hypot(x - 100, y - 100))}
            refuge_sample = None
            if phase != "planned" and not unreachable and not progress["refuge"]:
                refuge_sample = {
                    "value_bits": CHECK.float_bits(math.hypot(x - (goal[0] + 2) * 10, y - (goal[1] + 2) * 10))
                }
            maximum = max(maximum, CHECK.sample(bomb_sample))
        returning = not initial and not unreachable and (now < first_return or (redivert and clear <= now < arrived))
        moving = not initial and now < arrived
        destination = (goal if returning else [20, 20]) if moving else [-1, -1]
        actors, robots = [], []
        for index in range(1, 5):
            absent = index == 3 and cleared and (not detonate or now >= retired)
            if absent:
                actors.append(dict(id=3, present=False))
                robots.append(dict(id=3, alive=False))
                continue
            position = ((120.0, 100.0), (x, y), (100.0, 100.0), (540.0, 540.0))[index - 1]
            health = 9 if detonate and now >= damage and (index == 1 or (index == 2 and unreachable)) else 10
            queue = []
            move = (
                destination
                if index == 2
                else [48, 8] if index == 1 and family == "reserved" and not initial else [-1, -1]
            )
            saved = [20, 20] if index == 2 and returning else [-1, -1]
            if move != [-1, -1]:
                queue.append([1, 0, CHECK.float_bits(move[0]), CHECK.float_bits(move[1]), 0, 0, 0])
            if saved != [-1, -1]:
                queue.append([3, 0, CHECK.float_bits(20), CHECK.float_bits(20), 0, 0, 0])
            actor = dict(
                id=index,
                present=True,
                alive=True,
                state=2,
                x_bits=CHECK.float_bits(position[0]),
                y_bits=CHECK.float_bits(position[1]),
                health_bits=CHECK.float_bits(health),
                group=-1,
                place=-1,
                moving=move != [-1, -1],
                move=move,
                returning=saved != [-1, -1],
                saved=saved,
                must_die=index == 3 and cleared,
                observes_cover=index == 3,
                queue=queue,
            )
            actors.append(actor)
            robots.append(
                dict(
                    id=index,
                    alive=True,
                    state=2,
                    x_bits=actor["x_bits"],
                    y_bits=actor["y_bits"],
                    health_bits=actor["health_bits"],
                    group=-1,
                    side=1 if index <= 2 else 2,
                    order=-1,
                    in_place=False,
                    target=0,
                    target_is_null=True,
                    destination=[-1, -1],
                    queue=[[q[0], q[1], q[2], q[3], q[6]] for q in queue],
                )
            )
        if phase in ("tick", "before-clearance"):
            if not unreachable:
                if now >= aligned(1000) and not progress["safe"]:
                    progress["safe"] = now
                if now >= refuge_time and not progress["refuge"]:
                    progress["refuge"] = now
                if now >= first_return and not returning and moving and not progress["first_return"]:
                    progress["first_return"] = now
                if redivert and now >= clear and returning and not progress["second_escape"]:
                    progress["second_escape"] = now
            if phase == "tick" and cleared:
                if not actors[2]["present"] and not progress["retired"]:
                    progress["retired"] = now
                if detonate and now >= damage and not progress["damage"]:
                    progress["damage"] = now
                if now >= arrived and not progress["arrived"]:
                    progress["arrived"] = now
        return dict(
            type="snapshot",
            phase=phase,
            original_prefix=("detonate:" if detonate else "remove:") if phase == "clearance" else "",
            state=dict(frame=now // step, time_ms=now, rng_draws=7, robots=robots, player_groups=[]),
            bomber_core_present=actors[2]["present"],
            bomber_core=3 if actors[2]["present"] else 0,
            milestones=dict(
                progress,
                maximum_distance_bits=CHECK.float_bits(maximum),
                bomb_distance=bomb_sample,
                refuge_distance=refuge_sample,
            ),
            kills=[0, 0],
            start_edge_masks=[2, 2] if unreachable and not initial else [0, 0],
            actors=actors,
        )

    def command(name, now, **payload):
        return dict(
            type="command", name=name, frame=now // step, time_ms=now, rng_draws_before=7, rng_draws_after=7, **payload
        )

    yield state("setup-ready", 0)
    if family == "reserved":
        yield command("reserve-cover", 0, actor=1, destination=[48, 8])
    elif unreachable:
        yield command("block-refuge-edges", 0, actor=2, masks=[2, 2])
    yield command("initial-move", 0, actor=2, destination=[20, 20])
    yield command("escape-from-bomb", 0, side=1)
    yield state("planned", 0)
    for now in range(step, end + step, step):
        if now == clear:
            yield state("before-clearance", now)
            yield command("detonate" if detonate else "remove", now, actor=3)
            cleared = True
            progress["cleared"] = now
            yield state("clearance", now)
        yield state("tick", now)
    yield state("final", end)
    yield dict(type="complete", frame=end // step, time_ms=end, rng_draws=7)


class BombEscapeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in synthetic_trace())

    def test_all_families_schedules_and_compatibility_modes(self):
        for family in ("free", "reserved", "unreachable"):
            for detonate in (False, True):
                for containment in ("corrected", "original"):
                    for step in (10, 16):
                        with self.subTest(family=family, detonate=detonate, containment=containment, step=step):
                            records = CHECK.TRACES.read_records(
                                json.dumps(r) + "\n" for r in synthetic_trace(family, detonate, containment, step)
                            )
                            CHECK.validate(
                                records, (family, "pm", step, containment, detonate), 0, arguments(), REVISION
                            )

    def test_complete_inventory_has_disjoint_callers(self):
        pairs = list(CHECK.pairs())
        names = [name for _, files in pairs for name in files]
        self.assertEqual(len(pairs), 48)
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(len(names), 96)
        for family in ("free", "reserved", "unreachable"):
            self.assertEqual(sum(name.startswith(f"bomb-escape-{family}-") for name in names), 32)

    def test_full_order_domains_and_capacity(self):
        good = [8, 8, 0, 0xFFFFFFFF, 1, -(2**31), 4]
        CHECK.queue([good], 7)
        for index in range(7):
            for bad in (True, 1.5, "1"):
                value = list(good)
                value[index] = bad
                with self.subTest(index=index, bad=bad), self.assertRaises(ValueError):
                    CHECK.queue([value], 7)
        for index, values in (
            (0, (-1, 9)),
            (1, (-1, 9)),
            (2, (-1, 2**32)),
            (3, (-1, 2**32)),
            (4, (-1, 2**32)),
            (5, (-(2**31) - 1, 2**31)),
            (6, (-2, 5)),
        ):
            for bad in values:
                value = list(good)
                value[index] = bad
                with self.assertRaises(ValueError):
                    CHECK.queue([value], 7)
        for bad in ([good] * 6, [good[:-1]], [good + [0]], "queue"):
            with self.assertRaises(ValueError):
                CHECK.queue(bad, 7)
        CHECK.queue([[8, 8, 0, 0xFFFFFFFF, 4]], 5)
        with self.assertRaises(ValueError):
            CHECK.queue([[1, 0, 0, 0, 5]], 5)

    def test_actual_compiler_version_contracts(self):
        for compiler, good, bad in (
            ("MinGW", "13.2.0", ("", 1938, True, None)),
            ("MSVC", 1938, ("1938", 0, True, 2**31)),
        ):
            h = dict(self.records[0], compiler=compiler, compiler_version=good)
            CHECK.identity(h, arguments(compiler=compiler))
            for value in bad:
                with self.assertRaises(ValueError):
                    CHECK.identity(dict(h, compiler_version=value), arguments(compiler=compiler))

    def test_reference_identity_independent_of_candidate_pin(self):
        h = dict(self.records[0], revision="2" * 40)
        CHECK.identity(h, arguments(), "2" * 40, True)
        CHECK.identity(h, arguments(revision=None), reference=True)
        for changed in (dict(dirty=1), dict(revision="short")):
            with self.assertRaises(ValueError):
                CHECK.identity(dict(h, **changed), arguments(revision=None), reference=True)
        with self.assertRaises(ValueError):
            CHECK.identity(h, arguments(), REVISION, True)
        CHECK.identity(dict(h, dirty=1, revision="diagnostic"), arguments(revision=None))

    def test_replay_ordinal_is_only_pair_normalization(self):
        other = list(self.records)
        other[0] = dict(other[0], replay=1)
        self.assertIsNone(CHECK.pair_difference(self.records, other))
        for field in ("trace", "detonate", "refuge", "place_containment"):
            altered = list(other)
            altered[0] = dict(altered[0])
            altered[0][field] = None
            self.assertIsNotNone(CHECK.pair_difference(self.records, altered))

    def test_original_vector_excludes_extra_phases(self):
        vectors = list(CHECK.original_vectors(self.records))
        snapshots = [r for r in self.records if r.get("phase") in ("planned", "clearance", "tick")]
        self.assertEqual(len(vectors), len(snapshots))
        self.assertEqual(sum(v.startswith("remove:") for v in vectors), 1)
        self.assertFalse(any("setup-ready" in v or "before-clearance" in v for v in vectors))

    def test_correlated_order_damage_needs_independent_guard(self):
        bad = list(self.records)
        bad[1] = copy.deepcopy(bad[1])
        bad[1]["actors"][0]["queue"] = [[1, 0, 0, 0, "stale", 0, 0]]
        bad[1]["state"]["robots"][0]["queue"] = [[1, 0, 0, 0, 0]]
        other = copy.deepcopy(bad)
        self.assertIsNone(CHECK.pair_difference(bad, other))
        with patch.object(CHECK, "queue", lambda *args: None):
            CHECK.validate(bad, ("unreachable", "pm", 10, "corrected", False), 0, arguments(), REVISION)
        for records in (bad, other):
            with self.assertRaises(ValueError):
                CHECK.validate(records, ("unreachable", "pm", 10, "corrected", False), 0, arguments(), REVISION)

    def test_lifetime_payload_and_core_presence(self):
        initial = copy.deepcopy(self.records[1])
        for edit in (
            lambda r: r["actors"][2].update(present=False),
            lambda r: r["actors"][2].update(alive=False, state=5),
            lambda r: r.update(bomber_core_present=False),
        ):
            bad = copy.deepcopy(initial)
            edit(bad)
            with self.assertRaises(ValueError):
                CHECK.observations(bad)
        # An object may leave the live list before its retained core becomes null.
        initial["actors"][2] = dict(id=3, present=False)
        initial["state"]["robots"][2] = dict(id=3, alive=False)
        CHECK.observations(initial)
        self.assertTrue(initial["bomber_core_present"])
        self.assertEqual(initial["bomber_core"], 3)

    def test_missing_typed_fields_and_tails(self):
        for key in ("actors", "milestones", "bomber_core_present", "start_edge_masks", "kills", "original_prefix"):
            bad = copy.deepcopy(self.records[1])
            bad.pop(key)
            with self.subTest(key=key), self.assertRaises((ValueError, KeyError, TypeError)):
                CHECK.observations(bad)
        for field in ("moving", "saved", "place", "queue", "observes_cover", "health_bits"):
            bad = copy.deepcopy(self.records[1])
            bad["actors"][0].pop(field)
            with self.subTest(field=field), self.assertRaises(ValueError):
                CHECK.observations(bad)
        lines = [json.dumps(r) + "\n" for r in self.records]
        for bad in (lines[:-1], lines + lines[-1:], lines[:-1] + [json.dumps(dict(self.records[-1], frame=0))]):
            with self.assertRaises(ValueError):
                CHECK.TRACES.read_records(bad)

    def test_milestone_and_footer_faults(self):
        index = next(i for i, r in enumerate(self.records) if r.get("phase") == "clearance")
        for where, edit in (
            (index, lambda r: r.update(original_prefix="detonate:")),
            (index, lambda r: r["milestones"].update(retired=r["state"]["time_ms"])),
            (-1, lambda r: r.update(rng_draws=r["rng_draws"] + 1)),
        ):
            bad = list(self.records)
            bad[where] = copy.deepcopy(bad[where])
            edit(bad[where])
            with self.assertRaises(ValueError):
                CHECK.validate(bad, ("unreachable", "pm", 10, "corrected", False), 0, arguments(), REVISION)

    def test_source_samples_retain_float_rounding_and_short_circuit(self):
        self.assertIsNone(CHECK.sample(None))
        self.assertEqual(CHECK.sample({"value_bits": CHECK.float_bits(330.000001)}), 330.0)
        self.assertEqual(CHECK.sample({"value_bits": CHECK.float_bits(0.99999999)}), 1.0)
        for value in (
            True,
            1,
            {"value_bits": True},
            {"value_bits": -1},
            {"value_bits": 0x7F800000},
            {"value_bits": CHECK.float_bits(-1)},
        ):
            with self.assertRaises(ValueError):
                CHECK.sample(value)
        bad = list(self.records)
        index = next(i for i, r in enumerate(bad) if r.get("phase") == "tick")
        bad[index] = copy.deepcopy(bad[index])
        bad[index]["milestones"]["refuge_distance"] = {"value_bits": 0}
        with self.assertRaises(ValueError):
            CHECK.validate(bad, ("unreachable", "pm", 10, "corrected", False), 0, arguments(), REVISION)

    def test_cli_rejects_incomplete_inventory_and_invalid_pins(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stderr(io.StringIO()):
            args = [
                directory,
                "--configuration",
                "Debug",
                "--compiler",
                "MinGW",
                "--target",
                "EXE",
                "--cheats",
                "OFF",
                "--trace",
                "OFF",
            ]
            for extra in ([], ["--revision", "short"], ["--reference-revision", REVISION]):
                self.assertEqual(CHECK.main(args + extra), 1)


if __name__ == "__main__":
    unittest.main()
