"""Validate the twelve existing competing-repair episodes before exact comparison."""
import argparse
import collections
import importlib.util
from pathlib import Path
import re
import struct
import sys

SPEC = importlib.util.spec_from_file_location(
    "battle_traces", Path(__file__).parents[1] / "tools" / "compare-battle-traces.py")
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=-(2**31), maximum=2**31 - 1):
    return type(value) is int and minimum <= value <= maximum


def bits(value):
    return integer(value, 0, 0xffffffff)


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(x) for x in value)


def queue(value, width, maximum_id):
    require(type(value) is list and len(value) <= 5, "invalid physical queue capacity/type")
    for order in value:
        require(type(order) is list and len(order) == width, "invalid physical queue shape")
        require(all(integer(x, 0, 8) for x in order[:2]), "invalid physical order type/phase")
        require(all(bits(x) for x in order[2:width - 2 if width == 7 else 4]), "invalid order float bits")
        if width == 7:
            require(integer(order[5]), "invalid signed Param4")
        require(integer(order[-1], -1, maximum_id), "unnormalized physical order object")


def live_actor(actor, session=False):
    fields = {"id", "state", "x_bits", "y_bits", "health_bits", "group", "in_place", "order", "destination", "queue"}
    if session:
        fields |= {"alive", "side", "target", "target_is_null"}
    else:
        fields |= {"present", "alive", "place"}
    require(fields <= actor.keys(), "missing live actor field")
    require(all(bits(actor[key]) for key in ("x_bits", "y_bits", "health_bits")), "invalid actor float bits")
    require(integer(actor["state"], 0, 7) and integer(actor["group"], -1, 60), "invalid actor state/group")
    require(integer(actor["order"], -1, 9) and point(actor["destination"]), "invalid player intention")
    require(type(actor["in_place"]) is bool, "invalid arrival flag")
    queue(actor["queue"], 5 if session else 7, 4 if session else 6)
    if session:
        require(integer(actor["side"]) and integer(actor["target"], -1, 4), "invalid Session target/side")
        require(type(actor["target_is_null"]) is bool, "invalid target-null flag")
        require(actor["target_is_null"] is (actor["target"] == 0), "target-null disagreement")
    else:
        require(integer(actor["place"]), "invalid signed place")


def observations(record):
    require(type(record.get("phase")) is str, "missing observation phase")
    state = record["state"]
    require({"frame", "time_ms", "rng_draws", "robots", "player_groups"} <= state.keys(), "missing Session field")
    require(all(integer(state[key], 0, 2**63 - 1) for key in ("frame", "time_ms", "rng_draws")), "invalid boundary")
    robots = state["robots"]
    require(type(robots) is list and len(robots) == 4, "Session actor inventory differs")
    for index, robot in enumerate(robots, 1):
        require(type(robot) is dict and integer(robot.get("id"), index, index), "invalid Session actor ID")
        require(robot.get("alive") is True, "unexpected retired Session actor")
        live_actor(robot, session=True)
    groups = state["player_groups"]
    require(type(groups) is list, "invalid Session group inventory")
    slots = []
    for group in groups:
        require(type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys(),
                "missing player-group field")
        require(integer(group["slot"], 0, 60) and integer(group["stored_count"]) and integer(group["members"], 0),
                "invalid group counters")
        require(integer(group["order"], 0, 9) and integer(group["target"], -1, 4), "invalid group order/target")
        slots.append(group["slot"])
    require(len(slots) == len(set(slots)), "duplicate group slot")
    require(all(r["group"] in slots for r in robots if r["side"] == 1 and r["group"] >= 0), "missing player group")
    for key in ("target", "tick_target", "first_core"):
        require(integer(record.get(key), -1, 6), "invalid normalized " + key)
    require(integer(record.get("repairer"), 1, 1) and type(record.get("changed")) is bool, "invalid repairer/change")
    counts, health = record.get("pulse_counts"), record.get("cached_health")
    require(type(counts) is list and len(counts) == 2 and all(integer(x, 0, 5) for x in counts), "invalid pulse counts")
    require(type(health) is list and len(health) == 2 and all(bits(x) for x in health), "invalid cached health")
    require(type(record.get("logic_order")) is list and all(integer(x, -1, 6) for x in record["logic_order"]),
            "invalid ordered logic inventory")
    actors = record.get("actors")
    require(type(actors) is list and len(actors) == 6, "custom actor inventory differs")
    for index, actor in enumerate(actors, 1):
        require(type(actor) is dict and integer(actor.get("id"), index, index), "invalid custom actor ID")
        require(type(actor.get("present")) is bool, "missing presence flag")
        if index != 5:
            require(actor["present"] is True and actor.get("alive") is True, "missing required live custom actor")
        if not actor["present"]:
            require(set(actor) == {"id", "present"}, "retired actor has stale live payload")
            continue
        require(type(actor.get("alive")) is bool and integer(actor.get("state"), 0, 7), "invalid actor life state")
        if not actor["alive"]:
            require(set(actor) == {"id", "present", "alive", "state"}, "non-live actor has live union payload")
            continue
        live_actor(actor)
        if index <= 4:
            for field in ("x_bits", "y_bits", "health_bits", "group", "in_place", "order", "destination"):
                require(actor[field] == robots[index - 1][field], "Session/custom actor disagreement: " + field)
            reduced = [[q[0], q[1], q[2], q[3], q[6] if q[6] <= 4 else -1] for q in actor["queue"]]
            require(reduced == robots[index - 1]["queue"], "Session/custom physical queue disagreement")


