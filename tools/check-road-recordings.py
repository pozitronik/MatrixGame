#!/usr/bin/env python3
"""Check the existing regional-road and destroyed-blocker recordings without selecting a reference."""
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
VARIANTS = ("unguided", "guided", "live-blocker", "destroyed-blocker")
BUILD_FIELDS = ("revision", "dirty", "configuration", "compiler", "compiler_version", "precision_control",
                "assert_off", "build_exe", "cheats", "trace", "delayed_damage", "group_counts",
                "place_containment", "zero_x_trace", "render", "audio", "viewport")


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


def equal(left, right, message):
    require(TRACES._first_difference(left, right) is None, message)


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def clean_source(header, expected=None):
    require(full_revision(header.get("revision")), "require full compiled revision")
    require(type(header.get("dirty")) is int and header["dirty"] == 0, "dirty compiled source")
    if expected is not None:
        require(full_revision(expected) and header["revision"] == expected, "compiled revision differs")


def inventory():
    for rng in ("pm", "crt"):
        for step in (10, 16):
            for variant in VARIANTS:
                profile = dict(rng=rng, step=step, variant=variant)
                yield profile, tuple(f"road-journey-{rng}-{step}-{variant}-{replay}.jsonl" for replay in (0, 1))


def header(record, profile, identity=None):
    expected = dict(fixture="road-ring-v1", road_observation="road-journey-v1",
                    navigation_layout="road-ring-v1", seed=81, step_ms=profile["step"],
                    max_frames=40000 // profile["step"], audio="unavailable", initially_selected=True,
                    render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720],
                    rng="ParkMiller" if profile["rng"] == "pm" else "LegacyCRT", variant=profile["variant"],
                    journey_ms=40000, hit_ms=6000, settled_ms=35000)
    if identity:
        expected.update({key: value for key, value in identity.items() if key != "target"})
        expected["build_exe"] = identity["target"] == "EXE"
        clean_source(record, identity.get("revision"))
    for key, value in expected.items():
        equal(record.get(key), value, "header." + key + " differs")
    require(set(BUILD_FIELDS) <= record.keys(), "missing effective build field")
    for key in ("revision", "configuration"):
        require(type(record[key]) is str and bool(record[key]), "invalid header." + key)
    require(record["compiler"] in ("MSVC", "MinGW"), "invalid header.compiler")
    version = record["compiler_version"]
    require(integer(version, 1) if record["compiler"] == "MSVC" else type(version) is str and bool(version),
            "invalid header.compiler_version")
    require(integer(record["precision_control"], 0) and integer(record["dirty"], 0, 1),
            "invalid precision/source scalar")
    for key in ("assert_off", "build_exe", "cheats", "trace"):
        require(type(record[key]) is bool, "invalid header." + key)
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(record[key] in ("Original", "Corrected"), "invalid compatibility profile")


def queue(value, width, maximum_id):
    require(type(value) is list and len(value) <= 5, "physical queue requires at most five orders")
    for order in value:
        require(type(order) is list and len(order) == width and all(integer(x, -(1 << 31), 0xffffffff)
                for x in order), "incomplete/noninteger physical order")
        require(integer(order[0], 0, 8) and integer(order[1], 0, 8), "invalid physical order type/phase")
        require(all(bits(x) for x in order[2:4 if width == 5 else 5]), "invalid physical parameter bits")
        require(integer(order[-1], -1, maximum_id), "unnormalized physical order object")
        if width == 7:
            require(integer(order[5]), "invalid signed Param4")


