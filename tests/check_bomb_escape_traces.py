"""Validate the existing 96 physical bomb-escape episodes and their original replay pairs."""

import argparse
import collections
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import sys

SPEC = importlib.util.spec_from_file_location(
    "battle_traces", Path(__file__).parents[1] / "tools" / "compare-battle-traces.py"
)
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)
EVENTS = ("safe", "refuge", "first_return", "second_escape", "cleared", "damage", "arrived", "retired")


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=-(2**31), maximum=2**31 - 1):
    return type(value) is int and minimum <= value <= maximum


def bits(value):
    return integer(value, 0, 0xFFFFFFFF)


def floating(value):
    require(bits(value), "invalid float bits")
    result = struct.unpack("<f", struct.pack("<I", value))[0]
    require(math.isfinite(result), "nonfinite physical observation")
    return result


def sample(value):
    if value is None:
        return None
    require(type(value) is dict and set(value) == {"value_bits"}, "invalid source distance sample")
    result = floating(value["value_bits"])
    require(result >= 0, "negative source distance sample")
    return result


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(x) for x in value)


def queue(value, width):
    require(type(value) is list and len(value) <= 5, "invalid physical queue capacity/type")
    for order in value:
        require(type(order) is list and len(order) == width, "invalid physical queue shape")
        require(all(integer(x, 0, 8) for x in order[:2]), "invalid physical order type/phase")
        require(all(bits(x) for x in order[2 : 5 if width == 7 else 4]), "invalid order float bits")
        if width == 7:
            require(integer(order[5]), "invalid signed Param4")
        require(integer(order[-1], -1, 4), "unnormalized physical order object")


