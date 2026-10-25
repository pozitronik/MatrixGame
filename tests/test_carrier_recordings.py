"""Synthetic protocol guards; physical delivery and collision behavior remain in the existing C++ cases."""
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
    "carrier_records", Path(__file__).parents[1] / "tools/check-carrier-recordings.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def profile(family="damage", rng="pm", step=10, variant=0):
    return dict(family=family, rng=rng, step=step, variant=variant)


def header(p):
    return dict(type="header", schema=1, fixture=CHECK.FIXTURES[p["family"]], family=p["family"],
                carrier_observation="carrier-journey-v1", variant=p["variant"], navigation_layout="open-v1",
                seed=81, rng="ParkMiller" if p["rng"] == "pm" else "LegacyCRT", step_ms=p["step"],
                max_frames=32000 // p["step"] if p["family"] == "approach" else 6000, initially_selected=False,
                audio="unavailable", viewport=[1280, 720],
                render="storage-device-unavailable-icons-no-draw-cursor-surrogate", revision="a" * 40,
                dirty=0, compiler="MinGW", compiler_version="synthetic", configuration="Debug",
                precision_control=131072, assert_off=False, build_exe=True, cheats=False, trace=False,
                delayed_damage="Corrected", group_counts="Corrected", place_containment="Corrected",
                zero_x_trace="Corrected")