def snapshot(record, profile):
    state = record["state"]
    require(type(record.get("source_observation")) is bool, "missing source observation marker")
    robots = state["robots"]
    require([r.get("id") for r in robots] == [1, 2, 3, 4] and all(integer(r.get("id")) for r in robots),
            "Session actor inventory differs")
    live_fields = {"id", "alive", "x_bits", "y_bits", "health_bits", "side", "state", "group", "order", "target",
                   "target_is_null", "in_place", "destination", "queue"}
    for robot in robots:
        require(live_fields <= robot.keys() and robot["alive"] is True, "missing live Session actor")
        require(all(bits(robot[key]) and math.isfinite(real(robot[key]))
                    for key in ("x_bits", "y_bits", "health_bits")), "invalid Session float bits")
        require(all(integer(robot[key]) for key in ("side", "state", "group")), "invalid Session scalar")
        require(integer(robot["order"], -1, 9) and integer(robot["target"], -1, 4), "invalid Session order/target")
        require(type(robot["in_place"]) is bool and type(robot["target_is_null"]) is bool
                and point(robot["destination"]), "invalid Session arrival/destination")
        queue(robot["queue"], 5, 4)
    groups = state.get("player_groups")
    require(type(groups) is list, "missing Session player groups")
    for group in groups:
        require(type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys()
                and all(integer(group[key]) for key in ("slot", "stored_count", "members"))
                and integer(group["order"], 0, 9) and integer(group["target"], -1, 4), "invalid Session group")
    moving_group = robots[0]["group"]
    require(integer(moving_group, 0, 60) and robots[1]["group"] == moving_group,
            "moving actors lost shared group membership")
    owned_group = [g for g in groups if g["slot"] == moving_group]
    require(len(owned_group) == 1 and owned_group[0]["order"] == 1, "Session group lost Move intent")
    group = record.get("group")
    require(type(group) is dict and integer(group.get("order"), 0, 9), "missing road group order")
    equal(group["order"], 1, "road group lost Move intent")
    equal(group.get("destination"), [48, 26], "road group destination differs")
    roads = group.get("roads")
    guided = profile["variant"] != "unguided"
    require(type(roads) is list and len(roads) == int(guided), "regional guidance list differs")
    if guided:
        route = roads[0]
        require(type(route) is dict and integer(route.get("distance")) and route["distance"] == 50,
                "regional route distance differs")
        units = route.get("units")
        require(type(units) is list and len(units) == 5, "incomplete regional route")
        for index, (unit, zone) in enumerate(zip(units, (0, 15, 14, 13, 12))):
            require(type(unit) is dict and integer(unit.get("zone")) and unit["zone"] == zone
                    and type(unit.get("has_road")) is bool and unit["has_road"] == bool(index),
                    "regional route junction differs")
            road_zones = unit.get("road_zones")
            require(type(road_zones) is list and all(integer(z, 0, 15) for z in road_zones),
                    "invalid regional road zones")
            require((not road_zones) if index == 0 else len(road_zones) == 2
                    and set(road_zones) == {zone, (zone + 1) % 16}, "regional road connection differs")
    actors = record.get("actors")
    require(type(actors) is list and all(type(r) is dict for r in actors)
            and [r.get("id") for r in actors] == [1, 2, 3, 4]
            and all(integer(r.get("id")) for r in actors), "road actor inventory differs")
    for actor, robot in zip(actors, robots):
        require(point(actor.get("map")) and all(0 <= c < 64 for c in actor["map"])
                and integer(actor.get("zone"), 0, 15) and integer(actor.get("region"), 0, 1),
                "invalid actual movement zone/region")
        equal(actor["region"], int(5 <= actor["zone"] <= 12), "recorded region disagrees with zone")
        require(integer(actor.get("place")) and point(actor.get("place_add")), "missing allocated place")
        queue(actor.get("queue"), 7, 5)
        projected = [[*q[:4], q[-1] if q[-1] <= 4 else -1] for q in actor["queue"]]
        equal(projected, robot["queue"], "full physical queue disagrees with Session")
    blocked = profile["variant"] in ("live-blocker", "destroyed-blocker")
    blocker = record.get("blocker")
    require(type(blocker) is dict and type(blocker.get("configured")) is bool
            and blocker["configured"] == blocked and type(blocker.get("present")) is bool,
            "missing blocker core/lifetime")
    require(blocked or not blocker["present"], "unconfigured blocker is present")
    if blocker["present"]:
        require(integer(blocker.get("id")) and blocker["id"] == 5 and blocker.get("same_created_object") is True
                and type(blocker.get("alive")) is bool and integer(blocker.get("state")),
                "invalid blocker identity/state")
        require(all(bits(blocker.get(key)) and math.isfinite(real(blocker[key]))
                    for key in ("x_bits", "y_bits", "health_bits")), "missing blocker position/health")
        if blocker["alive"]:
            queue(blocker.get("queue"), 7, 5)
        else:
            require("queue" not in blocker, "dead blocker retains a live queue")
    else:
        require(not {"id", "alive", "state", "queue", "x_bits", "y_bits", "health_bits", "same_created_object"}
                & blocker.keys(), "absent blocker retains object fields")
    require(integer(record.get("enemy_kills"), 0) and integer(record.get("player_kills"), 0),
            "missing kill credit")
    original = record.get("original_observation")
    require(type(original) is str, "missing original observation bytes")
    boundary = original.find(":blocker=") if blocked else original.find(":enemy_kills=")
    require(boundary > 0, "original observation suffix differs")
    equal(json.loads(original[:boundary], object_pairs_hook=TRACES._object,
                     parse_constant=TRACES._constant, parse_float=TRACES._float), state,
          "original Session payload differs")
    suffix = ""
    if blocked:
        suffix = ":blocker=" + str(int(blocker["present"]))
        if blocker["present"]:
            suffix += "," + ",".join(str(blocker[key]) for key in ("state", "x_bits", "y_bits", "health_bits"))
    suffix += ":enemy_kills=" + str(record["enemy_kills"])
    equal(original[boundary:], suffix, "original blocker/kill payload differs")
    progress = record.get("progress")
    require(type(progress) is dict, "missing journey progress")
    for key in ("zones", "crossed", "arrived"):
        require(type(progress.get(key)) is list and len(progress[key]) == 2
                and all(integer(v, 0, 65535 if key == "zones" else 40000) for v in progress[key]),
                "invalid journey progress " + key)
    require(bits(progress.get("minimum_separation_bits"))
            and math.isfinite(real(progress["minimum_separation_bits"]))
            and real(progress["minimum_separation_bits"]) >= 0 and integer(progress.get("retired"), 0, 40000),
            "invalid blocker separation/retirement")