def observations(record):
    require(
        type(record.get("phase")) is str and record.get("original_prefix") in ("", "remove:", "detonate:"),
        "missing phase/original prefix",
    )
    state = record["state"]
    require({"frame", "time_ms", "rng_draws", "robots", "player_groups"} <= state.keys(), "missing Session field")
    require(all(integer(state[key], 0, 2**63 - 1) for key in ("frame", "time_ms", "rng_draws")), "invalid boundary")
    require(state["player_groups"] == [], "unexpected player-group intention")
    robots, actors = state["robots"], record.get("actors")
    require(
        type(robots) is list and len(robots) == 4 and type(actors) is list and len(actors) == 4,
        "actor inventory differs",
    )
    for index, (robot, actor) in enumerate(zip(robots, actors), 1):
        require(
            type(robot) is dict and integer(robot.get("id"), index, index) and type(robot.get("alive")) is bool,
            "invalid Session identity/life",
        )
        require(
            type(actor) is dict and integer(actor.get("id"), index, index) and type(actor.get("present")) is bool,
            "invalid custom identity/presence",
        )
        if index != 3:
            require(robot["alive"] is True and actor["present"] is True, "required actor disappeared")
        if not actor["present"]:
            require(
                set(actor) == {"id", "present"} and robot == {"id": index, "alive": False},
                "retired actor retains payload",
            )
            continue
        require(type(actor.get("alive")) is bool and integer(actor.get("state"), 0, 7), "invalid custom life/state")
        require(actor["alive"] is (actor["state"] != 5) and robot["alive"] is actor["alive"], "life state differs")
        if not actor["alive"]:
            require(
                set(actor) == {"id", "present", "alive", "state"} and robot == {"id": index, "alive": False},
                "non-live actor retains live union payload",
            )
            continue
        fields = {
            "x_bits",
            "y_bits",
            "health_bits",
            "group",
            "place",
            "moving",
            "move",
            "returning",
            "saved",
            "must_die",
            "observes_cover",
            "queue",
        }
        require(fields <= actor.keys(), "missing live custom field")
        require(all(bits(actor[key]) for key in ("x_bits", "y_bits", "health_bits")), "invalid custom float bits")
        require(integer(actor["group"], -1, 60) and integer(actor["place"]), "invalid group/place")
        require(
            all(type(actor[key]) is bool for key in ("moving", "returning", "must_die", "observes_cover")),
            "invalid movement/perception flag",
        )
        require(point(actor["move"]) and point(actor["saved"]), "invalid movement/return coordinates")
        require(actor["moving"] or actor["move"] == [-1, -1], "inactive Move has stale coordinates")
        require(actor["returning"] or actor["saved"] == [-1, -1], "inactive return has stale coordinates")
        queue(actor["queue"], 7)
        live = {
            "x_bits",
            "y_bits",
            "health_bits",
            "group",
            "state",
            "side",
            "order",
            "in_place",
            "target",
            "target_is_null",
            "destination",
            "queue",
        }
        require(live <= robot.keys(), "missing live Session field")
        require(all(bits(robot[key]) for key in ("x_bits", "y_bits", "health_bits")), "invalid Session float bits")
        require(
            integer(robot["state"], 0, 7)
            and integer(robot["side"])
            and integer(robot["group"], -1, 60)
            and integer(robot["order"], -1, 9),
            "invalid Session integer",
        )
        require(
            type(robot["in_place"]) is bool
            and type(robot["target_is_null"]) is bool
            and integer(robot["target"], -1, 4),
            "invalid Session flag/target",
        )
        require(
            robot["target_is_null"] is (robot["target"] == 0) and point(robot["destination"]),
            "Session target/destination differs",
        )
        queue(robot["queue"], 5)
        for key in ("x_bits", "y_bits", "health_bits", "group", "state"):
            require(actor[key] == robot[key], "Session/custom actor disagreement: " + key)
        require(
            [[q[0], q[1], q[2], q[3], q[6]] for q in actor["queue"]] == robot["queue"],
            "Session/custom physical queue disagreement",
        )
    require(integer(record.get("bomber_core"), -1, 3) and record["bomber_core"] in (-1, 0, 3), "invalid bomber core")
    require(
        type(record.get("bomber_core_present")) is bool
        and record["bomber_core_present"] is (record["bomber_core"] != 0),
        "core presence/identity differs",
    )
    require(
        type(record.get("kills")) is list and len(record["kills"]) == 2 and all(integer(x, 0) for x in record["kills"]),
        "invalid side kill statistics",
    )
    require(
        type(record.get("start_edge_masks")) is list
        and len(record["start_edge_masks"]) == 2
        and all(integer(x, 0, 255) for x in record["start_edge_masks"]),
        "invalid refuge edge masks",
    )
    progress = record.get("milestones")
    require(
        type(progress) is dict
        and set(EVENTS) | {"maximum_distance_bits", "bomb_distance", "refuge_distance"} <= progress.keys(),
        "missing milestone",
    )
    require(
        all(integer(progress[key], 0, 45008) for key in EVENTS) and bits(progress["maximum_distance_bits"]),
        "invalid milestone type",
    )


def pairs():
    for family in ("free", "reserved", "unreachable"):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for containment in ("corrected", "original"):
                    for detonate in (False, True):
                        action = "detonate" if detonate else "remove"
                        prefix = f"bomb-escape-{family}-{action}-{containment}-{rng}-{step}"
                        yield (family, rng, step, containment, detonate), [f"{prefix}-{r}.jsonl" for r in (0, 1)]


