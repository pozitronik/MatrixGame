"""Synthetic source-contract and correlated-data controls for physical capture recordings."""

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
    "capture_check", Path(__file__).with_name("check_capture_completion_traces.py")
)
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
REVISION = "1" * 40


def args(**changes):
    result = dict(
        configuration="Debug",
        compiler="MinGW",
        target="EXE",
        cheats=False,
        trace=False,
        revision=REVISION,
        reference_revision=None,
    )
    result.update(changes)
    return argparse.Namespace(**result)


def fb(value):
    return CHECK.struct.unpack("<I", CHECK.struct.pack("<f", value))[0]


def synthetic(base=True, owner=2, route="context", step=16):
    """Independent data fixtures retain the source schedules, not a fake runtime physics model."""
    complete = (
        (5580 if step == 10 else 5600)
        if base
        else (18540 if step == 10 else 18544) if owner else (7530 if step == 10 else 7536)
    )
    first = 1280 if base else 630 if step == 10 else 640
    second = (4330 if step == 10 else 4336) if base else (11030 if step == 10 else 11040) if owner else 0
    end = complete // step + 100
    header = dict(
        type="header",
        schema=1,
        fixture="base-capture-route-completion-v2" if base else "capture-route-completion-v1",
        revision=REVISION,
        dirty=0,
        rng="ParkMiller",
        seed=79,
        step_ms=step,
        max_frames=3000,
        precision_control=0,
        initially_selected=True,
        build_exe=True,
        cheats=False,
        trace=False,
        assert_off=False,
        configuration="Debug",
        compiler="MinGW",
        compiler_version="synthetic",
        delayed_damage="Corrected",
        group_counts="Corrected",
        place_containment="Corrected",
        zero_x_trace="Corrected",
        audio="unavailable",
        navigation_layout="central-obstacle-v1",
        render="storage-device-no-draw-cursor-surrogate",
        viewport=[1280, 720],
        kind=0 if base else 1,
        initial_owner=owner,
        route=route,
        caller=CHECK.caller(base, owner, route),
    )
    yield header

    def state(phase, frame):
        now = frame * step
        ready = phase == "ready"
        transferred = now >= complete
        building_owner = 1 if transferred else 0 if not base and owner and now >= second else owner
        paint = (
            14
            if transferred
            else (14 if now < first else 13 if now < second else 0) if owner else 1 if now >= first else 0
        )
        if base:
            paint = 14 if owner or transferred else 0
        color = (0xFFFF0000 if owner else 0) if base or not transferred else 0xFF00FF00
        progress = [
            first if now >= first else 0,
            second if second and now >= second else 0,
            complete if transferred else 0,
        ]
        actors = []
        robots = []
        for index in range(1, 5):
            if base and transferred and index == 1:
                actors.append(dict(id=1, present=False))
                robots.append(dict(id=1, alive=False))
                continue
            capture = not ready and index == 1 and not transferred
            group = 0 if not ready and index <= 2 else -1
            target = 5 if group >= 0 else 0
            queue = [[7, 0, 0, 0, 0, 0, 5]] if capture else []
            actor = dict(
                id=index,
                present=True,
                alive=True,
                state=2,
                x_bits=fb(100 + index * 10),
                y_bits=fb(200),
                health_bits=fb(100),
                group=group,
                group_target=target,
                group_order=2 if group >= 0 else -1,
                destination=[10, 10] if group >= 0 else [-1, -1],
                queue=queue,
                capture_target=5 if capture else 0,
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
                    group=group,
                    order=actor["group_order"],
                    target=-1 if target else 0,
                    target_is_null=target == 0,
                    side=1 if index <= 2 else 2,
                    destination=actor["destination"],
                    in_place=False,
                    queue=[[q[0], q[1], q[2], q[3], -1] for q in queue],
                )
            )
        queue = [dict(id=6, owner=2), dict(id=7, owner=2)] if base and owner and not transferred else []
        building = dict(
            id=5,
            owner=building_owner,
            kind=0 if base else 1,
            paint=paint,
            color=color,
            capturer=0 if transferred and now == complete else 4 if transferred else 0,
            keepalive=0,
            queue_count=len(queue),
            queue=queue,
        )
        if base:
            building.update(
                base_state=0 if ready or transferred else 1 if now < first else 2 if now < second else 3, floor_bits=0
            )
        balances = (
            [[200, 100 + (2 * (11 + r) if owner and transferred else 0)] for r in range(4)]
            if base
            else [[300, 300] for _ in range(4)]
        )
        group_members = 1 if base and transferred else 2
        groups = [] if ready else [dict(slot=0, stored_count=group_members, members=group_members, order=2, target=-1)]
        return dict(
            type="snapshot",
            phase=phase,
            state=dict(frame=frame, time_ms=now, rng_draws=7 if ready else 8, robots=robots, player_groups=groups),
            milestones=progress,
            paused=False,
            player_status=1,
            preorder=0,
            building=building,
            balances=balances,
            logic_order=[5] + [a["id"] for a in actors if a["present"]],
            actors=actors,
        )

    def command(name, before, after, **extra):
        return dict(
            type="command",
            frame=0,
            time_ms=0,
            name=name,
            target=5,
            rng_draws_before=before,
            rng_draws_after=after,
            trace_position=[fb(100), fb(100), fb(10)],
            preorder_before=CHECK.PREORDER if name == "capture-confirm" else 0,
            preorder_after=CHECK.PREORDER if name in ("capture-key", "capture-button") else 0,
            **extra
        )

    yield state("ready", 0)
    if route == "context":
        yield command("capture-context", 7, 8, button="right", client=[0, 0])
    else:
        if route == "keyboard":
            yield command("capture-key", 7, 7, key=75, pressed=[True, False])
        else:
            yield command("capture-button", 7, 7, element="oca")
        yield command("capture-confirm", 7, 8, button="left", client=[0, 0])
    yield state("command", 0)
    for frame in range(1, end + 1):
        yield state("tick" if frame <= complete // step else "tail", frame)
    yield state("final", end)
    yield dict(type="complete", frame=end, time_ms=end * step, rng_draws=8)


class CaptureCompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in synthetic())

    def test_all_existing_route_kind_owner_schedule_branches(self):
        for base in (False, True):
            for owner in (0, 2):
                for route in ("context", "keyboard", "button"):
                    for step in (10, 16):
                        with self.subTest(base=base, owner=owner, route=route, step=step):
                            records = CHECK.TRACES.read_records(
                                json.dumps(r) + "\n" for r in synthetic(base, owner, route, step)
                            )
                            CHECK.validate(records, (base, owner, route, "pm", step), args(), REVISION)

    def test_exact48_inventory_and_whole_caller_counts(self):
        inventory = list(CHECK.expected())
        self.assertEqual(len(inventory), 48)
        self.assertEqual(len({n for n, _ in inventory}), 48)
        counts = CHECK.collections.Counter(CHECK.caller(*description[:3]) for _, description in inventory)
        self.assertEqual(sorted(counts.values()), [4, 4, 8, 16, 16])

    def test_full_queue_scalar_domains_and_capacity(self):
        good = [8, 8, 0, 0xFFFFFFFF, 1, -(2**31), 7]
        CHECK.queue([good], 7, 7)
        for index in range(7):
            for bad in (True, 1.5, "1"):
                item = list(good)
                item[index] = bad
                with self.subTest(index=index, bad=bad), self.assertRaises(ValueError):
                    CHECK.queue([item], 7, 7)
        for index, values in (
            (0, (-1, 9)),
            (1, (-1, 9)),
            (2, (-1, 2**32)),
            (3, (-1, 2**32)),
            (4, (-1, 2**32)),
            (5, (-(2**31) - 1, 2**31)),
            (6, (-2, 8)),
        ):
            for bad in values:
                item = list(good)
                item[index] = bad
                with self.assertRaises(ValueError):
                    CHECK.queue([item], 7, 7)
        for bad in ([good] * 6, [good[:-1]], [good + [0]], "queue"):
            with self.assertRaises(ValueError):
                CHECK.queue(bad, 7, 7)

    def test_actor_side_domain_and_ownership_swap_correlated(self):
        for side in (99, 1, True):
            bad = list(self.records)
            bad[1] = copy.deepcopy(bad[1])
            bad[1]["state"]["robots"][2]["side"] = side
            other = copy.deepcopy(bad)
            self.assertIsNone(CHECK.TRACES.compare_records(bad, other))
            for records in (bad, other):
                with self.assertRaises(ValueError):
                    CHECK.validate(records, (True, 2, "context", "pm", 16), args(), REVISION)

    def test_foreign_building_projection_and_group_orders(self):
        index = next(i for i, r in enumerate(self.records) if r.get("phase") == "command")
        record = self.records[index]
        CHECK.observations(record)
        self.assertEqual(record["actors"][0]["queue"][0][6], 5)
        self.assertEqual(record["state"]["robots"][0]["queue"][0][4], -1)
        for width, maximum in ((5, 4), (7, 7)):
            with self.assertRaises(ValueError):
                CHECK.queue([[9, 0] + [0] * (width - 2)], width, maximum)

    def test_opaque_queue_counts_identity_order_and_ownership(self):
        for edit in (
            lambda r: r["building"].update(queue_count=1),
            lambda r: r["building"]["queue"][0].update(id=99),
            lambda r: r["building"]["queue"][0].update(id=7),
            lambda r: r["building"]["queue"][0].update(alive=True),
            lambda r: r["building"]["queue"][0].update(owner=1),
        ):
            bad = list(self.records)
            bad[1] = copy.deepcopy(bad[1])
            edit(bad[1])
            with self.assertRaises(ValueError):
                CHECK.validate(bad, (True, 2, "context", "pm", 16), args(), REVISION)

    def test_base_transfer_null_is_not_tail_null_rule(self):
        CHECK.validate(self.records, (True, 2, "context", "pm", 16), args(), REVISION)
        final = self.records[-2]
        self.assertEqual(final["building"]["capturer"], 4)
        index = next(
            i for i, r in enumerate(self.records) if r.get("phase") == "tick" and r["state"]["time_ms"] == 5600
        )
        bad = list(self.records)
        bad[index] = copy.deepcopy(bad[index])
        bad[index]["building"]["capturer"] = 4
        with self.assertRaises(ValueError):
            CHECK.validate(bad, (True, 2, "context", "pm", 16), args(), REVISION)

    def test_nonlive_actor_has_no_live_union_fields(self):
        final = copy.deepcopy(self.records[-2])
        final["actors"][0] = dict(id=1, present=True, alive=False, state=5)
        final["logic_order"].insert(1, 1)
        CHECK.observations(final)
        for field in ("queue", "health_bits", "capture_target"):
            bad = copy.deepcopy(final)
            bad["actors"][0][field] = 0
            with self.assertRaises(ValueError):
                CHECK.observations(bad)

    def test_correlated_Param3_requires_independent_guard(self):
        bad = list(self.records)
        bad[1] = copy.deepcopy(bad[1])
        bad[1]["actors"][0]["queue"] = [[1, 0, 0, 0, "stale", 0, 0]]
        bad[1]["state"]["robots"][0]["queue"] = [[1, 0, 0, 0, 0]]
        other = copy.deepcopy(bad)
        self.assertIsNone(CHECK.TRACES.compare_records(bad, other))
        with patch.object(CHECK, "queue", lambda *x: None):
            CHECK.validate(bad, (True, 2, "context", "pm", 16), args(), REVISION)
        for records in (bad, other):
            with self.assertRaises(ValueError):
                CHECK.validate(records, (True, 2, "context", "pm", 16), args(), REVISION)

    def test_command_milestone_and_tail_guards(self):
        for index, edit in (
            (2, lambda r: r.update(target=6)),
            (2, lambda r: r.update(preorder_before=True)),
            (-2, lambda r: r["milestones"].__setitem__(2, 5580)),
            (-1, lambda r: r.update(rng_draws=9)),
        ):
            bad = list(self.records)
            bad[index] = copy.deepcopy(bad[index])
            edit(bad[index])
            with self.assertRaises(ValueError):
                CHECK.validate(bad, (True, 2, "context", "pm", 16), args(), REVISION)
        lines = [json.dumps(r) + "\n" for r in self.records]
        for bad in (lines[:-1], lines + lines[-1:], lines[:-1] + [json.dumps(dict(self.records[-1], frame=0))]):
            with self.assertRaises(ValueError):
                CHECK.TRACES.read_records(bad)

    def test_actual_compiler_version_contract(self):
        for compiler, good, bad in (
            ("MinGW", "13.2.0", ("", 1938, True, None)),
            ("MSVC", 1938, ("1938", 0, True, 2**31)),
        ):
            header = dict(self.records[0], compiler=compiler, compiler_version=good)
            CHECK.identity(header, args(compiler=compiler))
            for value in bad:
                with self.assertRaises(ValueError):
                    CHECK.identity(dict(header, compiler_version=value), args(compiler=compiler))

    def test_independent_historical_reference_identity(self):
        header = dict(self.records[0], revision="2" * 40)
        CHECK.identity(header, args(), "2" * 40, True)
        CHECK.identity(header, args(revision=None), reference=True)
        for changes in (dict(dirty=1), dict(revision="short")):
            with self.assertRaises(ValueError):
                CHECK.identity(dict(header, **changes), args(revision=None), reference=True)
        with self.assertRaises(ValueError):
            CHECK.identity(header, args(), REVISION, True)
        CHECK.identity(dict(header, dirty=1, revision="diagnostic"), args(revision=None))

    def test_cli_rejects_missing_inventory_and_invalid_pins(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stderr(io.StringIO()):
            base = [
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
                self.assertEqual(CHECK.main(base + extra), 1)


if __name__ == "__main__":
    unittest.main()