def command(record, name, frame, step, previous_draws=None):
    equal(record.get("type"), "command", "missing command boundary")
    equal(record.get("name"), name, "command identity differs")
    equal(record.get("frame"), frame, "command frame differs")
    equal(record.get("time_ms"), frame * step, "command time differs")
    require(integer(record.get("rng_draws_before"), 0, (1 << 64) - 1)
            and integer(record.get("rng_draws_after"), record["rng_draws_before"], (1 << 64) - 1),
            "invalid command RNG boundary")
    if previous_draws is not None:
        equal(record["rng_draws_before"], previous_draws, "command RNG input differs")
    if name == "ground-move":
        equal(record.get("point"), [500, 280], "ground command point differs")
    elif name == "regional-guidance":
        equal(record.get("regions"), [0, 1], "regional command regions differ")
        equal(record.get("chassis_mask"), 2, "regional command chassis differs")
    else:
        equal(record.get("weapon"), 98, "hit weapon differs")
        equal(record.get("attacker_side"), 2, "hit attribution differs")
        require(record.get("reported_death") is True, "hit did not report death")
        require(type(record.get("origin_bit_patterns")) is list and len(record["origin_bit_patterns"]) == 3
                and all(bits(x) and math.isfinite(real(x)) for x in record["origin_bit_patterns"]),
                "invalid hit origin")
        equal(record.get("direction_bit_patterns"), [1065353216, 0, 0], "hit direction differs")
    return record["rng_draws_after"]