def expected():
    for family in ("retirement", "replace-far", "replace-near"):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                yield f"repair-competing-{family}-{rng}-{step}.jsonl", (family, rng, step)


def identity(header, arguments, revision=None, reference=False):
    require(header["configuration"] == arguments.configuration and header["compiler"] == arguments.compiler,
            "configuration/compiler mismatch")
    require(header.get("build_exe") is (arguments.target == "EXE") and header.get("cheats") is arguments.cheats,
            "role/cheats mismatch")
    version = header.get("compiler_version")
    require((type(version) is str and bool(version)) if header["compiler"] == "MinGW" else integer(version, 1),
            "invalid compiler_version contract")
    require(type(header.get("trace")) is bool and header["trace"] is arguments.trace, "trace profile mismatch")
    require(type(header.get("assert_off")) is bool and header["assert_off"] is (arguments.configuration == "Release"),
            "assertion profile mismatch")
    require(integer(header.get("precision_control"), 0) and integer(header.get("dirty"), 0, 1),
            "invalid build identity")
    require(type(header.get("revision")) is str and bool(header["revision"]), "missing source identity")
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(header.get(key) in ("Original", "Corrected"), "missing effective compatibility field " + key)
    if arguments.revision or revision or reference:
        require(re.fullmatch(r"[0-9a-f]{40}", header["revision"]) is not None and header["dirty"] == 0,
                "strict evidence requires a full clean compiled source identity")
    if revision:
        require(header["revision"] == revision and header["dirty"] == 0, "stale or dirty compiled source identity")