def identity(header, args, pin=None, reference=False):
    require(
        header["configuration"] == args.configuration and header["compiler"] == args.compiler,
        "configuration/compiler mismatch",
    )
    require(
        header.get("build_exe") is (args.target == "EXE")
        and header.get("cheats") is args.cheats
        and header.get("trace") is args.trace,
        "role/cheats/trace mismatch",
    )
    require(type(header.get("trace")) is bool, "invalid trace flag")
    version = header.get("compiler_version")
    require(
        (type(version) is str and bool(version)) if args.compiler == "MinGW" else integer(version, 1),
        "invalid compiler_version contract",
    )
    require(
        type(header.get("assert_off")) is bool and header["assert_off"] is (args.configuration == "Release"),
        "assertion profile mismatch",
    )
    require(
        integer(header.get("precision_control"), 0) and integer(header.get("dirty"), 0, 1), "invalid build identity"
    )
    require(type(header.get("revision")) is str and bool(header["revision"]), "missing source identity")
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(header.get(key) in ("Original", "Corrected"), "missing compatibility mode " + key)
    if args.revision or pin or reference:
        require(
            re.fullmatch(r"[0-9a-f]{40}", header["revision"]) is not None and header["dirty"] == 0,
            "strict evidence requires a full clean compiled source identity",
        )
    if pin:
        require(header["revision"] == pin, "compiled source revision does not match its pin")
    return {
        key: header[key]
        for key in (
            "revision",
            "dirty",
            "configuration",
            "compiler",
            "compiler_version",
            "build_exe",
            "cheats",
            "trace",
            "precision_control",
            "assert_off",
            "delayed_damage",
            "group_counts",
            "zero_x_trace",
        )
    }