def validate(records, profile, identity=None):
    header(records[0], profile, identity)
    step = profile["step"]
    variant = profile["variant"]
    blocked = variant in ("live-blocker", "destroyed-blocker")
    kill = variant == "destroyed-blocker"
    index = 1
    draws = command(records[index], "ground-move", 0, step)
    index += 1
    if variant != "unguided":
        draws = command(records[index], "regional-guidance", 0, step, draws)
        index += 1
    zones, crossed, arrived = [0, 0], [0, 0], [0, 0]
    retired = 0
    previous_separation = 10000.0
    originals = []
    for frame in range(40000 // step + 1):
        record = records[index]
        require(record.get("type") == "snapshot" and record.get("source_observation") is True,
                "missing complete original observation")
        snapshot(record, profile)
        state = record["state"]
        equal(state["frame"], frame, "original frame schedule differs")
        equal(state["time_ms"], frame * step, "original time schedule differs")
        if not frame:
            equal(state["rng_draws"], draws, "initial command RNG output differs")
        time = frame * step
        blocker = record["blocker"]
        if blocked:
            require(not retired or not blocker["present"], "retired blocker reappeared")
            if not kill or time <= 6000:
                require(blocker["present"] and blocker["alive"], "blocker died before intervention")
            elif blocker["present"]:
                require(not blocker["alive"] and blocker["state"] == 5, "lethal blocker did not remain DIP")
            elif not retired:
                retired = time
        equal(record["enemy_kills"], int(kill and time > 6000), "enemy kill credit differs")
        equal(record["player_kills"], 0, "unexpected player kill credit")
        for j, (robot, actor) in enumerate(zip(state["robots"][:2], record["actors"][:2])):
            x, y = real(robot["x_bits"]), real(robot["y_bits"])
            require(20 <= x <= 620 and 20 <= y <= 620, "moving actor left bounded map")
            equal(robot["destination"], [48, 26], "moving actor lost command destination")
            if frame:
                zones[j] |= 1 << actor["zone"]
                if not crossed[j] and x > 360:
                    crossed[j] = time
                if not arrived[j] and robot["in_place"] and x == 500 and y == 280 - j * 40:
                    arrived[j] = time
            if time >= 35000:
                require(robot["in_place"] and x == 500 and y == 280 - j * 40 and not actor["queue"],
                        "moving actor did not settle with an empty queue")
        for robot, x in zip(state["robots"][2:], (540, 600)):
            require(real(robot["x_bits"]) == x and real(robot["y_bits"]) == 50, "enemy sentinel moved")
        progress = record["progress"]
        for key, value in (("zones", zones), ("crossed", crossed), ("arrived", arrived), ("retired", retired)):
            equal(progress[key], value, "measured progress differs: " + key)
        separation = real(progress["minimum_separation_bits"])
        require(separation <= previous_separation, "minimum separation increased")
        if not blocked:
            equal(separation, 10000.0, "unblocked separation differs")
        if blocked and time == 6000:
            require(separation < 60 and 120 < real(state["robots"][0]["x_bits"]) < 320
                    and not state["robots"][0]["in_place"], "blocker approach milestone missing")
        previous_separation = separation
        originals.append(record)
        index += 1
        if kill and time == 6000:
            draws = command(records[index], "lethal-laser-hit", frame, step, state["rng_draws"])
            index += 1
            after = records[index]
            snapshot(after, profile)
            require(after["source_observation"] is False, "missing immediate post-hit observation")
            equal(after["state"]["frame"], frame, "post-hit frame differs")
            equal(after["state"]["time_ms"], time, "post-hit time differs")
            equal(after["state"]["rng_draws"], draws, "post-hit RNG output differs")
            require(after["blocker"]["present"] and not after["blocker"]["alive"]
                    and after["blocker"]["state"] == 5 and after["enemy_kills"] == 1,
                    "lethal hit outcome missing")
            equal(after["progress"], progress, "post-hit changed measured journey history")
            index += 1
    require(index == len(records) - 1 and records[index]["type"] == "complete", "unexpected tail/schedule")
    complete = records[index]
    equal(complete.get("frame"), 40000 // step, "completion frame differs")
    equal(complete.get("time_ms"), 40000, "completion time differs")
    equal(complete.get("rng_draws"), originals[-1]["state"]["rng_draws"], "terminal RNG differs")
    equal(complete.get("original_frames"), 40000 // step + 1, "original vector count differs")
    require(bits(complete.get("original_digest_low")) and bits(complete.get("original_digest_high")),
            "missing original vector digest")
    required, forbidden = (0x0039, 0x7000) if variant == "unguided" else (0x7001, 0x0038)
    require(all(z & required == required and not z & forbidden for z in zones), "wrong visited corridor")
    require(all(0 < t < 35000 for t in arrived), "arrival milestone missing")
    if kill:
        require(6000 < retired < 40000 and all(t > 6000 for t in crossed), "death/progress lifecycle missing")
    return originals


def read(path):
    with path.open(encoding="utf-8") as stream:
        return TRACES.read_records(stream, str(path))


def check_directory(directory, identity, reference=None, reference_revision=None):
    require(reference_revision is None or reference is not None, "--reference-revision requires --reference")
    require(reference_revision is None or full_revision(reference_revision), "require full reference revision")
    groups = list(inventory())
    names = {name for _, paths in groups for name in paths}
    equal({path.name for path in directory.glob("road-journey-*.jsonl")}, names, "road inventory differs")
    if reference is not None:
        equal({path.name for path in reference.glob("road-journey-*.jsonl")}, names, "reference inventory differs")
    signature = None
    reference_signature = None
    control = None
    prefixes = 0
    for profile, paths in groups:
        episodes, observations = [], []
        for name in paths:
            try:
                records = read(directory / name)
                observations.append(validate(records, profile, identity))
                current = {key: records[0][key] for key in BUILD_FIELDS}
                if signature is not None:
                    equal(current, signature, "road execution profiles differ")
                signature = current
                if reference is not None:
                    old = read(reference / name)
                    clean_source(old[0], reference_revision)
                    historical_identity = {k: v for k, v in identity.items() if k != "revision"}
                    validate(old, profile, historical_identity)
                    current_reference = {key: old[0][key] for key in BUILD_FIELDS}
                    if reference_signature is not None:
                        equal(current_reference, reference_signature, "reference execution profiles differ")
                    reference_signature = current_reference
                    require(TRACES.compare_records(old, records) is None, "fresh/reference divergence: " + name)
                episodes.append(records)
            except (ValueError, KeyError, TypeError, IndexError) as error:
                raise TRACES.TraceError(f"{name}: {error}") from error
        require(TRACES.compare_records(*episodes) is None, "same-profile replay divergence: " + paths[0])
        if profile["variant"] in ("unguided", "live-blocker"):
            control = observations[0]
        elif profile["variant"] == "guided":
            require(control != observations[0], "guided and unguided journeys did not differ")
            require(control[-1]["progress"]["zones"] != observations[0][-1]["progress"]["zones"],
                    "guided and unguided corridors did not differ")
        else:
            end = 6000 // profile["step"] + 1
            equal(control[:end], observations[0][:end], "live/destroyed pre-hit prefix differs")
            prefixes += 1
            require(control != observations[0], "lethal intervention did not distinguish journeys")
            for key in ("crossed", "arrived"):
                require(all(killed < alive for killed, alive in
                            zip(observations[0][-1]["progress"][key], control[-1]["progress"][key])),
                        "destroyed blocker did not permit earlier " + key)
    return len(names), len(groups), prefixes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--reference-revision")
    parser.add_argument("--revision", required=True)
    parser.add_argument("--configuration", required=True, choices=("Debug", "Release"))
    parser.add_argument("--compiler", required=True, choices=("MinGW", "MSVC"))
    parser.add_argument("--target", required=True, choices=("EXE", "DLL"))
    parser.add_argument("--cheats", required=True, choices=("true", "false"))
    parser.add_argument("--trace", required=True, choices=("true", "false"))
    args = parser.parse_args(argv)
    identity = {key: getattr(args, key) for key in ("revision", "configuration", "compiler", "target")}
    identity.update(cheats=args.cheats == "true", trace=args.trace == "true")
    try:
        require(full_revision(args.revision), "require full source revision")
        count, pairs, prefixes = check_directory(args.directory, identity, args.reference, args.reference_revision)
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, IndexError) as error:
        print("Road recording validation failed: " + str(error), file=sys.stderr)
        return 1
    print(f"Validated {count} road recordings, {pairs} replay pairs and {prefixes} pre-hit prefixes; "
          "no reference acceptance implied.")
    if args.reference is not None and args.reference_revision is None:
        print("Reference revision is unpinned; this comparison is diagnostic.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