def validate(records, description, arguments, revision=None, reference=False):
    family, rng, step = description
    replacement, near = family != "retirement", family == "replace-near"
    header = records[0]
    identity(header, arguments, revision, reference)
    require(header["fixture"] == "competing-moving-repair-v2", "fixture mismatch")
    require(header.get("replacement") is replacement and header.get("competitor_in_range") is near, "family mismatch")
    expected_rng = "ParkMiller" if rng == "pm" else "LegacyCRT"
    require(header["rng"] == expected_rng and header["seed"] == 84
            and header["step_ms"] == step, "RNG/seed/step mismatch")
    require(header.get("navigation_layout") == "open-v1"
            and header.get("max_frames") == 3000
            and header.get("audio") == "unavailable" and header.get("initially_selected") is True,
                    "fixture policy mismatch")
    require(header.get("render") == "storage-device-no-draw-cursor-surrogate"
            and header.get("viewport") == [1280, 720], "render policy mismatch")
    phases = collections.Counter()
    ticks = []
    commands = []
    previous = None
    events = []
    before = after = None
    last_draws = None
    for record in records[1:-1]:
        if record["type"] == "command":
            require(integer(record.get("time_ms"), 0) and record["time_ms"] == record["frame"] * step,
                    "invalid command boundary")
            require(integer(record.get("rng_draws_before"), 0, 2**63 - 1)
                    and integer(record.get("rng_draws_after"), record["rng_draws_before"], 2**63 - 1),
                            "invalid command RNG")
            require(record["rng_draws_before"] == last_draws, "command RNG before differs")
            last_draws = record["rng_draws_after"]
            events.append(("command:" + record["name"], record["frame"]))
            commands.append(record)
            continue
        observations(record)
        state, actors = record["state"], record["actors"]
        phase = record["phase"]
        phases[phase] += 1
        events.append((phase, state["frame"]))
        require(record["changed"] is (after is not None or phase == "after-change"), "change flag schedule differs")
        require(state["time_ms"] == state["frame"] * step, "snapshot frame/time mismatch")
        if last_draws is not None:
            require(state["rng_draws"] >= last_draws, "snapshot RNG moved backwards")
            if previous["state"]["frame"] == state["frame"]:
                require(state["rng_draws"] == last_draws, "same-boundary RNG mismatch")
        last_draws = state["rng_draws"]
        require(record["logic_order"] == ([1, 2, 3, 4, 6] if record["changed"] and not replacement
                                          else [1, 2, 3, 4, 6, 5]), "ordered live candidate inventory differs")
        for index, count in enumerate(record["pulse_counts"]):
            require(record["cached_health"][index] == float_bits(min(50, 32 + count * 4) / 10),
                    "pulse/cached health differs")
            actor = actors[index + 4]
            if actor["present"]:
                require(actor["health_bits"] == record["cached_health"][index], "patient health differs")
            if previous:
                delta = count - previous["pulse_counts"][index]
                require(delta in (0, 1), "pulse count jump/backtrack")
                if delta:
                    require(record["tick_target"] == index + 5, "repair pulse attributed to wrong pre-tick target")
                    if index == 1:
                        require(record["changed"] and (not replacement or near or record["pulse_counts"][0] == 5),
                                "competitor repaired before priority permits")
                    elif record["changed"] and near:
                        require(record["pulse_counts"][1] == 5, "first patient resumed before competitor completed")
        if phase == "initial":
            require(previous is None and state["frame"] == 0 and record["pulse_counts"] == [0, 0]
                    and record["target"] == 0 and record["first_core"] == 5 and not record["changed"],
                            "initial state differs")
        else:
            require(actors[0]["order"] == 1 and actors[0]["destination"] ==
                    ([28, 48] if record["changed"] and replacement else [48, 18]), "move intention/destination changed")
        if phase == "after-move":
            require(state["frame"] == 0 and record["target"] == 5 and not record["changed"],
                    "initial acquisition differs")
        elif phase == "before-change":
            before = record
            require(not record["changed"] and record["target"] == 5 and record["pulse_counts"] == [3 if near else 2, 0],
                    "intervention boundary differs")
            require(actors[0]["x_bits"] > float_bits(100), "repairer did not move before intervention")
        elif phase == "after-change":
            after = record
            require(before is not None and record["changed"] and state["frame"] == before["state"]["frame"],
                    "intervention transition differs")
            require(record["target"] == (6 if near else 5 if replacement else 0), "post-intervention target differs")
        elif phase == "tick":
            ticks.append(state["frame"])
        elif phase == "final":
            require(record["changed"] and record["target"] == 0
                    and record["pulse_counts"] == [5 if replacement else 2, 5],
                    "terminal repair result differs")
            require(all(actors[i]["in_place"] for i in (0, 1)), "selected robots did not arrive")
            require(actors[0]["x_bits"] == float_bits(300 if replacement else 500)
                    and actors[0]["y_bits"] == float_bits(500 if replacement else 200),
                            "terminal physical arrival differs")
            require(all(q[0] != 5 for q in actors[0]["queue"]), "terminal fire order remains")
        else:
            require(phase == "initial", "unknown observation phase")
        if record["changed"] and not replacement:
            require(record["first_core"] == 0 and actors[4] == {"id": 5, "present": False}, "retired patient retained")
        else:
            require(record["first_core"] == 5 and actors[4]["present"] is True, "retained first patient disappeared")
        previous = record
    final_frame = (25000 + step - 1) // step
    require(ticks == list(range(1, final_frame + 1)), "missing/extra completed map boundary")
    require(phases == {"initial": 1, "after-move": 1, "before-change": 1, "after-change": 1,
                       "tick": final_frame, "final": 1}, "missing/extra observation phase")
    require([c["name"] for c in commands] == ["move", "replace-move" if replacement else "retire-patient"],
            "command schedule differs")
    require(commands[0]["frame"] == 0 and commands[1]["frame"] == before["state"]["frame"],
            "command schedule boundary differs")
    schedule = [("initial", 0), ("command:move", 0), ("after-move", 0)]
    for frame in range(1, final_frame + 1):
        if frame == before["state"]["frame"]:
            schedule += [("before-change", frame),
                         ("command:" + ("replace-move" if replacement else "retire-patient"), frame),
                         ("after-change", frame)]
        schedule.append(("tick", frame))
    schedule.append(("final", final_frame))
    require(events == schedule, "observation/command ordering differs")
    require(integer(records[-1].get("rng_draws"), 0, 2**63 - 1)
            and records[-1]["rng_draws"] == previous["state"]["rng_draws"], "terminal RNG differs")
    require(commands[0].get("world") == [500, 200] and point(commands[0]["world"]), "initial Move input differs")
    if replacement:
        require(commands[1].get("world") == [300, 500] and point(commands[1]["world"]),
                "replacement Move input differs")
    else:
        require(integer(commands[1].get("patient"), 5, 5), "deleted patient differs")
    require(records[-1]["frame"] == final_frame and records[-1]["time_ms"] == final_frame * step,
            "completion horizon differs")
    require(previous["phase"] == "final" and after is not None, "missing final assertion boundary")
    return header


