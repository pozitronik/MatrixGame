#!/usr/bin/env python3
"""Validate the fifty existing physical carrier episodes, without choosing a historical reference."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys

SPEC = importlib.util.spec_from_file_location("battle_traces", Path(__file__).with_name("compare-battle-traces.py"))
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)
FIXTURES = dict(flights="synthetic-navigation-delivery-v1", neighbors="neighboring-delivery-landings-v1",
                damage="synthetic-navigation-carrier-damage-v1", approach="carrier-approach-obstacle-v1")
BUILD = ("revision", "dirty", "compiler", "compiler_version", "configuration", "precision_control", "assert_off",
         "build_exe", "cheats", "trace", "delayed_damage", "group_counts", "place_containment", "zero_x_trace")
FLIGHT_TIMES = (
    ((3420, 3740, 6690), (5610, 6280, 10650), (4760, 5140, 8740), (6110, 6810, 11610)),
    ((3424, 3744, 6704), (5600, 6272, 10656), (4768, 5152, 8752), (6112, 6800, 11616)),
    ((5410, 5730, 10600), (7760, 8420, 14860), (6500, 6880, 12150), (8120, 8820, 15560)),
    ((5408, 5728, 10608), (7760, 8432, 14864), (6496, 6880, 12160), (8128, 8832, 15552)))
NEIGHBOR_LANDINGS = (
    ((6520, 4170), (6500, 4170), (6470, 4090)),
    ((6528, 4176), (6512, 4176), (6480, 4096)),
    ((6150, 4670), (6130, 4670), (6160, 4700)),
    ((6160, 4672), (6128, 4672), (6160, 4704)))
ANGLE_BITS = (0, 0x3fc90fdb, 0x40490fdb, 0x4096cbe4)


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=-(1 << 31), maximum=(1 << 31) - 1):
    return type(value) is int and minimum <= value <= maximum


def bits(value):
    return integer(value, 0, 0xffffffff)


def real(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(item) for item in value)


def bit_vector(value):
    return type(value) is list and len(value) == 3 and all(bits(x) and math.isfinite(real(x)) for x in value)


def equal(left, right, message):
    require(TRACES._first_difference(left, right) is None, message)


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def clean_source(record, revision=None):
    require(full_revision(record.get("revision")), "require full compiled revision")
    require(type(record.get("dirty")) is int and record["dirty"] == 0, "dirty compiled source")
    if revision is not None:
        require(full_revision(revision) and record["revision"] == revision, "compiled revision differs")


def inventory():
    for family in FIXTURES:
        for rng in ("pm", "crt"):
            for step in ((10,) if family == "damage" else (10, 16)):
                for variant in range(4 if family == "flights" else 2 if family == "approach" else 3):
                    p = dict(family=family, rng=rng, step=step, variant=variant)
                    yield p, tuple(f"carrier-{family}-{rng}-{step}-{variant}-{i}.jsonl"
                                   for i in ((0, 1) if family == "approach" else (0,)))


def header(record, p, identity=None):
    expected = dict(fixture=FIXTURES[p["family"]], carrier_observation="carrier-journey-v1",
                    family=p["family"], variant=p["variant"], navigation_layout="open-v1", seed=81,
                    rng="ParkMiller" if p["rng"] == "pm" else "LegacyCRT", step_ms=p["step"],
                    max_frames=32000 // p["step"] if p["family"] == "approach" else 6000,
                    initially_selected=False, audio="unavailable", viewport=[1280, 720],
                    render="storage-device-unavailable-icons-no-draw-cursor-surrogate")
    if identity:
        expected.update({k: v for k, v in identity.items() if k != "target"})
        expected["build_exe"] = identity["target"] == "EXE"
        clean_source(record, identity.get("revision"))
    for key, value in expected.items():
        equal(record.get(key), value, "header." + key + " differs")
    require(set(BUILD) <= record.keys(), "missing effective build profile")
    require(record["compiler"] in ("MSVC", "MinGW"), "invalid compiler")
    version = record["compiler_version"]
    require(integer(version, 1) if record["compiler"] == "MSVC" else type(version) is str and bool(version),
            "invalid compiler_version")
    require(all(type(record[k]) is str and bool(record[k]) for k in ("revision", "configuration")),
            "invalid source/configuration text")
    require(integer(record["dirty"], 0, 1) and integer(record["precision_control"], 0), "invalid source/precision")
    require(all(type(record[k]) is bool for k in ("assert_off", "build_exe", "cheats", "trace")),
            "invalid build flags")
    require(all(record[k] in ("Original", "Corrected") for k in
                ("delayed_damage", "group_counts", "place_containment", "zero_x_trace")), "invalid compatibility")


def queue(value, width, identities):
    require(type(value) is list and len(value) <= 5, "physical queue exceeds five orders or is missing")
    for order in value:
        require(type(order) is list and len(order) == width, "incomplete physical order")
        require(integer(order[0], 0, 8) and integer(order[1], 0, 8), "invalid physical type/phase")
        require(all(bits(x) for x in order[2:4 if width == 5 else 5]), "invalid physical parameter bits")
        require(integer(order[-1]) and order[-1] in identities, "unnormalized physical object")
        if width == 7:
            require(integer(order[5]), "invalid signed Param4")


def session_state(state):
    robots = state["robots"]
    require([r.get("id") for r in robots] == [1, 2, 3, 4] and all(integer(r.get("id")) for r in robots),
            "Session actor inventory differs")
    fields = {"id", "alive", "side", "state", "x_bits", "y_bits", "health_bits", "in_place", "group", "order",
              "target", "target_is_null", "destination", "queue"}
    for robot in robots:
        require(type(robot.get("alive")) is bool, "missing Session liveness")
        if not robot["alive"]:
            equal(set(robot), {"id", "alive"}, "retired Session actor has live fields")
            continue
        require(fields <= robot.keys(), "missing live Session field")
        require(all(bits(robot[k]) for k in ("x_bits", "y_bits", "health_bits")), "invalid Session bits")
        require(all(integer(robot[k]) for k in ("side", "group")) and integer(robot["state"], 0, 7)
                and integer(robot["order"], -1, 9)
                and integer(robot["target"], -1, 4), "invalid Session order/scalar")
        require(robot["state"] != 5, "live Session actor is DIP")
        require(type(robot["target_is_null"]) is bool and type(robot["in_place"]) is bool
                and point(robot["destination"]), "invalid Session destination/arrival")
        queue(robot["queue"], 5, set(range(-1, 5)))
    groups = state.get("player_groups")
    require(type(groups) is list, "missing Session groups")
    for group in groups:
        require(type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys()
                and all(integer(group[k]) for k in ("slot", "stored_count", "members"))
                and integer(group["order"], 0, 9) and integer(group["target"], -1, 4), "invalid Session group")


def snapshot(record, p):
    require(record.get("type") == "snapshot", "missing snapshot")
    state = record["state"]
    session_state(state)
    family = p["family"]
    count = 2 if family == "neighbors" else 1
    identities = set(range(-1, 5 + count * 2)) | ({9} if family == "approach" else set())
    pairs = record.get("pairs")
    require(type(pairs) is list and len(pairs) == count, "delivery pair inventory differs")
    require(integer(record.get("active_carriers"), 0, 16) and integer(record.get("rejected_icons"), 0),
            "missing carrier/icon observations")
    equal(record["rejected_icons"], count, "unavailable icon count differs")
    for index, pair in enumerate(pairs):
        require(type(pair) is dict and integer(pair.get("request_index")) and pair["request_index"] == index,
                "request ordering differs")
        require(integer(pair.get("requested_place"), 1, 3) and point(pair.get("destination")),
                "missing requested place/destination")
        if family == "neighbors":
            place = pair["requested_place"]
            equal(pair["destination"], [24 + place % 2 * 4, 24 + place // 2 * 4], "neighbor origin differs")
            require(p["variant"] == 0 or place != 1, "blocked landing candidate selected")
        else:
            equal(pair["requested_place"], 3, "direct request place differs")
            equal(pair["destination"], [48, 48], "direct request destination differs")
        carrier, cargo = pair.get("carrier"), pair.get("cargo")
        require(type(carrier) is dict and integer(carrier.get("id")) and carrier["id"] == 6 + 2 * index
                and type(carrier.get("present")) is bool, "missing carrier identity/core")
        if carrier["present"]:
            require({"in_active_list", "health_bits", "carrying", "cargo", "pose_sampled"} <= carrier.keys()
                    and carrier["in_active_list"] is True and bits(carrier["health_bits"])
                    and type(carrier["carrying"]) is bool and type(carrier["pose_sampled"]) is bool
                    and integer(carrier["cargo"]), "invalid live carrier fields")
            equal(carrier["cargo"], 5 + 2 * index if carrier["carrying"] else 0, "carrier/cargo link differs")
            if carrier["pose_sampled"]:
                require(bit_vector(carrier.get("position_bit_patterns")), "missing sampled carrier pose")
            else:
                require("position_bit_patterns" not in carrier, "unsampled carrier pose was invented")
        else:
            equal(set(carrier), {"id", "present"}, "retired carrier has object fields")
        require(type(cargo) is dict and integer(cargo.get("id")) and cargo["id"] == 5 + 2 * index
                and type(cargo.get("present")) is bool, "missing cargo identity/core")
        if not cargo["present"]:
            equal(set(cargo), {"id", "present"}, "retired cargo has object fields")
        else:
            require(type(cargo.get("alive")) is bool and integer(cargo.get("state"), 0, 7),
                    "missing cargo liveness/state")
            if not cargo["alive"]:
                equal(cargo["state"], 5, "non-live cargo state differs")
                equal(set(cargo), {"id", "present", "alive", "state"}, "DIP cargo has live-layout fields")
            else:
                require(cargo["state"] != 5, "live cargo is DIP")
                fields = {"in_logic_list", "must_die", "x_bits", "y_bits", "health_bits", "in_place", "place",
                          "place_add", "group", "group_state", "queue", "height_sampled"}
                require(fields <= cargo.keys(), "missing live cargo field")
                require(all(type(cargo[k]) is bool for k in
                            ("in_logic_list", "must_die", "in_place", "height_sampled")), "invalid cargo flags")
                require(all(bits(cargo[k]) for k in ("x_bits", "y_bits", "health_bits")), "invalid cargo bits")
                require(integer(cargo["place"]) and point(cargo["place_add"])
                        and integer(cargo["group"], -1, 60), "invalid cargo place/group")
                group = cargo["group_state"]
                require(type(group) is dict and type(group.get("present")) is bool
                        and group["present"] == (cargo["group"] >= 0), "invalid cargo group presence")
                if group["present"]:
                    require(integer(group.get("order"), 0, 9) and integer(group.get("target"))
                            and group["target"] in identities and point(group.get("destination")),
                            "invalid cargo logical order/target")
                    shared = [g for g in state["player_groups"] if g["slot"] == cargo["group"]]
                    require(len(shared) == 1, "cargo group absent from Session")
                    equal(shared[0]["order"], group["order"], "Session/cargo group orders disagree")
                    equal(shared[0]["target"], group["target"] if group["target"] <= 4 else -1,
                          "Session/cargo group targets disagree")
                queue(cargo["queue"], 7, identities)
                if cargo["height_sampled"]:
                    require(bits(cargo.get("height_bits")) and math.isfinite(real(cargo["height_bits"])),
                            "missing sampled cargo height")
                else:
                    require("height_bits" not in cargo, "unsampled cargo height was invented")
        progress = pair.get("progress", "missing")
        if family == "damage":
            require(progress is None, "damage scene invents driver progress")
        else:
            require(type(progress) is dict and {"release", "landed", "retired", "falling_samples"} <= progress.keys()
                    and all(integer(progress[k], -1, 32000) for k in
                            ("release", "landed", "retired", "falling_samples")), "missing source lifecycle counters")
    equal(record["active_carriers"], sum(pair["carrier"]["present"] for pair in pairs),
          "active list/core observations disagree")
    if family == "neighbors":
        require(type(record.get("resources")) is list and len(record["resources"]) == 4
                and all(integer(x) for x in record["resources"]), "missing resource sample")
    if family == "approach":
        a = record.get("approach")
        require(type(a) is dict and integer(a.get("obstacle")) and a["obstacle"] == 9
                and bit_vector(a.get("minimum_bit_patterns")) and bit_vector(a.get("maximum_bit_patterns")),
                "missing traced obstacle bounds")
        require(all(bits(a.get(k)) and math.isfinite(real(a[k]))
                    for k in ("roof_bits", "maximum_z_bits", "maximum_surface_bits"))
                and all(integer(a.get(k), 0, 32000) for k in ("inside", "first_inside", "last_inside")),
                "invalid sampled approach metrics")
        carrier, cargo = pairs[0]["carrier"], pairs[0]["cargo"]
        require(cargo["present"] and cargo["alive"], "approach cargo died")
        if state["frame"]:
            original = record.get("original_observation")
            require(type(original) is str, "missing original approach observation")
            split = original.find(":carrier=")
            require(split > 0, "original approach suffix missing")
            equal(json.loads(original[:split], object_pairs_hook=TRACES._object,
                             parse_constant=TRACES._constant, parse_float=TRACES._float), state,
                  "original Session payload differs")
            suffix = ":carrier=" + str(int(carrier["present"]))
            if carrier["present"]:
                require(carrier["pose_sampled"], "original carrier sample missing")
                suffix += "," + ",".join(str(x) for x in carrier["position_bit_patterns"])
            require(cargo["height_sampled"], "original cargo sample missing")
            suffix += ":cargo=" + ",".join(str(cargo[k]) for k in ("state", "x_bits", "y_bits", "height_bits"))
            equal(original[split:], suffix, "original carrier/cargo payload differs")
        else:
            require("original_observation" not in record, "invented original zero-frame observation")


def command(record, name, frame, step, previous=None):
    equal(record.get("type"), "command", "command missing")
    equal(record.get("name"), name, "command identity differs")
    equal(record.get("frame"), frame, "command frame differs")
    equal(record.get("time_ms"), frame * step, "command time differs")
    require(integer(record.get("rng_draws_before"), 0, (1 << 64) - 1)
            and integer(record.get("rng_draws_after"), record["rng_draws_before"], (1 << 64) - 1),
            "invalid command RNG")
    if previous is not None:
        equal(record["rng_draws_before"], previous, "command RNG input differs")
    return record["rng_draws_after"]


def validate(records, p, identity=None):
    header(records[0], p, identity)
    family, variant, step = p["family"], p["variant"], p["step"]
    row = (2 if p["rng"] == "crt" else 0) + (step == 16)
    index = 1
    resources = places = None
    first_command = records[index]
    if family == "neighbors":
        draws = command(first_command, "maintenance", 0, step)
        require(draws == first_command["rng_draws_before"] + 5, "maintenance RNG cost differs")
        places = first_command.get("places")
        require(point(places) and set(places) == ({1, 2} if variant == 0 else {2, 3}),
                "reserved/blocked landing selection differs")
        resources = first_command.get("resources_before")
        require(type(resources) is list and len(resources) == 4 and all(integer(x) for x in resources),
                "missing maintenance resources")
        equal(first_command.get("resources_after"), resources, "maintenance changed resources")
        equal(first_command.get("cooldown_ms"), 1000, "maintenance cooldown differs")
        index += 1
        retry = records[index]
        after_retry = command(retry, "maintenance-retry", 0, step, draws)
        equal(after_retry, draws, "cooldown retry consumed RNG")
        equal(retry.get("places"), places, "cooldown retry changed requested deliveries")
    else:
        draws = command(first_command, "request", 0, step)
        equal(first_command.get("order"), 0, "request order differs")
        equal(first_command.get("angle_bits"), ANGLE_BITS[variant] if family == "flights" else 0,
              "request approach direction differs")
        equal(first_command.get("landing_x_bits"), 1139802112, "request landing X differs")
        equal(first_command.get("landing_y_bits"), 1139802112, "request landing Y differs")
        equal(first_command.get("place"), 3, "request place differs")
        equal(first_command.get("destination"), [48, 48], "request destination differs")
    index += 1
    count = 2 if family == "neighbors" else 1
    release, landed, retired, falling = [-1] * count, [-1] * count, [-1] * count, [0] * count
    released_xy, previous_height = [None] * count, [None] * count
    cargo_retired, carrier_retired = [False] * count, [False] * count
    cargo_dead = [False] * count
    approach_bounds = initial_health = None
    observations, normals = [], []
    approach_max_z = approach_max_surface = 0.0
    inside = first_inside = last_inside = 0
    hit_time = None
    end_time = 32000 if family == "approach" else 30000
    frame = 0
    while True:
        record = records[index]
        snapshot(record, p)
        state = record["state"]
        equal(state["frame"], frame, "map frame schedule differs")
        equal(state["time_ms"], frame * step, "map time schedule differs")
        if not frame:
            equal(state["rng_draws"], draws, "request RNG output differs")
        time = frame * step
        for j, pair in enumerate(record["pairs"]):
            carrier, cargo = pair["carrier"], pair["cargo"]
            require(not carrier_retired[j] or not carrier["present"], "retired carrier reappeared")
            require(not cargo_retired[j] or not cargo["present"], "retired cargo reappeared")
            require(not cargo_dead[j] or not cargo["present"] or not cargo["alive"], "DIP cargo became live again")
            carrier_retired[j] |= not carrier["present"]
            cargo_retired[j] |= not cargo["present"]
            cargo_dead[j] |= cargo["present"] and not cargo["alive"]
            if not frame:
                require(carrier["present"] and carrier["carrying"] and cargo["present"] and cargo["alive"]
                        and cargo["state"] == 3, "initial requested delivery was not carrying its cargo")
                equal(cargo["group_state"].get("order"), 3, "initial inherited Attack order missing")
                equal(cargo["group_state"].get("destination"), pair["destination"],
                      "initial inherited destination differs")
            if family != "damage":
                require(cargo["present"] and cargo["alive"] and not cargo["must_die"], "healthy delivery lost cargo")
                require(cargo["height_sampled"] == bool(frame), "driver cargo sampling schedule differs")
                require(all(math.isfinite(real(cargo[k])) for k in ("x_bits", "y_bits")), "nonfinite cargo position")
                if carrier["present"]:
                    expected_pose = bool(frame) or family == "flights"
                    equal(carrier["pose_sampled"], expected_pose, "driver carrier sampling schedule differs")
                if frame:
                    if release[j] < 0 and cargo["state"] == 4:
                        release[j] = time
                        released_xy[j] = (cargo["x_bits"], cargo["y_bits"])
                    if release[j] >= 0 and cargo["state"] == 4:
                        falling[j] += 1
                        equal((cargo["x_bits"], cargo["y_bits"]), released_xy[j], "falling cargo moved horizontally")
                        height = real(cargo["height_bits"])
                        require(previous_height[j] is None or height <= previous_height[j], "falling height increased")
                        previous_height[j] = height
                    if landed[j] < 0 and release[j] >= 0 and cargo["state"] == 2:
                        landed[j] = time
                    if retired[j] < 0 and not carrier["present"]:
                        retired[j] = time
                if cargo["state"] == 3:
                    require(carrier["present"] and carrier["carrying"], "carried cargo lost its carrier")
                elif carrier["present"]:
                    require(not carrier["carrying"], "released cargo remains attached")
                expected = dict(release=release[j], landed=landed[j], retired=retired[j], falling_samples=falling[j])
                if family == "approach":
                    expected = {k: max(0, v) for k, v in expected.items()}
                    expected["falling_samples"] = -1
                equal(pair["progress"], expected, "source lifecycle counters disagree with observed states")
                if family in ("neighbors", "approach"):
                    equal(cargo["group_state"].get("order"), 3, "inherited Attack order lost")
                    equal(cargo["group_state"].get("destination"), pair["destination"], "inherited target cell changed")
            else:
                if carrier["present"]:
                    require(carrier["pose_sampled"] is False, "damage path invented an ordinary pose sample")
                if cargo["present"] and cargo["alive"]:
                    require(cargo["height_sampled"] is False, "damage path invented a height sample")
        if family == "neighbors":
            equal(record["resources"], resources, "delivery changed resources")
            equal([pair["requested_place"] for pair in record["pairs"]], places, "request ordering changed")
            for robot, x, y in ((state["robots"][0], 100, 200), (state["robots"][2], 540, 100)):
                require(robot["alive"] and real(robot["x_bits"]) == x and real(robot["y_bits"]) == y,
                        "neighbor sentinel moved")
        if family == "approach":
            a, pair = record["approach"], record["pairs"][0]
            carrier, cargo = pair["carrier"], pair["cargo"]
            bounds = {k: a[k] for k in ("minimum_bit_patterns", "maximum_bit_patterns", "roof_bits")}
            if approach_bounds is None:
                approach_bounds = bounds
                initial_health = cargo["health_bits"]
                require(real(initial_health) > 0, "approach cargo initial health is invalid")
            equal(bounds, approach_bounds, "fixed approach obstacle changed")
            equal(cargo["health_bits"], initial_health, "healthy approach cargo took damage")
            if carrier["present"]:
                equal(real(carrier["health_bits"]), 1000.0, "approach carrier health differs")
            for j, robot in enumerate(state["robots"], 1):
                require(robot["alive"] and real(robot["x_bits"]) == 100
                        and real(robot["y_bits"]) == (50 + j * 50 if j <= 2 else 400 + j * 50),
                        "approach sentinel moved")
            if frame and carrier["present"]:
                point_sample = [real(x) for x in carrier["position_bit_patterns"]]
                minimum, maximum = ([real(x) for x in a[k]] for k in ("minimum_bit_patterns", "maximum_bit_patterns"))
                approach_max_z = max(approach_max_z, point_sample[2])
                require(bits(a.get("sampled_surface_bits")) and math.isfinite(real(a["sampled_surface_bits"])),
                        "missing original interpolated surface sample")
                approach_max_surface = max(approach_max_surface, real(a["sampled_surface_bits"]))
                penetrating = all(low < value < high for low, value, high in zip(minimum, point_sample, maximum))
                if penetrating:
                    inside += 1
                    if not first_inside:
                        first_inside = time
                    last_inside = time
                    require(carrier["carrying"] and bits(a.get("hit_roof_bits"))
                            and real(a["hit_roof_bits"]) > point_sample[2], "penetration trace evidence missing")
                else:
                    require("hit_roof_bits" not in a, "invented penetration ray sample")
            else:
                require("sampled_surface_bits" not in a and "hit_roof_bits" not in a,
                        "invented unobserved approach surface")
            for key, value in (("maximum_z_bits", approach_max_z), ("maximum_surface_bits", approach_max_surface)):
                equal(real(a[key]), value, "recorded maximum differs from supplied samples")
            for key, value in (("inside", inside), ("first_inside", first_inside), ("last_inside", last_inside)):
                equal(a[key], value, "recorded penetration history differs")
            if time >= 30000:
                require(cargo["state"] == 2 and cargo["in_place"] and real(cargo["x_bits"]) == 500
                        and real(cargo["y_bits"]) == 500 and real(cargo["height_bits"]) == 0
                        and not cargo["queue"], "approach final standing state differs")
        observations.append(record)
        normals.append(record)
        index += 1
        if family == "damage" and hit_time is None:
            cargo = record["pairs"][0]["cargo"]
            ready = time >= 1000 if variant == 0 else cargo["state"] != 3 if variant == 1 else cargo["state"] == 2
            if ready:
                require(time <= 10000 and cargo["state"] == (3 if variant == 0 else 4 if variant == 1 else 2),
                        "damage lifecycle stage differs")
                hit_time, end_time = time, time + 20000
                repair = records[index]
                draws = command(repair, "repair", frame, step, state["rng_draws"])
                equal(draws, repair["rng_draws_before"], "repair consumed RNG")
                post_repair = records[index + 1]
                hit = records[index + 2]
                final_draws = command(hit, "lethal-laser", frame, step, draws)
                post_hit = records[index + 3]
                for value in (repair, hit):
                    equal(value.get("target"), 6, "damage target differs")
                    equal(value.get("attacker_side"), 2, "damage attribution differs")
                    require(bit_vector(value.get("origin_bit_patterns")), "missing sampled hit origin")
                    equal(value.get("direction_bit_patterns"), [0, 0, 1065353216], "damage direction differs")
                    require(bits(value.get("health_before_bits")) and real(value["health_before_bits"]) == 1000,
                            "carrier pre-hit health differs")
                equal(repair.get("weapon"), 57, "repair weapon differs")
                require(repair.get("reported_death") is False, "repair reported death")
                equal(record["pairs"][0]["carrier"].get("health_bits"), repair["health_before_bits"],
                      "pre-repair health sample differs")
                equal(repair.get("health_after_bits"), repair["health_before_bits"], "repair changed health")
                equal(hit.get("weapon"), 98, "lethal weapon differs")
                require(hit.get("reported_death") is True, "laser did not report death")
                equal(hit.get("cargo_marked"), variant == 0, "attached-cargo death flag differs")
                equal(hit["origin_bit_patterns"], repair["origin_bit_patterns"], "damage calls used different origins")
                for extra, extra_draws in ((post_repair, draws), (post_hit, final_draws)):
                    snapshot(extra, p)
                    equal(extra["state"]["frame"], frame, "post-damage frame differs")
                    equal(extra["state"]["time_ms"], time, "post-damage time differs")
                    equal(extra["state"]["rng_draws"], extra_draws, "post-damage RNG differs")
                    extra_cargo = extra["pairs"][0]["cargo"]
                    if extra_cargo["present"] and extra_cargo["alive"]:
                        require(extra_cargo["height_sampled"] is False, "damage invented a post-hit height")
                    observations.append(extra)
                repaired = post_repair["pairs"][0]["carrier"]
                require(repaired["present"] and repaired["pose_sampled"], "original hit pose sample missing")
                equal(repaired["position_bit_patterns"], repair["origin_bit_patterns"], "sampled hit point differs")
                equal(repaired["health_bits"], repair["health_after_bits"], "post-repair health differs")
                dead_carrier, after_cargo = (post_hit["pairs"][0][key] for key in ("carrier", "cargo"))
                require(not dead_carrier["present"] and after_cargo["present"] and after_cargo["alive"],
                        "lethal carrier/cargo ownership outcome differs")
                equal(after_cargo["must_die"], hit["cargo_marked"], "observed cargo death flag differs")
                carrier_retired[0] = True
                index += 4
        if time == end_time:
            break
        require(time < end_time, "simulation exceeded original duration")
        frame += 1
    require(index == len(records) - 1, "unexpected records after original duration")
    tail = records[index]
    equal(tail.get("type"), "complete", "completion missing")
    equal(tail.get("frame"), frame, "terminal frame differs")
    equal(tail.get("time_ms"), end_time, "terminal time differs")
    equal(tail.get("rng_draws"), observations[-1]["state"]["rng_draws"], "terminal RNG differs")
    equal(tail.get("observations"), len(observations), "complete observation count differs")
    require(bits(tail.get("observation_digest_low")) and bits(tail.get("observation_digest_high")),
            "diagnostic observation digest missing")
    last = normals[-1]
    if family == "damage":
        require(hit_time is not None, "damage intervention never occurred")
        cargo = last["pairs"][0]["cargo"]
        if variant == 0:
            require(not cargo["present"], "attached cargo did not retire")
        else:
            require(cargo["present"] and cargo["alive"] and cargo["state"] == 2 and not cargo["must_die"]
                    and cargo["in_place"] and real(cargo["x_bits"]) == 500 and real(cargo["y_bits"]) == 500,
                    "released cargo did not survive/arrive")
    else:
        for j, pair in enumerate(last["pairs"]):
            require(0 < release[j] < landed[j] < retired[j] < end_time, "delivery lifecycle ordering differs")
            cargo = pair["cargo"]
            require(cargo["state"] == 2 and cargo["in_place"], "delivered cargo did not settle")
            equal([real(cargo[k]) for k in ("x_bits", "y_bits")],
                  [(c + 2) * 10.0 for c in pair["destination"]], "delivered final position differs")
            if family != "approach":
                require(falling[j] > 1, "falling phase missing")
        if family == "flights":
            equal([release[0], landed[0], retired[0]], list(FLIGHT_TIMES[row][variant]),
                  "original flight timing table differs")
        elif family == "neighbors":
            equal(landed, list(NEIGHBOR_LANDINGS[row][variant]), "original neighbor landing table differs")
            poses = tail.get("standing_pose_bit_patterns")
            require(type(poses) is list and len(poses) == 2 and all(bit_vector(pose) for pose in poses),
                    "missing original final standing poses")
            require(bits(tail.get("separation_bits")) and 40 <= real(tail["separation_bits"]) <= 57,
                    "original sampled separation differs")
            for pose, pair in zip(poses, last["pairs"]):
                cargo = pair["cargo"]
                equal(pose[2], cargo["height_bits"], "final sampled standing height changed")
                require(abs(real(pose[0]) - real(cargo["x_bits"])) < 1
                        and abs(real(pose[1]) - real(cargo["y_bits"])) < 1, "standing cache bound differs")
        else:
            require(retired[0] < 30000, "approach carrier retired late")
            if variant:
                require(inside >= 10 and first_inside > 0 and last_inside - first_inside >= 200
                        and last_inside < release[0], "original tall-building penetration disappeared")
            else:
                equal(inside, 0, "low building unexpectedly encloses the carrier")
    return normals


def approach_control(low, high):
    def flight(records):
        return [r for r in records if r["state"]["frame"] and r["pairs"][0]["carrier"]["present"]]
    a, b = flight(low), flight(high)
    equal([r["pairs"][0]["carrier"]["position_bit_patterns"][:2] for r in a],
          [r["pairs"][0]["carrier"]["position_bit_patterns"][:2] for r in b], "low/high horizontal path differs")
    equal([r["state"]["rng_draws"] for r in low[1:]], [r["state"]["rng_draws"] for r in high[1:]],
          "low/high RNG history differs")
    equal(low[-1]["pairs"][0]["progress"], high[-1]["pairs"][0]["progress"], "low/high lifecycle differs")
    low_surface = real(low[-1]["approach"]["maximum_surface_bits"])
    high_surface = real(high[-1]["approach"]["maximum_surface_bits"])
    require(high_surface > low_surface + 200, "sampled tall surface response missing")
    response = False
    for left, right in zip(a, b):
        x, y, z = (real(v) for v in left["pairs"][0]["carrier"]["position_bit_patterns"])
        high_z = real(right["pairs"][0]["carrier"]["position_bit_patterns"][2])
        require(high_z >= z, "high flight fell below low flight")
        if 468 < x < 492 and 318 < y < 342 and high_z > z + 1:
            response = True
    require(response, "original height-response comparison missing")
    require(low != high, "low/high complete traces did not differ")


def read(path):
    with path.open(encoding="utf-8") as stream:
        return TRACES.read_records(stream, str(path))


def check_directory(directory, identity=None, reference=None, reference_revision=None):
    require(reference_revision is None or reference is not None, "--reference-revision requires --reference")
    require(reference_revision is None or full_revision(reference_revision), "require full reference revision")
    groups = list(inventory())
    names = {name for _, paths in groups for name in paths}
    equal({path.name for path in directory.glob("carrier-*.jsonl")}, names, "carrier inventory differs")
    if reference is not None:
        equal({path.name for path in reference.glob("carrier-*.jsonl")}, names, "reference inventory differs")
    signature = historical_signature = low = None
    pairs = controls = 0
    for p, paths in groups:
        episodes, normals = [], []
        for name in paths:
            try:
                values = read(directory / name)
                normals.append(validate(values, p, identity))
                current = {k: values[0][k] for k in BUILD}
                if signature is not None:
                    equal(current, signature, "candidate profiles differ")
                signature = current
                if reference is not None:
                    old = read(reference / name)
                    clean_source(old[0], reference_revision)
                    reference_identity = {k: v for k, v in identity.items() if k != "revision"} if identity else None
                    validate(old, p, reference_identity)
                    historical = {k: old[0][k] for k in BUILD}
                    if historical_signature is not None:
                        equal(historical, historical_signature, "reference profiles differ")
                    historical_signature = historical
                    require(TRACES.compare_records(old, values) is None, "fresh/reference divergence")
                episodes.append(values)
            except (ValueError, KeyError, TypeError, IndexError) as error:
                raise TRACES.TraceError(f"{name}: {error}") from error
        if len(episodes) == 2:
            require(TRACES.compare_records(*episodes) is None, paths[0] + ": replay diverged")
            pairs += 1
        if p["family"] == "approach":
            if p["variant"] == 0:
                low = normals[0]
            else:
                approach_control(low, normals[0])
                controls += 1
    return len(names), pairs, controls


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--reference-revision")
    parser.add_argument("--configuration", required=True, choices=("Debug", "Release"))
    parser.add_argument("--compiler", required=True, choices=("MinGW", "MSVC"))
    parser.add_argument("--target", required=True, choices=("EXE", "DLL"))
    parser.add_argument("--cheats", required=True, choices=("true", "false"))
    parser.add_argument("--trace", required=True, choices=("true", "false"))
    args = parser.parse_args(argv)
    identity = {k: getattr(args, k) for k in ("revision", "configuration", "compiler", "target")}
    identity.update(cheats=args.cheats == "true", trace=args.trace == "true")
    try:
        require(full_revision(args.revision), "require full source revision")
        count, pairs, controls = check_directory(args.directory, identity, args.reference, args.reference_revision)
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, IndexError) as error:
        print("Carrier recording validation failed: " + str(error), file=sys.stderr)
        return 1
    print(f"Validated {count} carrier files, {pairs} original replay pairs and {controls} low/high controls; "
          "no reference acceptance implied.")
    if args.reference is not None and args.reference_revision is None:
        print("Reference revision is unpinned; comparison is diagnostic.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
