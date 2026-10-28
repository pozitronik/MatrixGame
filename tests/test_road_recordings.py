"""Protocol guards for existing road scenes; synthetic histories do not model movement physics."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "road_recordings", Path(__file__).parents[1] / "tools/check-road-recordings.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def profile(variant="unguided", rng="pm", step=16):
    return dict(variant=variant, rng=rng, step=step)


def header(p):
    return dict(type="header", schema=1, fixture="road-ring-v1", road_observation="road-journey-v1",
                navigation_layout="road-ring-v1", seed=81, step_ms=p["step"], max_frames=40000 // p["step"],
                audio="unavailable", initially_selected=True, render="storage-device-no-draw-cursor-surrogate",
                viewport=[1280, 720], rng="ParkMiller" if p["rng"] == "pm" else "LegacyCRT", variant=p["variant"],
                journey_ms=40000, hit_ms=6000, settled_ms=35000, revision="a" * 40, dirty=0,
                compiler="MinGW", compiler_version="synthetic", configuration="Debug", precision_control=131072,
                assert_off=False, build_exe=True, cheats=False, trace=False, delayed_damage="Corrected",
                group_counts="Corrected", place_containment="Corrected", zero_x_trace="Corrected")


def original(record):
    blocker = record["blocker"]
    suffix = ""
    if blocker["configured"]:
        suffix = ":blocker=" + str(int(blocker["present"]))
        if blocker["present"]:
            suffix += "," + ",".join(str(blocker[key]) for key in ("state", "x_bits", "y_bits", "health_bits"))
    suffix += ":enemy_kills=" + str(record["enemy_kills"])
    record["original_observation"] = json.dumps(record["state"], separators=(",", ":")) + suffix


def recording(p):
    guided = p["variant"] != "unguided"
    blocked = p["variant"] in ("live-blocker", "destroyed-blocker")
    killed = p["variant"] == "destroyed-blocker"
    step = p["step"]
    crossed_at = 10000 if killed else 14000
    arrived_at = ((20000 if killed else 25000) + step - 1) // step * step
    zones = [0, 0]
    roads = []
    if guided:
        units = [dict(zone=0, has_road=False, road_zones=[])]
        units += [dict(zone=z, has_road=True, road_zones=[z, (z + 1) % 16]) for z in (15, 14, 13, 12)]
        roads = [dict(distance=50, units=units)]
    group = dict(order=1, destination=[48, 26], roads=roads)

    def command(name, time):
        value = dict(type="command", name=name, frame=time // step, time_ms=time,
                     rng_draws_before=100, rng_draws_after=101 if name == "lethal-laser-hit" else 100)
        if name == "ground-move":
            value.update(point=[500, 280])
        elif name == "regional-guidance":
            value.update(regions=[0, 1], chassis_mask=2)
        else:
            value.update(weapon=98, attacker_side=2, reported_death=True,
                         origin_bit_patterns=[bits(320), bits(540), bits(20)],
                         direction_bit_patterns=[bits(1), 0, 0])
        return value

    def snapshot(frame, after_hit=False):
        time = frame * step
        dead = killed and (time > 6000 or after_hit)
        retired = killed and time >= 8000
        robots, actors = [], []
        for index in range(4):
            moving = index < 2
            x = (100 if time < 6000 else 250 if time < crossed_at else 500) if moving else (540, 600)[index - 2]
            y = (360 + index * 40 if time < 6000 else 540 if time < arrived_at else 280 - index * 40)
            if not moving:
                y = 50
            physical = [[1, 2, bits(500), bits(280), 0, 0, 0]] if moving and time < arrived_at else []
            zone = ((0, 15, 14, 13, 12) if guided else (0, 3, 4, 5, 6))[min(time // 3000, 4)] if moving else 5
            robots.append(dict(id=index + 1, alive=True, x_bits=bits(x), y_bits=bits(y), health_bits=bits(100),
                               side=1 if moving else 2, state=2, group=0 if moving else -1,
                               order=1 if moving else -1, target=0, target_is_null=True,
                               in_place=bool(moving and time >= arrived_at),
                               destination=[48, 26] if moving else [-1, -1],
                               queue=[[*q[:4], q[-1]] for q in physical]))
            actors.append(dict(id=index + 1, map=[8, 8], zone=zone, region=int(5 <= zone <= 12), place=-1,
                               place_add=[48, 26], queue=physical))
            if moving and frame and not after_hit:
                zones[index] |= 1 << zone
        blocker = dict(configured=blocked, present=blocked and not retired)
        if blocker["present"]:
            blocker.update(id=5, same_created_object=True, alive=not dead, state=5 if dead else 2,
                           x_bits=bits(320), y_bits=bits(540), health_bits=bits(0 if dead else 10))
            if not dead:
                blocker["queue"] = []
        state = dict(frame=frame, time_ms=time, rng_draws=101 if dead else 100, robots=robots,
                     player_groups=[dict(slot=0, stored_count=2, members=2, order=1, target=0)])
        progress = dict(zones=list(zones), crossed=[crossed_at if time >= crossed_at else 0] * 2,
                        arrived=[arrived_at if time >= arrived_at else 0] * 2,
                        minimum_separation_bits=bits(50 if blocked and frame else 10000),
                        retired=8000 if retired else 0)
        value = dict(type="snapshot", state=state, source_observation=not after_hit, group=group,
                     actors=actors, blocker=blocker, enemy_kills=int(dead), player_kills=0, progress=progress)
        original(value)
        return value

    records = [header(p), command("ground-move", 0)]
    if guided:
        records.append(command("regional-guidance", 0))
    for frame in range(40000 // step + 1):
        records.append(snapshot(frame))
        if killed and frame * step == 6000:
            records += [command("lethal-laser-hit", 6000), snapshot(frame, True)]
    records.append(dict(type="complete", frame=40000 // step, time_ms=40000,
                        rng_draws=101 if killed else 100, original_frames=40000 // step + 1,
                        original_digest_low=0, original_digest_high=0))
    return records


class RoadRecordingContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = profile()
        cls.valid = recording(cls.p)
        cls.observation = cls.valid[3]

    def test_finite_inventory_has_thirty_two_episodes_and_no_added_sessions(self):
        groups = list(CHECK.inventory())
        self.assertEqual(16, len(groups))
        self.assertEqual(32, sum(len(names) for _, names in groups))
        self.assertEqual(4, sum(p["variant"] == "destroyed-blocker" for p, _ in groups))

    def test_complete_protocol_profiles_and_original_prefix(self):
        for p in (profile(), profile("guided", "crt", 10), profile("live-blocker"), profile("destroyed-blocker")):
            with self.subTest(profile=p):
                records = recording(p)
                parsed = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in records)
                self.assertEqual(40000 // p["step"] + 1, len(CHECK.validate(parsed, p)))
        live = CHECK.validate(recording(profile("live-blocker")), profile("live-blocker"))
        killed = CHECK.validate(recording(profile("destroyed-blocker")), profile("destroyed-blocker"))
        self.assertEqual(live[:376], killed[:376])

    def test_effective_compiler_types_and_header_fields(self):
        for compiler, version in (("MinGW", "13.2.0"), ("MSVC", 1944)):
            value = header(self.p)
            value.update(compiler=compiler, compiler_version=version)
            CHECK.header(value, self.p)
        for compiler, version in (("MSVC", "1944"), ("MSVC", True), ("MSVC", 1944.0), ("MSVC", 0),
                                  ("MSVC", -1), ("MSVC", 1 << 31), ("MinGW", 1944), ("MinGW", "")):
            value = header(self.p)
            value.update(compiler=compiler, compiler_version=version)
            with self.subTest(compiler=compiler, version=version), self.assertRaisesRegex(
                    CHECK.TRACES.TraceError, "compiler_version"):
                CHECK.header(value, self.p)
        for key, value in (("seed", True), ("journey_ms", 39999), ("trace", "false"), ("viewport", [1280.0, 720])):
            value_header = header(self.p)
            value_header[key] = value
            with self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.header(value_header, self.p)

    def test_correlated_missing_or_illtyped_scene_fields_fail(self):
        edits = (lambda r: r.pop("source_observation"), lambda r: r.pop("original_observation"),
                 lambda r: r["actors"][0].pop("place"), lambda r: r["actors"][0].update(zone=True),
                 lambda r: r["actors"][0].update(region=2), lambda r: r["progress"].pop("arrived"),
                 lambda r: r["actors"][0].update(region=1 - r["actors"][0]["region"]),
                 lambda r: r["blocker"].update(present=True), lambda r: r["state"]["robots"][0].update(alive=1),
                 lambda r: r["state"]["robots"][1].update(group=1),
                 lambda r: r["state"]["player_groups"][0].update(order=999))
        for edit in edits:
            left = copy.deepcopy(self.observation)
            edit(left)
            for record in (left, copy.deepcopy(left)):
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(record, self.p)

    def test_full_order_domains_reject_correlated_missing_param3_or_bad_param4(self):
        CHECK.queue([[8, 8, 0xffffffff, 0, 0xffffffff, -(1 << 31), -1]] * 5, 7, 5)
        for value in ([[1, 2, 0, 0, 0, 0]], [[9, 2, 0, 0, 0, 0, 0]], [[1, 9, 0, 0, 0, 0, 0]],
                      [[1, 2, True, 0, 0, 0, 0]], [[1, 2, 0, 0, 0, 1 << 31, 0]],
                      [[1, 2, 0, 0, 0, 0, 6]], [[1, 2, 0, 0, 0, 0, 0]] * 6):
            for queue in (value, copy.deepcopy(value)):
                with self.subTest(queue=queue), self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.queue(queue, 7, 5)

    def test_full_queue_must_project_to_existing_session_queue(self):
        value = copy.deepcopy(self.observation)
        value["actors"][0]["queue"][0][2] ^= 1
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "disagrees"):
            CHECK.snapshot(value, self.p)

    def test_logical_auto_defence_nine_is_valid_for_uncontrolled_sentinels(self):
        value = copy.deepcopy(self.observation)
        value["state"]["robots"][2].update(order=9, target=-1, target_is_null=False)
        original(value)
        CHECK.snapshot(value, self.p)
        value["state"]["robots"][2]["order"] = 999
        original(value)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "order/target"):
            CHECK.snapshot(value, self.p)

    def test_road_junctions_and_original_suffix_are_checked_independently(self):
        p = profile("guided")
        value = recording(p)[3]
        for edit in (lambda r: r["group"]["roads"][0]["units"][1].update(zone=14),
                     lambda r: r["group"]["roads"][0].update(distance=49),
                     lambda r: r["group"]["roads"][0]["units"][1].update(road_zones=[2, 3]),
                     lambda r: r.update(original_observation=r["original_observation"] + "0")):
            left = copy.deepcopy(value)
            edit(left)
            for record in (left, copy.deepcopy(left)):
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(record, p)

    def test_blocker_dead_and_retired_fields_follow_original_observer(self):
        p = profile("destroyed-blocker")
        values = recording(p)
        dead = next(r for r in values if r.get("source_observation") is False)
        CHECK.snapshot(dead, p)
        bad = copy.deepcopy(dead)
        bad["blocker"]["queue"] = []
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "dead blocker"):
            CHECK.snapshot(bad, p)
        retired = next(r for r in values if r.get("progress", {}).get("retired"))
        CHECK.snapshot(retired, p)
        bad = copy.deepcopy(retired)
        bad["blocker"]["health_bits"] = 0
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "absent blocker"):
            CHECK.snapshot(bad, p)

    def test_command_schedule_outcome_and_rng_boundaries_fail_closed(self):
        p = profile("destroyed-blocker")
        good = next(r for r in recording(p) if r.get("name") == "lethal-laser-hit")
        CHECK.command(good, "lethal-laser-hit", 375, 16, 100)
        for key, value in (("frame", 374), ("time_ms", 5999), ("rng_draws_before", 99),
                           ("weapon", 99), ("reported_death", 1), ("origin_bit_patterns", [0, 0]),
                           ("direction_bit_patterns", [0, 0, 0])):
            bad = copy.deepcopy(good)
            bad[key] = value
            with self.subTest(key=key), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.command(bad, "lethal-laser-hit", 375, 16, 100)

    def test_missing_original_frame_and_wrong_terminal_evidence_fail(self):
        missing = self.valid[:3] + self.valid[4:]
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.validate(missing, self.p)
        for key, value in (("time_ms", 39984), ("rng_draws", 999), ("original_frames", 2500),
                           ("original_digest_low", True)):
            bad = self.valid[:-1] + [dict(self.valid[-1], **{key: value})]
            with self.subTest(key=key), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate(bad, self.p)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in self.valid[:-1])

    def test_clean_reference_and_candidate_identities_are_independent(self):
        identity = dict(revision="a" * 40, compiler="MinGW", configuration="Debug", target="EXE",
                        cheats=False, trace=False)
        names = next(names for p, names in CHECK.inventory() if p == self.p)
        with tempfile.TemporaryDirectory() as temporary:
            candidate, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            candidate.mkdir()
            reference.mkdir()
            def write(folder, value_header):
                text = "".join(json.dumps(r) + "\n" for r in [value_header, *self.valid[1:]])
                for name in names:
                    (folder / name).write_text(text, encoding="utf-8")
            write(candidate, self.valid[0])
            historical = dict(self.valid[0], revision="b" * 40)
            with mock.patch.object(CHECK, "inventory", return_value=[(self.p, names)]):
                write(reference, historical)
                self.assertEqual((2, 1, 0), CHECK.check_directory(candidate, identity, reference, "b" * 40))
                mixed = dict(historical, revision="c" * 40)
                (reference / names[1]).write_text(
                    "".join(json.dumps(r) + "\n" for r in [mixed, *self.valid[1:]]), encoding="utf-8")
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "reference execution profiles differ"):
                    CHECK.check_directory(candidate, identity, reference)
                for key, value in (("dirty", 1), ("dirty", False), ("revision", "short"),
                                   ("configuration", "Release"), ("trace", True), ("build_exe", False)):
                    write(reference, dict(historical, **{key: value}))
                    with mock.patch.object(CHECK.TRACES, "compare_records") as compare:
                        with self.subTest(key=key), self.assertRaises(CHECK.TRACES.TraceError):
                            CHECK.check_directory(candidate, identity, reference)
                        compare.assert_not_called()
                write(reference, historical)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "revision differs"):
                    CHECK.check_directory(candidate, identity, reference, "c" * 40)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "requires --reference"):
                    CHECK.check_directory(candidate, identity, reference_revision="b" * 40)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "full reference"):
                    CHECK.check_directory(candidate, identity, reference, "short")

    def test_cli_rejects_incomplete_inventory_and_bad_revision(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch("sys.stderr", new=io.StringIO()):
            args = [directory, "--revision", "a" * 40, "--configuration", "Debug", "--compiler", "MinGW",
                    "--target", "EXE", "--cheats", "false", "--trace", "false"]
            self.assertEqual(1, CHECK.main(args))
            args[2] = "short"
            self.assertEqual(1, CHECK.main(args))


if __name__ == "__main__":
    unittest.main()