def check_directory(directory, arguments, reference=False):
    inventory = list(expected())
    require({p.name for p in directory.glob("repair-competing-*.jsonl")} == {n for n, _ in inventory},
            "finite twelve-file inventory differs")
    revision = arguments.reference_revision if reference else arguments.revision
    profile = None
    for name, description in inventory:
        with (directory / name).open(encoding="utf-8") as stream:
            records = TRACES.read_records(stream, name)
        header = validate(records, description, arguments, revision, reference)
        current = {key: header[key] for key in ("revision", "dirty", "configuration", "compiler", "compiler_version",
                   "build_exe", "cheats", "trace", "precision_control", "assert_off", "delayed_damage", "group_counts",
                   "place_containment", "zero_x_trace")}
        require(profile is None or current == profile, "inconsistent corpus identity/effective profile")
        profile = current
        yield name, records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--reference-revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"), required=True)
    parser.add_argument("--target", choices=("EXE", "DLL"), required=True)
    parser.add_argument("--trace", choices=("ON", "OFF"), required=True)
    parser.add_argument("--cheats", choices=("ON", "OFF"), required=True)
    args = parser.parse_args(argv)
    args.cheats = args.cheats == "ON"
    args.trace = args.trace == "ON"
    try:
        for pin in (args.revision, args.reference_revision):
            require(pin is None or re.fullmatch(r"[0-9a-f]{40}", pin) is not None,
                    "revision pin must be full lowercase hex")
        require(not args.reference_revision or args.reference is not None, "reference pin requires reference directory")
        if args.reference and not args.reference_revision:
            print("Diagnostic comparison: the historical reference revision is not pinned; "
                  "this is not accepted baseline evidence.", file=sys.stderr)
        if not args.revision:
            print("Diagnostic candidate: no compiled source revision pin was supplied.", file=sys.stderr)
        count = 0
        candidate = check_directory(args.directory, args)
        reference = check_directory(args.reference, args, reference=True) if args.reference else None
        for name, records in candidate:
            if reference:
                other_name, other = next(reference)
                require(name == other_name, "reference inventory differs")
                difference = TRACES.compare_records(records, other)
                require(difference is None, name + ": " + str(difference))
            count += 1
        print(f"Validated {count} competing-repair recordings" + (" and exact references" if reference else ""))
        return 0
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, StopIteration) as error:
        print(f"Invalid competing-repair recordings: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