def validate(records, description, replay, args, pin=None, reference=False):
    family, rng, step, containment, detonate = description
    header = records[0]
    profile = identity(header, args, pin, reference)
    require(header["fixture"] == "bomb-escape-journey-v1" and header.get("refuge") == family, "fixture/family mismatch")
    require(header.get("detonate") is detonate and integer(header.get("replay"), replay, replay), "episode mismatch")
    require(header["place_containment"] == containment.capitalize(), "effective containment mismatch")
    require(
        header["rng"] == ("ParkMiller" if rng == "pm" else "LegacyCRT")
        and header["seed"] == 84
        and header["step_ms"] == step
        and header.get("max_frames") == 5000,
        "RNG/seed/step/bound mismatch",
    )
    require(
        header.get("audio") == "unavailable"
        and header.get("navigation_layout") == "open-v1"
        and header.get("initially_selected") is False,
        "fixture execution policy mismatch",
    )
    require(
        header.get("render") == "storage-device-no-draw-cursor-surrogate"
        and header.get("viewport") == [1280, 720]
        and point(header["viewport"]),
        "render policy mismatch",
    )
    expected_refuge = (
        [20, 20]
        if family == "unreachable"
        else [8, 48] if family == "reserved" and containment == "corrected" else [48, 8]
    )
    progress = dict.fromkeys(EVENTS, 0)
    events, ticks = [], []
    phases = collections.Counter()
    previous_frame = None
    last_draws = None
    last_maximum = 0.0
    measured_frame = None
    last = None
    clearance_frame = None
    for record in records[1:-1]:
        if record["type"] == "command":
            command = record["name"]
            require(
                integer(record.get("time_ms"), 0) and record["time_ms"] == record["frame"] * step,
                "invalid command boundary",
            )
            require(
                integer(record.get("rng_draws_before"), 0, 2**63 - 1)
                and integer(record.get("rng_draws_after"), record["rng_draws_before"], 2**63 - 1),
                "invalid command RNG",
            )
            require(record["rng_draws_before"] == last_draws, "command RNG before differs")
            last_draws = record["rng_draws_after"]
            if command in ("reserve-cover", "initial-move"):
                actor_id = 1 if command == "reserve-cover" else 2
                destination = [48, 8] if command == "reserve-cover" else [20, 20]
                require(
                    integer(record.get("actor"), actor_id, actor_id)
                    and point(record.get("destination"))
                    and record["destination"] == destination,
                    "Move input differs",
                )
            elif command == "escape-from-bomb":
                require(
                    integer(record.get("side"), 1, 1) and record["rng_draws_before"] == last_draws,
                    "initial escape side/RNG differs",
                )
            elif command == "block-refuge-edges":
                require(
                    integer(record.get("actor"), 2, 2)
                    and record.get("masks") == [2, 2]
                    and all(integer(x, 0, 255) for x in record["masks"]),
                    "blocked-edge input differs",
                )
            else:
                require(
                    command in ("remove", "detonate") and integer(record.get("actor"), 3, 3), "clearance input differs"
                )
                require(clearance_frame is None, "duplicate threat clearance")
                clearance_frame = record["frame"]
                progress["cleared"] = record["time_ms"]
            events.append(("command:" + command, record["frame"]))
            continue
        observations(record)
        phase, state = record["phase"], record["state"]
        actors, milestone = record["actors"], record["milestones"]
        cover, escaping, bomber = actors[:3]
        now = state["time_ms"]
        require(
            now == state["frame"] * step and all(milestone[k] % step == 0 for k in EVENTS), "observation time mismatch"
        )
        require(last_draws is None or state["rng_draws"] >= last_draws, "RNG moved backwards")
        if previous_frame == state["frame"]:
            require(state["rng_draws"] == last_draws, "same-boundary RNG mismatch")
        last_draws, previous_frame = state["rng_draws"], state["frame"]
        events.append((phase, state["frame"]))
        phases[phase] += 1
        require(
            record["original_prefix"] == (("detonate:" if detonate else "remove:") if phase == "clearance" else ""),
            "original intervention prefix differs",
        )
        require(
            cover["group"] == escaping["group"] == -1
            and not cover["returning"]
            and all(q[0] != 5 for q in cover["queue"]),
            "cover/escaping intent changed",
        )
        require(not escaping["returning"] or escaping["saved"] == [20, 20], "saved original destination differs")
        require(record["kills"] == [0, 0], "nonlethal journey changed side kills")
        if phase != "setup-ready":
            require(
                record["start_edge_masks"] == ([2, 2] if family == "unreachable" else [0, 0]), "refuge masks differ"
            )
        if family == "unreachable":
            require(
                not escaping["returning"] and (not escaping["moving"] or escaping["move"] == [20, 20]),
                "unreachable refuge replaced original route",
            )
        maximum = floating(milestone["maximum_distance_bits"])
        bomb_distance = sample(milestone["bomb_distance"])
        refuge_distance = sample(milestone["refuge_distance"])
        if phase == "setup-ready":
            require(
                maximum == 0 and bomb_distance is None and refuge_distance is None,
                "distance sampled before its original source evaluation",
            )
        else:
            require(
                bomb_distance is not None and maximum == max(last_maximum, bomb_distance),
                "maximum/source distance differs",
            )
        last_maximum = maximum
        if phase in ("tick", "before-clearance"):
            if measured_frame != state["frame"]:
                measured_frame = state["frame"]
                if not progress["safe"] and bomb_distance > 330:
                    progress["safe"] = now
                if family != "unreachable":
                    if not progress["refuge"]:
                        require(refuge_distance is not None, "missing evaluated source refuge distance")
                        if refuge_distance < 1:
                            progress["refuge"] = now
                    else:
                        require(refuge_distance is None, "source refuge call should have short-circuited")
                    if (
                        not progress["first_return"]
                        and not escaping["returning"]
                        and escaping["moving"]
                        and escaping["move"] == [20, 20]
                    ):
                        progress["first_return"] = now
                else:
                    require(refuge_distance is None, "unreachable case evaluated a refuge distance")
                if progress["first_return"] and escaping["returning"] and not progress["second_escape"]:
                    progress["second_escape"] = now
            if phase == "tick" and progress["cleared"]:
                if not record["bomber_core_present"] and not progress["retired"]:
                    progress["retired"] = now
                if cover["health_bits"] != float_bits(10) and not progress["damage"]:
                    progress["damage"] = now
                if (
                    not progress["arrived"]
                    and not escaping["moving"]
                    and not escaping["returning"]
                    and escaping["x_bits"] == escaping["y_bits"] == float_bits(220)
                ):
                    progress["arrived"] = now
        if last is not None and phase != "planned" and last["state"]["frame"] == state["frame"]:
            for field in ("bomb_distance", "refuge_distance"):
                require(milestone[field] == last["milestones"][field], "same-boundary source sample changed")
        require(all(milestone[k] == progress[k] for k in EVENTS), "observed milestone schedule differs")
        if not progress["cleared"]:
            require(
                record["bomber_core"] == 3 and bomber.get("alive") is True and bomber["observes_cover"],
                "threat lifetime/perception lost before clearance",
            )
            require(cover["health_bits"] == escaping["health_bits"] == float_bits(10), "damage before clearance")
        if phase == "setup-ready":
            require(
                last is None and now == 0 and maximum == 0 and record["start_edge_masks"] == [0, 0],
                "setup state differs",
            )
        elif phase == "planned":
            require(
                now == 0
                and maximum == bomb_distance == 80
                and refuge_distance is None
                and escaping["moving"]
                and escaping["move"] == expected_refuge
                and escaping["returning"] is (family != "unreachable"),
                "initial escape/refuge choice differs",
            )
        elif phase == "before-clearance":
            trigger = (
                (now >= 1200)
                if family == "unreachable"
                else bool(progress["second_escape"]) if family == "free" and not detonate else bool(progress["safe"])
            )
            require(trigger and not progress["cleared"], "threat clearance trigger differs")
        elif phase == "clearance":
            if detonate:
                require(bomber.get("alive") is True and bomber["must_die"], "detonation did not mark bomber")
            else:
                require(record["bomber_core"] == 0 and bomber == {"id": 3, "present": False}, "removed bomber retained")
        elif phase == "tick":
            ticks.append(state["frame"])
        elif phase == "final":
            require(
                escaping["x_bits"] == escaping["y_bits"] == float_bits(220)
                and not escaping["moving"]
                and not escaping["returning"],
                "terminal route did not complete",
            )
            require(record["bomber_core"] == 0 and bomber == {"id": 3, "present": False}, "terminal bomber not retired")
            require(cover["health_bits"] == float_bits(9 if detonate else 10), "terminal cover damage differs")
            require(
                escaping["health_bits"] == float_bits(9 if detonate and family == "unreachable" else 10),
                "terminal escaping-robot damage differs",
            )
        else:
            require(False, "unknown observation phase")
        last = record
    require(
        progress["cleared"] > 0
        and progress["arrived"] > progress["cleared"]
        and progress["retired"] >= progress["cleared"],
        "terminal milestone ordering differs",
    )
    if family == "unreachable":
        require(progress["safe"] == progress["first_return"] == 0 and last_maximum < 250, "unreachable safety differs")
        require(progress["cleared"] == ((1200 + step - 1) // step) * step, "unreachable clearance time differs")
    else:
        require(
            progress["safe"] > 0
            and last_maximum > 330
            and progress["refuge"] > progress["safe"]
            and progress["first_return"] >= progress["refuge"]
            and progress["first_return"] < progress["arrived"],
            "escape/refuge/return ordering differs",
        )
    if family == "free" and not detonate:
        require(
            progress["second_escape"] == progress["cleared"] == (8910 if step == 10 else 8976)
            and progress["second_escape"] > progress["first_return"],
            "measured free rediversion differs",
        )
    else:
        require(progress["second_escape"] == 0, "unexpected second diversion")
        if family != "unreachable":
            require(progress["cleared"] == progress["safe"], "safe-clearance boundary differs")
    if detonate:
        require(progress["damage"] == progress["cleared"] + (300 // step + 1) * step, "effect damage deadline differs")
    else:
        require(
            progress["damage"] == 0 and progress["retired"] == progress["cleared"], "removal lifetime/damage differs"
        )
    final_frame = max((progress["arrived"] + 500 + step - 1) // step, progress["retired"] // step)
    require(
        final_frame <= (45000 + step - 1) // step and ticks == list(range(1, final_frame + 1)),
        "missing/extra map boundary or changed completion horizon",
    )
    require(
        phases
        == {"setup-ready": 1, "planned": 1, "before-clearance": 1, "clearance": 1, "tick": final_frame, "final": 1},
        "missing/extra observation phase",
    )
    schedule = [("setup-ready", 0)]
    if family != "free":
        schedule.append(("command:reserve-cover" if family == "reserved" else "command:block-refuge-edges", 0))
    schedule += [("command:initial-move", 0), ("command:escape-from-bomb", 0), ("planned", 0)]
    for frame in range(1, final_frame + 1):
        if frame == clearance_frame:
            schedule += [
                ("before-clearance", frame),
                ("command:detonate" if detonate else "command:remove", frame),
                ("clearance", frame),
            ]
        schedule.append(("tick", frame))
    schedule.append(("final", final_frame))
    require(events == schedule, "observation/command ordering differs")
    tail = records[-1]
    require(
        tail["frame"] == final_frame
        and tail["time_ms"] == final_frame * step
        and integer(tail.get("rng_draws"), 0, 2**63 - 1)
        and tail["rng_draws"] == last["state"]["rng_draws"],
        "terminal boundary/RNG differs",
    )
    return profile


def original_vectors(records):
    """Reconstruct only the original stored vector; extra observation phases are excluded."""
    for record in records:
        if record["type"] == "snapshot" and record["phase"] in ("planned", "clearance", "tick"):
            yield record["original_prefix"] + json.dumps(record["state"], separators=(",", ":"))


def pair_difference(left, right):
    # The sole omitted field is the ordinal identifying the two existing replay executions.
    a, b = list(left), list(right)
    a[0], b[0] = dict(a[0], replay=0), dict(b[0], replay=0)
    return TRACES.compare_records(a, b)


def check_directory(directory, args, reference=False):
    inventory = list(pairs())
    names = {name for _, files in inventory for name in files}
    require({p.name for p in directory.glob("bomb-escape-*.jsonl")} == names, "finite 96-file inventory differs")
    common = None
    pin = args.reference_revision if reference else args.revision
    for description, files in inventory:
        records = []
        for replay, name in enumerate(files):
            with (directory / name).open(encoding="utf-8") as stream:
                observed = TRACES.read_records(stream, name)
            profile = validate(observed, description, replay, args, pin, reference)
            require(common is None or profile == common, "inconsistent corpus identity/effective profile")
            common = profile
            records.append(observed)
        require(pair_difference(*records) is None, files[0] + ": original replay observations diverge")
        yield files, records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--reference-revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"), required=True)
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"), required=True)
    parser.add_argument("--target", choices=("EXE", "DLL"), required=True)
    parser.add_argument("--cheats", choices=("ON", "OFF"), required=True)
    parser.add_argument("--trace", choices=("ON", "OFF"), required=True)
    args = parser.parse_args(argv)
    args.cheats, args.trace = args.cheats == "ON", args.trace == "ON"
    try:
        for pin in (args.revision, args.reference_revision):
            require(
                pin is None or re.fullmatch(r"[0-9a-f]{40}", pin) is not None, "revision pin must be full lowercase hex"
            )
        require(not args.reference_revision or args.reference is not None, "reference pin requires reference directory")
        if not args.revision:
            print("Diagnostic candidate: no compiled source revision pin supplied.", file=sys.stderr)
        if args.reference and not args.reference_revision:
            print(
                "Diagnostic comparison: historical reference is unpinned; not accepted baseline evidence.",
                file=sys.stderr,
            )
        reference = check_directory(args.reference, args, True) if args.reference else None
        count = 0
        for files, recordings in check_directory(args.directory, args):
            if reference:
                other_files, other = next(reference)
                require(files == other_files, "reference inventory differs")
                for name, left, right in zip(files, recordings, other):
                    difference = TRACES.compare_records(left, right)
                    require(difference is None, name + ": " + str(difference))
            count += 1
        print(f"Validated 96 bomb-escape recordings and {count} original replay pairs")
        return 0
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, TypeError, StopIteration) as error:
        print(f"Invalid bomb-escape recordings: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
