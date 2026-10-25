"""Synthetic serialization/guard contracts; actual physics remains in the existing C++ cases."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from contextlib import redirect_stderr
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "manual_recordings", Path(__file__).parents[1] / "tools/check-manual-control-recordings.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def profile(continuation="original", chassis="track", step=10, moving=False):
    return dict(chassis=chassis, kind=3 if chassis == "track" else 4, step=step,
                moving=moving, rng="pm", continuation=continuation)


def make_header(p):
    return dict(type="header", schema=1,
                fixture="manual-chassis-lifecycle-v1" if p["continuation"] == "original"
                else "manual-live-enemy-continuation-v1",
                manual_observation="direct-control-v1", navigation_layout="open-v1", seed=80,
                step_ms=p["step"], max_frames=3000, audio="unavailable", initially_selected=False,
                rng="ParkMiller", chassis=p["chassis"], chassis_kind=p["kind"], moving_target=p["moving"],
                continuation=p["continuation"], manual_actor=5, enter_key=13, mouse_button=1, forward_key=87,
                right_key=68, render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720],
                revision="a" * 40, dirty=0, compiler="MinGW", compiler_version="synthetic",
                configuration="Debug", precision_control=131072, assert_off=False, build_exe=True,
                cheats=False, trace=False, delayed_damage="Corrected", group_counts="Corrected",
                place_containment="Corrected", zero_x_trace="Corrected")


def recording(p):
    """Create a complete synthetic protocol history, without modeling engine movement."""
    header = make_header(p)
    t = CHECK.timing(p["step"])
    original = p["continuation"] == "original"
    live = p["continuation"] == "live"
    end = t["settled"] if original else t["finished"]
    held = dict(forward=False, right=False, enter=False, mouse_left=False)
    active = removed = moved = free = False
    specs = CHECK.command_specs(p, header)
    records = [header]
    group_destination = [8, 8]

    def snapshot(frame, phase):
        now = frame * p["step"]
        health = 100 if now < t["fire"] else 98 if now < t["fire_again"] else 96
        target_x, target_y = (310, 260) if p["moving"] and now >= t["fire"] else (300, 250)
        robots = [{"id": 1, "alive": False}, {"id": 2, "alive": False}]
        for actor, x, y in ((3, target_x, target_y), (4, 540, 540)):
            robot = dict(id=actor, alive=actor == 4 or not removed)
            if robot["alive"]:
                robot.update(x_bits=bits(x), y_bits=bits(y), health_bits=bits(health if actor == 3 else 100),
                             side=2, state=2, in_place=False, group=-1, order=-1, target=0,
                             target_is_null=True, destination=[-1, -1], queue=[])
            robots.append(robot)
        x = 100 if now < t["forward"] else 110 if now <= t["reset"] else 120
        y = 200
        if moved and now >= t["held_move"]:
            x, y = (100, 150)
        settled = moved and now >= t["settled"]
        if settled:
            x, y = (60, 60) if live else (100, 100)
        if free and now == end:
            x, y = 100, 300
        group = dict(present=moved or not active and now >= t["exit"])
        if group["present"]:
            group.update(order=1 if moved else 3, target=0, destination=list(group_destination), war=False)
        physical = []
        if active and held["mouse_left"] or (live and settled and not removed):
            physical = [[5, 3, 0, 0, 0, 0 if p["moving"] or active else 2, 0]]
        moving = moved and not settled or free and now < end
        move = [8, 28] if free else [4, 4] if live else [8, 8]
        speed = .5 if p["chassis"] == "track" else .7
        manual = dict(id=5, present=True, alive=True, state=2, chassis=p["kind"],
                      x_bits=bits(x), y_bits=bits(y), health_bits=bits(100),
                      speed_bits=bits(speed * 1.1 if active else speed),
                      forward_x_bits=bits(1 if now < t["turn"] else 0),
                      forward_y_bits=bits(0 if now < t["turn"] else 1), group=0 if group["present"] else -1,
                      in_place=settled, moving=bool(moving), move=move if moving else [-1, -1],
                      place=-1 if live and moved else 0, place_add=[4, 4] if live and moved else [-1, -1],
                      enemy_count=1, target=0, attack_target=0, group_state=group, queue=physical,
                      weapon=dict(present=True, type=98, range_bits=bits(250), heat=0, heat_period=0,
                                  heat_mod=10, cooldown_period=0, cooldown_mod=10, on=1))
        aim = dict(defined=frame > 0, kind="none" if frame == 0 else "landscape" if removed else "object",
                   object=0 if frame == 0 or removed else 3)
        if frame:
            aim.update(x_bits=bits(300), y_bits=bits(250), z_bits=bits(20))
        reservations = [dict(id=3, present=not removed), dict(id=4, present=True, alive=True,
                                                            place=0 if live and now >= t["retry"] else -1)]
        if not removed:
            reservations[0].update(alive=True, place=0)
        return dict(type="snapshot", state=dict(frame=frame, time_ms=now, rng_draws=10,
                                                robots=robots, player_groups=[]), phase=phase, manual=manual,
                    control=dict(id=5 if active else 0, arcade=active, robot_mode=active),
                    input=dict(held), aim=aim, reservations=reservations)

    records.append(snapshot(0, "initial"))
    index = 0
    for frame in range(end // p["step"] + 1):
        if frame:
            records.append(snapshot(frame, "tick"))
        while index < len(specs) and specs[index][1] == frame * p["step"]:
            name, when, kind, code, value, location = specs[index]
            records.append(dict(type="command", frame=frame, time_ms=when, name=name, kind=kind, code=code,
                                value=value, point=location, rng_draws_before=10, rng_draws_after=10))
            if kind == "keyboard":
                held[{87: "forward", 68: "right", 13: "enter"}[code]] = bool(value)
            elif kind == "mouse":
                held["mouse_left"] = value == 0
            elif name == "input-reset":
                held = dict.fromkeys(held, False)
            if name == "enter-down":
                active = True
            elif name == "exit-down":
                active = False
            elif name == "remove-nearby-enemy":
                removed = True
            elif kind == "context-ground":
                moved = True
                free = name == "free-goal"
                group_destination = [8, 28] if free else [8, 8]
            records.append(snapshot(frame, name))
            checkpoint = {"exit-up": "manual-exit", "move-goal": "move-command",
                          "retry-goal": "retry-move", "free-goal": "free-move"}.get(name)
            if checkpoint:
                records.append(snapshot(frame, checkpoint))
            index += 1
    records.append(snapshot(frame, "finished"))
    observed = frame + (2 if original else 4)
    result = dict(exit_ms=t["exit"], retry_ms=0 if original else t["retry"],
                  existing_frames=0 if original else observed,
                  existing_prefix_frames=0 if original else t["exit"] // p["step"] + 1,
                  existing_digest_low=0, existing_digest_high=0, observed_frames=observed,
                  observation_digest_low=0, observation_digest_high=0, terminal_rng_draws=10)
    records.append(dict(type="complete", frame=frame, time_ms=end, manual_result=result))
    return records


class ManualRecordingContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = profile()
        cls.valid = recording(cls.profile)

    def checked(self, records, p=None):
        parsed = CHECK.TRACES.read_records(json.dumps(record) + "\n" for record in records)
        return CHECK.validate(parsed, p or self.profile)

    def test_inventory_preserves_eighty_existing_episodes_without_original_replays(self):
        groups = list(CHECK.inventory())
        self.assertEqual(80, sum(len(names) for _, names in groups))
        self.assertEqual(32, sum(len(names) == 2 for _, names in groups))
        self.assertEqual(16, sum(p["continuation"] == "original" for p, _ in groups))

    def test_complete_synthetic_original_live_and_removed_profiles(self):
        for p in (profile(), profile(chassis="hovercraft", step=16, moving=True),
                  profile("live"), profile("live", "hovercraft", 16, True),
                  profile("removed", "hovercraft", 10, True), profile("removed", step=16)):
            with self.subTest(profile=p):
                self.checked(recording(p), p)

    def test_header_profile_and_effective_types_are_checked(self):
        for field, value in (("chassis_kind", 4), ("seed", True), ("viewport", [1280.0, 720]),
                             ("trace", "false"), ("zero_x_trace", "unknown"), ("forward_key", False)):
            with self.subTest(field=field):
                header = make_header(self.profile)
                header[field] = value
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.header(header, self.profile)

    def test_correlated_missing_or_malformed_extra_actor_fields_fail(self):
        original = next(r for r in self.valid if r.get("phase") == "tick")
        edits = (lambda r: r["manual"].pop("forward_x_bits"),
                 lambda r: r["manual"].__setitem__("present", False),
                 lambda r: r["manual"].__setitem__("chassis", 4),
                 lambda r: r["manual"].__setitem__("queue", [[1, 0, 0, 0, 0, 0]]),
                 lambda r: r["manual"].__setitem__("queue", [[9, 0, 0, 0, 0, 0, 0]]),
                 lambda r: r["manual"].__setitem__("queue", [[1, 0, 0, 0, 0, True, 0]]),
                 lambda r: r["manual"]["weapon"].pop("heat"),
                 lambda r: r["input"].__setitem__("forward", 1),
                 lambda r: r["aim"].pop("z_bits"),
                 lambda r: r["reservations"][1].pop("place"))
        for index, edit in enumerate(edits):
            with self.subTest(edit=index):
                left = copy.deepcopy(original)
                edit(left)
                right = copy.deepcopy(left)
                self.assertEqual(left, right)
                for record in (left, right):
                    with self.assertRaises(CHECK.TRACES.TraceError):
                        CHECK.snapshot(record, self.profile, False)

    def test_uint32_order_payloads_and_signed_param4_are_preserved(self):
        value = copy.deepcopy(next(r for r in self.valid if r.get("phase") == "tick"))
        value["manual"]["queue"] = [[8, 8, 0xffffffff, 0, 0xffffffff, -(1 << 31), -1]] * 5
        CHECK.snapshot(value, self.profile, False)
        for cell in (2, 3, 4):
            for payload in (True, -1, 1 << 32, 1.0, "0"):
                bad = copy.deepcopy(value)
                bad["manual"]["queue"][0][cell] = payload
                with self.subTest(cell=cell, payload=payload), self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(bad, self.profile, False)

    def test_player_order_nine_is_valid_but_correlated_999_is_rejected(self):
        value = copy.deepcopy(next(r for r in self.valid if r.get("phase") == "tick"))
        value["state"]["robots"][2]["order"] = 9
        value["state"]["player_groups"] = [dict(slot=0, stored_count=1, members=1, order=9, target=-1)]
        value["manual"]["group"] = 0
        value["manual"]["group_state"] = dict(present=True, order=9, target=5, destination=[8, 8], war=False)
        CHECK.snapshot(value, self.profile, False)
        for edit in (lambda r: r["state"]["robots"][2].update(order=999),
                     lambda r: r["state"]["player_groups"][0].update(order=999),
                     lambda r: r["manual"]["group_state"].update(order=999)):
            left = copy.deepcopy(value)
            edit(left)
            right = copy.deepcopy(left)
            self.assertEqual(left, right)
            for record in (left, right):
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(record, self.profile, False)

    def test_zero_frame_aim_is_explicitly_undefined(self):
        value = copy.deepcopy(self.valid[1])
        CHECK.snapshot(value, self.profile, False)
        value["aim"]["x_bits"] = 0
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "uninitialized"):
            CHECK.snapshot(value, self.profile, False)

    def test_declared_live_removed_prefix_is_exact_and_original_is_separate(self):
        live = recording(profile("live"))
        removed = recording(profile("removed"))
        self.assertIsNone(CHECK.TRACES._first_difference(CHECK.prefix(live), CHECK.prefix(removed)))
        damaged = copy.deepcopy(live)
        damaged[1]["manual"]["forward_x_bits"] ^= 1
        self.assertIsNotNone(CHECK.TRACES._first_difference(CHECK.prefix(damaged), CHECK.prefix(removed)))

    def test_correlated_command_or_terminal_corruption_fails(self):
        edits = (lambda r: next(x for x in r if x.get("name") == "fire-down").update(point=[0, 0]),
                 lambda r: next(x for x in r if x.get("name") == "input-reset").update(code=0),
                 lambda r: next(x for x in r if x.get("name") == "move-goal").update(rng_draws_after=11),
                 lambda r: r[-1]["manual_result"].__setitem__("observed_frames", 0),
                 lambda r: r[-1]["manual_result"].__setitem__("terminal_rng_draws", 11))
        for index, edit in enumerate(edits):
            with self.subTest(edit=index):
                left = copy.deepcopy(self.valid)
                edit(left)
                right = copy.deepcopy(left)
                self.assertIsNone(CHECK.TRACES.compare_records(left, right))
                for records in (left, right):
                    with self.assertRaises(CHECK.TRACES.TraceError):
                        self.checked(records)

    def test_incomplete_or_duplicate_terminal_record_fails(self):
        for records in (self.valid[:-1], self.valid + [self.valid[-1]], self.valid + [{"type": "unexpected"}]):
            with self.subTest(length=len(records)), self.assertRaises(CHECK.TRACES.TraceError):
                self.checked(records)

    def test_cli_rejects_missing_inventory_and_invalid_identity(self):
        with tempfile.TemporaryDirectory() as directory, redirect_stderr(io.StringIO()):
            args = [directory, "--revision", "a" * 40, "--configuration", "Debug", "--compiler", "MinGW",
                    "--target", "EXE", "--cheats", "false", "--trace", "false"]
            self.assertEqual(1, CHECK.main(args))
            args[2] = "not-a-revision"
            self.assertEqual(1, CHECK.main(args))

    def test_strict_reference_has_its_own_clean_revision_and_matching_build(self):
        identity = dict(revision="a" * 40, configuration="Debug", compiler="MinGW",
                        target="EXE", cheats=False, trace=False)
        name = next(names[0] for p, names in CHECK.inventory() if p == self.profile)
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary) / "candidate"
            reference = Path(temporary) / "reference"
            candidate.mkdir()
            reference.mkdir()
            text = "".join(json.dumps(record) + "\n" for record in self.valid)
            (candidate / name).write_text(text, encoding="utf-8")
            historical = copy.deepcopy(self.valid)
            historical[0]["revision"] = "b" * 40
            reference_path = reference / name
            def write(value):
                reference_path.write_text("".join(json.dumps(record) + "\n" for record in value), encoding="utf-8")
            with mock.patch.object(CHECK, "inventory", return_value=[(self.profile, (name,))]):
                write(historical)
                self.assertEqual((1, 0, 0), CHECK.check_directory(candidate, identity, reference))
                self.assertEqual((1, 0, 0), CHECK.check_directory(candidate, identity, reference, "b" * 40))
                for field, value in (("dirty", 1), ("dirty", False), ("revision", "short"),
                                     ("configuration", "Release"), ("trace", True), ("build_exe", False)):
                    with self.subTest(field=field, value=value):
                        damaged = copy.deepcopy(historical)
                        damaged[0][field] = value
                        write(damaged)
                        with mock.patch.object(CHECK.TRACES, "compare_records") as compare:
                            with self.assertRaises(CHECK.TRACES.TraceError):
                                CHECK.check_directory(candidate, identity, reference)
                            compare.assert_not_called()
                write(historical)
                with mock.patch.object(CHECK.TRACES, "compare_records") as compare:
                    with self.assertRaisesRegex(CHECK.TRACES.TraceError, "revision differs"):
                        CHECK.check_directory(candidate, identity, reference, "c" * 40)
                    compare.assert_not_called()
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "requires --reference"):
                    CHECK.check_directory(candidate, identity, reference_revision="b" * 40)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "full reference revision"):
                    CHECK.check_directory(candidate, identity, reference, "short")


if __name__ == "__main__":
    unittest.main()