def snapshot(p, frame=0):
    family = p["family"]
    pairs, groups = [], []
    for i in range(2 if family == "neighbors" else 1):
        place = i + 1 if family == "neighbors" else 3
        destination = [24 + place % 2 * 4, 24 + place // 2 * 4] if family == "neighbors" else [48, 48]
        cargo = dict(id=5 + 2 * i, present=True, alive=True, state=3, in_logic_list=False, must_die=False,
                     x_bits=bits(480), y_bits=bits(480), health_bits=bits(150), in_place=False,
                     place=place, place_add=[-1, -1], group=i,
                     group_state=dict(present=True, order=3, target=0, destination=destination),
                     queue=[[1, 0, bits(48), bits(48), 0, 0, 0]], height_sampled=False)
        carrier = dict(id=6 + 2 * i, present=True, in_active_list=True, health_bits=bits(1000),
                       carrying=True, cargo=cargo["id"], pose_sampled=False)
        progress = None if family == "damage" else dict(release=0 if family == "approach" else -1,
                   landed=0 if family == "approach" else -1, retired=0 if family == "approach" else -1,
                   falling_samples=-1 if family == "approach" else 0)
        pairs.append(dict(request_index=i, requested_place=place, destination=destination,
                          carrier=carrier, cargo=cargo, progress=progress))
        groups.append(dict(slot=i, stored_count=1, members=0, order=3, target=0))
    robots = []
    for i in range(1, 5):
        actor = dict(id=i, alive=family == "approach" or i in (1, 3))
        if actor["alive"]:
            actor.update(side=1 if i <= 2 else 2, state=2, x_bits=bits(100), y_bits=bits(200),
                         health_bits=bits(100), in_place=False, group=-1, order=-1, target=0,
                         target_is_null=True, destination=[-1, -1], queue=[])
        robots.append(actor)
    record = dict(type="snapshot", state=dict(frame=frame, time_ms=frame * p["step"], rng_draws=11,
                                              robots=robots, player_groups=groups),
                  active_carriers=len(pairs), rejected_icons=len(pairs), pairs=pairs)
    if family == "neighbors":
        record["resources"] = [300] * 4
    if family == "approach":
        maximum = 500 if p["variant"] else 20
        record["approach"] = dict(obstacle=9, minimum_bit_patterns=[bits(x) for x in (468, 318, 0)],
                                 maximum_bit_patterns=[bits(x) for x in (492, 342, maximum)],
                                 roof_bits=bits(maximum), maximum_z_bits=0, maximum_surface_bits=0,
                                 inside=0, first_inside=0, last_inside=0)
        if frame:
            carrier, cargo = pairs[0]["carrier"], pairs[0]["cargo"]
            carrier.update(pose_sampled=True, position_bit_patterns=[bits(x) for x in (480, 330, 100)])
            cargo.update(height_sampled=True, height_bits=bits(60))
            original = json.dumps(record["state"], separators=(",", ":")) + ":carrier=1,"
            original += ",".join(str(x) for x in carrier["position_bit_patterns"])
            original += ":cargo=" + ",".join(str(cargo[k]) for k in ("state", "x_bits", "y_bits", "height_bits"))
            record["original_observation"] = original
    return record


def refresh_original(record):
    if not record["state"]["frame"]:
        return
    carrier = record["pairs"][0]["carrier"]
    cargo = record["pairs"][0]["cargo"]
    text = json.dumps(record["state"], separators=(",", ":")) + ":carrier=" + str(int(carrier["present"]))
    if carrier["present"]:
        text += "," + ",".join(str(x) for x in carrier["position_bit_patterns"])
    text += ":cargo=" + ",".join(str(cargo[k]) for k in ("state", "x_bits", "y_bits", "height_bits"))
    record["original_observation"] = text


def damage_history(rng="pm"):
    """A complete serialization history; it deliberately supplies no movement/flight model."""
    p = profile(rng=rng)
    request = dict(type="command", name="request", frame=0, time_ms=0, rng_draws_before=10, rng_draws_after=11,
                   order=0, angle_bits=0, landing_x_bits=bits(480), landing_y_bits=bits(480), place=3,
                   destination=[48, 48])
    records = [header(p), request]
    for frame in range(2101):
        value = snapshot(p, frame)
        if frame > 100:
            value["state"]["rng_draws"] = 12
            value["active_carriers"] = 0
            value["pairs"][0]["carrier"] = dict(id=6, present=False)
            value["pairs"][0]["cargo"] = (dict(id=5, present=True, alive=False, state=5) if frame == 101
                                            else dict(id=5, present=False))
        records.append(value)
        if frame == 100:
            repair = dict(type="command", name="repair", frame=100, time_ms=1000, rng_draws_before=11,
                          rng_draws_after=11, target=6, weapon=57, attacker_side=2, reported_death=False,
                          origin_bit_patterns=[bits(x) for x in (480, 200, 100)],
                          direction_bit_patterns=[0, 0, bits(1)], health_before_bits=bits(1000),
                          health_after_bits=bits(1000))
            repaired = copy.deepcopy(value)
            repaired["pairs"][0]["carrier"].update(pose_sampled=True,
                                                    position_bit_patterns=list(repair["origin_bit_patterns"]))
            lethal = dict(repair, name="lethal-laser", rng_draws_after=12, weapon=98,
                          reported_death=True, cargo_marked=True)
            lethal.pop("health_after_bits")
            after = copy.deepcopy(value)
            after["state"]["rng_draws"] = 12
            after["active_carriers"] = 0
            after["pairs"][0]["carrier"] = dict(id=6, present=False)
            after["pairs"][0]["cargo"].update(state=4, must_die=True)
            records += [repair, repaired, lethal, after]
    records.append(dict(type="complete", frame=2100, time_ms=21000, rng_draws=12, observations=2103,
                        observation_digest_low=0, observation_digest_high=0))
    return records


class CarrierRecordingContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = profile()
        cls.valid = damage_history()
        cls.initial = cls.valid[2]

    def test_inventory_preserves_fifty_sessions_and_only_approach_replays(self):
        sizes = {family: 0 for family in CHECK.FIXTURES}
        pairs = 0
        for p, names in CHECK.inventory():
            sizes[p["family"]] += len(names)
            pairs += len(names) == 2
            if p["family"] == "damage":
                self.assertEqual(10, p["step"])
        self.assertEqual(dict(flights=16, neighbors=12, damage=6, approach=16), sizes)
        self.assertEqual(8, pairs)

    def test_complete_synthetic_damage_history_uses_real_reader_and_all_boundaries(self):
        parsed = CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in self.valid)
        self.assertEqual(2101, len(CHECK.validate(parsed, self.p)))
        for family in CHECK.FIXTURES:
            CHECK.snapshot(snapshot(profile(family=family)), profile(family=family))

    def test_compiler_versions_and_effective_header_types(self):
        for compiler, version in (("MSVC", 1944), ("MinGW", "13.2.0")):
            h = header(self.p)
            h.update(compiler=compiler, compiler_version=version)
            CHECK.header(h, self.p)
        for compiler, version in (("MSVC", True), ("MSVC", 1944.0), ("MSVC", "1944"), ("MSVC", 0),
                                  ("MSVC", -1), ("MSVC", 1 << 31), ("MinGW", 1944), ("MinGW", "")):
            h = header(self.p)
            h.update(compiler=compiler, compiler_version=version)
            with self.subTest(compiler=compiler, version=version), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.header(h, self.p)
        for field, value in (("seed", True), ("variant", False), ("trace", "false"), ("max_frames", 6000.0)):
            h = header(self.p)
            h[field] = value
            with self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.header(h, self.p)

    def test_correlated_scalar_pose_identity_and_queue_gaps_fail(self):
        edits = (lambda r: r["pairs"][0]["cargo"].pop("must_die"),
                 lambda r: r["pairs"][0]["cargo"].update(alive="true"),
                 lambda r: r["pairs"][0]["carrier"].update(id=True),
                 lambda r: r["pairs"][0]["carrier"].update(cargo=7),
                 lambda r: r["pairs"][0]["carrier"].update(position_bit_patterns=[0, 0, 0]),
                 lambda r: r["pairs"][0]["cargo"].update(height_bits=0),
                 lambda r: r["pairs"][0]["cargo"]["queue"][0].pop(4),
                 lambda r: r["pairs"][0]["cargo"]["group_state"].update(target=9),
                 lambda r: r["state"]["robots"][1].update(queue=[]),
                 lambda r: r["state"]["robots"][0].update(state=999),
                 lambda r: r.update(active_carriers=0))
        for edit in edits:
            left = copy.deepcopy(self.initial)
            edit(left)
            for value in (left, copy.deepcopy(left)):
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(value, self.p)

    def test_cargo_presence_is_qualified_by_core_not_logic_list(self):
        value = copy.deepcopy(self.initial)
        value["pairs"][0]["cargo"]["in_logic_list"] = False
        CHECK.snapshot(value, self.p)

    def test_order_domains_keep_uint32_param_bits_signed_param4_and_logical_nine(self):
        CHECK.queue([[8, 8, 0xffffffff, 0, 0xffffffff, -(1 << 31), -1]] * 5, 7, set(range(-1, 7)))
        for q in ([[9, 0, 0, 0, 0, 0, 0]], [[0, 9, 0, 0, 0, 0, 0]], [[0, 0, 0, 0, True, 0, 0]],
                  [[0, 0, 0, 0, 0, 1 << 31, 0]], [[0, 0, 0, 0, 0, 0, 9]], [[0] * 7] * 6):
            with self.subTest(queue=q), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.queue(q, 7, set(range(-1, 7)))
        value = copy.deepcopy(self.initial)
        value["state"]["robots"][0]["order"] = 9
        value["state"]["player_groups"][0]["order"] = 9
        value["pairs"][0]["cargo"]["group_state"]["order"] = 9
        CHECK.snapshot(value, self.p)
        value["pairs"][0]["cargo"]["group_state"]["order"] = 999
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.snapshot(value, self.p)

    def test_dead_and_retired_actors_have_no_live_layout_or_pose_fields(self):
        dead = next(r for r in self.valid if r.get("state", {}).get("frame") == 101)
        CHECK.snapshot(dead, self.p)
        value = copy.deepcopy(dead)
        value["pairs"][0]["cargo"]["queue"] = []
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "DIP"):
            CHECK.snapshot(value, self.p)
        retired = next(r for r in self.valid if r.get("state", {}).get("frame") == 102)
        for actor, field in (("cargo", "x_bits"), ("carrier", "health_bits")):
            value = copy.deepcopy(retired)
            value["pairs"][0][actor][field] = 0
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "retired"):
                CHECK.snapshot(value, self.p)

    def test_correlated_live_dip_claim_is_rejected_before_full_comparison(self):
        index = next(i for i, r in enumerate(self.valid) if r.get("name") == "lethal-laser") + 1
        value = copy.deepcopy(self.valid[index])
        value["pairs"][0]["cargo"]["state"] = 5
        for record in (value, copy.deepcopy(value)):
            malformed = self.valid[:index] + [record] + self.valid[index + 1:]
            with self.assertRaisesRegex(CHECK.TRACES.TraceError, "live cargo is DIP"):
                CHECK.validate(malformed, self.p)
        session = copy.deepcopy(self.initial)
        session["state"]["robots"][0]["state"] = 5
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "live Session actor is DIP"):
            CHECK.snapshot(session, self.p)

    def test_neighbor_distinct_request_ids_and_blocked_candidate_are_checked(self):
        p = profile("neighbors")
        value = snapshot(p)
        CHECK.snapshot(value, p)
        value["pairs"][1]["carrier"]["id"] = 6
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.snapshot(value, p)
        p = profile("neighbors", variant=1)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "blocked"):
            CHECK.snapshot(snapshot(profile("neighbors")), p)

    def test_original_approach_payload_and_surface_samples_are_required(self):
        p = profile("approach", variant=1)
        value = snapshot(p, 1)
        CHECK.snapshot(value, p)
        for edit in (lambda r: r.pop("original_observation"),
                     lambda r: r.update(original_observation=r["original_observation"] + "0"),
                     lambda r: r["approach"].pop("minimum_bit_patterns"),
                     lambda r: r["pairs"][0]["cargo"].update(height_sampled=False)):
            left = copy.deepcopy(value)
            edit(left)
            for record in (left, copy.deepcopy(left)):
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.snapshot(record, p)

    def test_missing_ticks_wrong_hit_time_and_terminal_data_fail(self):
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.validate(self.valid[:3] + self.valid[4:], self.p)
        for key, value in (("time_ms", 20990), ("rng_draws", 99), ("observations", 2102),
                           ("observation_digest_low", True)):
            altered = self.valid[:-1] + [dict(self.valid[-1], **{key: value})]
            with self.subTest(key=key), self.assertRaises(CHECK.TRACES.TraceError):
                CHECK.validate(altered, self.p)
        missing_hit = [r for r in self.valid if r.get("name") != "lethal-laser"]
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.validate(missing_hit, self.p)
        with self.assertRaises(CHECK.TRACES.TraceError):
            CHECK.TRACES.read_records(json.dumps(r) + "\n" for r in self.valid[:-1])

    def test_dip_cargo_cannot_reappear_as_live_before_retirement(self):
        index = next(i for i, r in enumerate(self.valid) if r.get("state", {}).get("frame") == 102)
        revived = snapshot(self.p, 102)
        revived["state"]["rng_draws"] = 12
        revived["active_carriers"] = 0
        revived["pairs"][0]["carrier"] = dict(id=6, present=False)
        malformed = self.valid[:index] + [revived] + self.valid[index + 1:]
        CHECK.snapshot(revived, self.p)
        with self.assertRaisesRegex(CHECK.TRACES.TraceError, "DIP cargo became live"):
            CHECK.validate(malformed, self.p)

    def test_low_high_controls_use_actual_samples_and_reject_correlated_bad_candidates(self):
        low = [snapshot(profile("approach")), snapshot(profile("approach"), 1)]
        high = [snapshot(profile("approach", variant=1)), snapshot(profile("approach", variant=1), 1)]
        low[1]["approach"]["maximum_surface_bits"] = bits(100)
        high[1]["approach"]["maximum_surface_bits"] = bits(400)
        high[1]["pairs"][0]["carrier"]["position_bit_patterns"][2] = bits(102)
        refresh_original(high[1])
        for r in low:
            CHECK.snapshot(r, profile("approach"))
        for r in high:
            CHECK.snapshot(r, profile("approach", variant=1))
        CHECK.approach_control(low, high)
        edits = (lambda r: r[1]["pairs"][0]["carrier"]["position_bit_patterns"].__setitem__(0, bits(481)),
                 lambda r: r[1]["state"].update(rng_draws=12),
                 lambda r: r[1]["pairs"][0]["progress"].update(release=10),
                 lambda r: r[1]["approach"].update(maximum_surface_bits=bits(100)),
                 lambda r: r[1]["pairs"][0]["carrier"]["position_bit_patterns"].__setitem__(2, bits(100)))
        for edit in edits:
            altered = copy.deepcopy(high)
            edit(altered)
            refresh_original(altered[1])
            for candidate in (altered, copy.deepcopy(altered)):
                for r in candidate:
                    CHECK.snapshot(r, profile("approach", variant=1))
                with self.assertRaises(CHECK.TRACES.TraceError):
                    CHECK.approach_control(low, candidate)

    def test_independent_clean_reference_and_one_consistent_unpinned_revision(self):
        profiles = [self.p, profile(rng="crt")]
        histories = [self.valid, damage_history("crt")]
        groups = [(p, next(names for expected, names in CHECK.inventory() if expected == p)) for p in profiles]
        identity = dict(revision="a" * 40, configuration="Debug", compiler="MinGW", target="EXE",
                        cheats=False, trace=False)
        with tempfile.TemporaryDirectory() as temporary:
            candidate, reference = Path(temporary) / "candidate", Path(temporary) / "reference"
            candidate.mkdir()
            reference.mkdir()
            def write(folder, revisions, edits=None):
                for i, ((_, names), history) in enumerate(zip(groups, histories)):
                    h = dict(history[0], revision=revisions[i])
                    h.update(edits or {})
                    text = "".join(json.dumps(r) + "\n" for r in [h, *history[1:]])
                    (folder / names[0]).write_text(text, encoding="utf-8")
            write(candidate, ["a" * 40] * 2)
            with mock.patch.object(CHECK, "inventory", return_value=groups):
                write(reference, ["b" * 40] * 2)
                self.assertEqual((2, 0, 0), CHECK.check_directory(candidate, identity, reference, "b" * 40))
                write(reference, ["b" * 40, "c" * 40])
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "reference profiles differ"):
                    CHECK.check_directory(candidate, identity, reference)
                for field, value in (("dirty", 1), ("dirty", False), ("revision", "short"),
                                     ("compiler_version", 1944), ("trace", True), ("build_exe", False)):
                    write(reference, ["b" * 40] * 2, {field: value})
                    with mock.patch.object(CHECK.TRACES, "compare_records") as compare:
                        with self.subTest(field=field), self.assertRaises(CHECK.TRACES.TraceError):
                            CHECK.check_directory(candidate, identity, reference)
                        compare.assert_not_called()
                write(reference, ["b" * 40] * 2)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "revision differs"):
                    CHECK.check_directory(candidate, identity, reference, "c" * 40)
                with self.assertRaisesRegex(CHECK.TRACES.TraceError, "requires --reference"):
                    CHECK.check_directory(candidate, identity, reference_revision="b" * 40)

    def test_cli_rejects_missing_inventory_and_invalid_source_identity(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch("sys.stderr", new=io.StringIO()):
            args = [directory, "--revision", "a" * 40, "--configuration", "Debug", "--compiler", "MinGW",
                    "--target", "EXE", "--cheats", "false", "--trace", "false"]
            self.assertEqual(1, CHECK.main(args))
            args[2] = "short"
            self.assertEqual(1, CHECK.main(args))


if __name__ == "__main__":
    unittest.main()
