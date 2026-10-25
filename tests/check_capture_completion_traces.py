"""Validate the48 existing registered capture command-completion episodes."""

import argparse
import collections
import importlib.util
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
PREORDER = (1 << 19) | (1 << 5)


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, low=-(2**31), high=2**31 - 1):
    return type(value) is int and low <= value <= high


def bits(value):
    return integer(value, 0, 0xFFFFFFFF)


def floating(value):
    require(bits(value), "invalid float bits")
    result = struct.unpack("<f", struct.pack("<I", value))[0]
    require(math.isfinite(result), "nonfinite physical observation")
    return result


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(x) for x in value)


def projected(value):
    return value if value <= 4 else -1


def queue(value, width, maximum):
    require(type(value) is list and len(value) <= 5, "invalid physical queue capacity/type")
    for order in value:
        require(type(order) is list and len(order) == width, "invalid physical queue shape")
        require(all(integer(x, 0, 8) for x in order[:2]), "invalid physical type/phase")
        require(all(bits(x) for x in order[2 : 5 if width == 7 else 4]), "invalid order float bits")
        if width == 7:
            require(integer(order[5]), "invalid signed Param4")
        require(integer(order[-1], -1, maximum), "unnormalized order object")


def observations(record):
    state = record["state"]
    require({"frame", "time_ms", "rng_draws", "robots", "player_groups"} <= state.keys(), "missing Session field")
    require(all(integer(state[k], 0, 2**63 - 1) for k in ("frame", "time_ms", "rng_draws")), "invalid Session boundary")
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
            "invalid actor presence/ID",
        )
        if not actor["present"]:
            require(
                set(actor) == {"id", "present"} and robot == {"id": index, "alive": False},
                "retired actor retains payload",
            )
            continue
        require(type(actor.get("alive")) is bool and integer(actor.get("state"), 0, 7), "invalid actor life/state")
        require(actor["alive"] is (actor["state"] != 5) and actor["alive"] is robot["alive"], "actor life disagreement")
        if not actor["alive"]:
            require(
                set(actor) == {"id", "present", "alive", "state"} and robot == {"id": index, "alive": False},
                "non-live actor has live union payload",
            )
            continue
        fields = {
            "x_bits",
            "y_bits",
            "health_bits",
            "group",
            "group_target",
            "group_order",
            "destination",
            "queue",
            "capture_target",
        }
        require(fields <= actor.keys(), "missing live actor field")
        for key in ("x_bits", "y_bits", "health_bits"):
            floating(actor[key])
        require(
            integer(actor["group"], -1, 60)
            and integer(actor["group_target"], -1, 7)
            and integer(actor["capture_target"], -1, 7),
            "invalid group/target",
        )
        require(integer(actor["group_order"], -1, 9) and point(actor["destination"]), "invalid group intention")
        queue(actor["queue"], 7, 7)
        captures = [q[6] for q in actor["queue"] if q[0] == 7 and q[6] != 0]
        require(actor["capture_target"] == (captures[0] if captures else 0), "capture target/order disagrees")
        required = {
            "x_bits",
            "y_bits",
            "health_bits",
            "group",
            "state",
            "side",
            "order",
            "target",
            "target_is_null",
            "destination",
            "in_place",
            "queue",
        }
        require(required <= robot.keys(), "missing live Session field")
        for key in ("x_bits", "y_bits", "health_bits", "group", "state", "destination"):
            require(actor[key] == robot[key], "Session/custom disagreement: " + key)
        require(
            integer(robot["side"], 1 if index <= 2 else 2, 1 if index <= 2 else 2)
            and integer(robot["order"], -1, 9)
            and integer(robot["target"], -1, 4),
            "invalid Session integer",
        )
        require(type(robot["in_place"]) is bool and type(robot["target_is_null"]) is bool, "invalid Session flag")
        require(
            actor["group_order"] == robot["order"] and projected(actor["group_target"]) == robot["target"],
            "Session/custom group intention disagrees",
        )
        require(robot["target_is_null"] is (robot["target"] == 0), "null-target flag disagrees")
        queue(robot["queue"], 5, 4)
        require(
            [[q[0], q[1], q[2], q[3], projected(q[6])] for q in actor["queue"]] == robot["queue"],
            "Session/custom order projection differs",
        )
    groups = state["player_groups"]
    require(type(groups) is list, "invalid player-group inventory")
    slots = []
    for group in groups:
        require(
            type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys(),
            "missing player-group field",
        )
        require(
            integer(group["slot"], 0, 60)
            and integer(group["stored_count"])
            and integer(group["members"], 0, 4)
            and integer(group["order"], 0, 9)
            and integer(group["target"], -1, 4),
            "invalid player-group scalar",
        )
        slots.append(group["slot"])
    require(len(slots) == len(set(slots)), "duplicate group slot")
    require(
        all(r["group"] in slots for r in robots if r["alive"] and r["side"] == 1 and r["group"] >= 0),
        "missing active player group",
    )
    building = record.get("building")
    fields = {"id", "owner", "kind", "paint", "color", "capturer", "keepalive", "queue_count", "queue"}
    require(type(building) is dict and fields <= building.keys(), "missing building field")
    require(
        integer(building["id"], 5, 5) and integer(building["owner"], 0, 2) and integer(building["kind"], 0, 1),
        "invalid building identity/ownership",
    )
    require(
        integer(building["paint"], 0, 14)
        and bits(building["color"])
        and integer(building["capturer"], -1, 4)
        and integer(building["keepalive"], 0),
        "invalid capture scalar",
    )
    if building["kind"] == 0:
        require(
            integer(building.get("base_state"), 0, 3) and bits(building.get("floor_bits")), "missing/invalid base phase"
        )
        require(0 <= floating(building["floor_bits"]) <= 1, "invalid base floor")
    else:
        require("base_state" not in building and "floor_bits" not in building, "factory has unrelated base payload")
    opaque = building["queue"]
    require(
        type(opaque) is list and integer(building["queue_count"], 0, 2) and building["queue_count"] == len(opaque),
        "opaque queue count differs",
    )
    for item in opaque:
        require(
            type(item) is dict
            and set(item) == {"id", "owner"}
            and integer(item["id"], 6, 7)
            and integer(item["owner"], 0, 2),
            "invalid opaque queue identity/ownership",
        )
    ids = [item["id"] for item in opaque]
    require(len(ids) == len(set(ids)), "duplicate opaque queue identity")
    balances = record.get("balances")
    require(
        type(balances) is list
        and len(balances) == 4
        and all(type(row) is list and len(row) == 2 and all(integer(x, 0) for x in row) for row in balances),
        "invalid four-resource balances",
    )
    progress = record.get("milestones")
    require(
        type(progress) is list and len(progress) == 3 and all(integer(x, 0) for x in progress),
        "invalid source milestones",
    )
    require(
        type(record.get("paused")) is bool
        and integer(record.get("player_status"), 0, 3)
        and integer(record.get("preorder"), 0, 0xFFFFFFFF),
        "invalid status/preorder",
    )
    order = record.get("logic_order")
    present = [a["id"] for a in actors if a["present"]]
    require(
        type(order) is list
        and all(integer(x, 1, 5) for x in order)
        and len(order) == len(set(order))
        and set(order) == {5, *present}
        and order[0] == 5,
        "ordered logic membership differs",
    )


def caller(base, owner, route):
    if route != "context":
        return f"game.player.capture_{route}_completion"
    return (
        "game.outcome.registered_base_capture"
        if base
        else "game.outcome.registered_capture" if owner == 0 else "game.outcome.registered_enemy_capture"
    )


def expected():
    for base in (False, True):
        for owner in (0, 2):
            for route in ("context", "keyboard", "button"):
                for rng in ("pm", "crt"):
                    for step in (10, 16):
                        kind = "base" if base else "factory"
                        ownership = "enemy" if owner else "neutral"
                        name = f"capture-command-{kind}-{ownership}-{route}-{rng}-{step}.jsonl"
                        yield name, (
                            base,
                            owner,
                            route,
                            rng,
                            step,
                        )


def identity(header, args, pin=None, reference=False):
    require(
        header["configuration"] == args.configuration and header["compiler"] == args.compiler,
        "compiler/configuration mismatch",
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
        integer(header.get("precision_control"), 0)
        and integer(header.get("dirty"), 0, 1)
        and type(header.get("revision")) is str
        and bool(header["revision"]),
        "missing build identity",
    )
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(header.get(key) in ("Original", "Corrected"), "missing compatibility mode: " + key)
    if args.revision or pin or reference:
        require(
            re.fullmatch(r"[0-9a-f]{40}", header["revision"]) is not None and header["dirty"] == 0,
            "strict evidence requires full clean compiled source identity",
        )
    if pin:
        require(header["revision"] == pin, "compiled source revision does not match pin")
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
            "place_containment",
            "zero_x_trace",
        )
    }


def validate(records, description, args, pin=None, reference=False):
    base, owner, route, rng, step = description
    header = records[0]
    profile = identity(header, args, pin, reference)
    require(
        header["fixture"] == ("base-capture-route-completion-v2" if base else "capture-route-completion-v1"),
        "fixture mismatch",
    )
    require(
        integer(header.get("kind"), 0 if base else 1, 0 if base else 1)
        and integer(header.get("initial_owner"), owner, owner)
        and header.get("route") == route
        and header.get("caller") == caller(base, owner, route),
        "episode identity mismatch",
    )
    require(
        header["rng"] == ("ParkMiller" if rng == "pm" else "LegacyCRT")
        and header["seed"] == 79
        and header["step_ms"] == step
        and header.get("max_frames") == 3000,
        "RNG/seed/step/bound mismatch",
    )
    require(
        header.get("audio") == "unavailable"
        and header.get("initially_selected") is True
        and header.get("navigation_layout") == "central-obstacle-v1",
        "fixture input policy differs",
    )
    require(
        header.get("render") == "storage-device-no-draw-cursor-surrogate"
        and header.get("viewport") == [1280, 720]
        and point(header["viewport"]),
        "render policy mismatch",
    )
    complete = (
        (5580 if step == 10 else 5600)
        if base
        else (18540 if step == 10 else 18544) if owner else (7530 if step == 10 else 7536)
    )
    required_progress = (
        [1280, 4330 if step == 10 else 4336, complete]
        if base
        else [630 if step == 10 else 640, (11030 if step == 10 else 11040) if owner else 0, complete]
    )
    frame_complete = complete // step
    last_frame = frame_complete + 100
    progress = [0, 0, 0]
    commands = []
    events = []
    phases = collections.Counter()
    ticks = []
    tails = []
    last = None
    last_draws = None
    selected = None
    for record in records[1:-1]:
        if record["type"] == "command":
            require(
                integer(record.get("frame"), 0, 0)
                and integer(record.get("time_ms"), 0, 0)
                and integer(record.get("target"), 5, 5),
                "wrong capture command boundary/target",
            )
            require(
                integer(record.get("rng_draws_before"), 0, 2**63 - 1)
                and integer(record.get("rng_draws_after"), record["rng_draws_before"], 2**63 - 1),
                "invalid command RNG",
            )
            require(record["rng_draws_before"] == last_draws, "command RNG before differs")
            last_draws = record["rng_draws_after"]
            position = record.get("trace_position")
            require(
                type(position) is list and len(position) == 3 and all(bits(x) for x in position),
                "missing typed trace input",
            )
            for value in position:
                floating(value)
            name = record["name"]
            expected_before, expected_after = (
                (0, PREORDER)
                if name in ("capture-key", "capture-button")
                else (PREORDER, 0) if name == "capture-confirm" else (0, 0)
            )
            require(
                integer(record.get("preorder_before"), 0, 0xFFFFFFFF)
                and integer(record.get("preorder_after"), 0, 0xFFFFFFFF)
                and record["preorder_before"] == expected_before
                and record["preorder_after"] == expected_after,
                "capture preorder transition differs",
            )
            if name == "capture-key":
                require(
                    integer(record.get("key"), 75, 75)
                    and type(record.get("pressed")) is list
                    and record["pressed"] == [True, False]
                    and all(type(x) is bool for x in record["pressed"]),
                    "keyboard input differs",
                )
            elif name == "capture-button":
                require(record.get("element") == "oca", "capture button identity differs")
            else:
                require(
                    record.get("button") == ("right" if name == "capture-context" else "left")
                    and record.get("client") == [0, 0]
                    and point(record["client"]),
                    "click input differs",
                )
            commands.append(record)
            events.append((name, 0))
            continue
        observations(record)
        state = record["state"]
        phase = record.get("phase")
        building = record["building"]
        actors = record["actors"]
        now = state["time_ms"]
        require(
            now == state["frame"] * step and all(x % step == 0 for x in record["milestones"]),
            "time/milestone alignment differs",
        )
        require(last_draws is None or state["rng_draws"] >= last_draws, "RNG moved backwards")
        if last is not None and last["state"]["frame"] == state["frame"]:
            require(state["rng_draws"] == last_draws, "same-boundary RNG mismatch")
        last_draws = state["rng_draws"]
        require(
            record["paused"] is False and record["player_status"] == 1 and record["preorder"] == 0,
            "capture status/input state differs",
        )
        require(building["kind"] == (0 if base else 1), "building kind changed")
        events.append((phase, state["frame"]))
        phases[phase] += 1
        if phase == "ready":
            require(
                last is None
                and now == 0
                and building["owner"] == owner
                and building["paint"] == (14 if owner else 0)
                and building["color"] == (0xFFFF0000 if owner else 0)
                and record["logic_order"] == [5, 1, 2, 3, 4],
                "initial building/list state differs",
            )
        elif phase == "command":
            require(now == 0, "command snapshot advanced time")
            assigned = [a["id"] for a in actors[:2] if a.get("capture_target") == 5]
            require(len(assigned) == 1, "initial capturer assignment differs")
            selected = assigned[0]
            require(
                all(a["group_order"] == 2 and a["group_target"] == 5 for a in actors[:2]),
                "initial group capture intention differs",
            )
        elif phase == "tick":
            ticks.append(state["frame"])
            if base:
                if not progress[0] and building["base_state"] == 2:
                    progress[0] = now
                if not progress[1] and progress[0] and building["base_state"] == 3:
                    progress[1] = now
                for boundary, expected_state in (
                    (1280 - step, 1),
                    (required_progress[1] - step, 2),
                    (complete - step, 3),
                ):
                    if now == boundary:
                        require(building["base_state"] == expected_state, "preceding base phase differs")
            else:
                if not progress[0] and building["paint"] != (14 if owner else 0):
                    progress[0] = now
                if owner and not progress[1] and building["owner"] == 0:
                    progress[1] = now
            if building["owner"] == 1:
                progress[2] = now
            if progress[2]:
                require(now == complete, "ownership transfer time differs")
                if base:
                    require(
                        building["base_state"] == 0 and building["capturer"] == 0,
                        "base transfer phase/capturer differs",
                    )
                    require(
                        not actors[selected - 1].get("alive", False) and actors[2 - selected].get("alive") is True,
                        "base capturer consumption differs",
                    )
                else:
                    require(
                        building["paint"] == 14 and building["color"] == 0xFF00FF00,
                        "factory completion paint/color differs",
                    )
        elif phase == "tail":
            tails.append(state["frame"])
            require(
                building["owner"] == 1 and (not base or building["base_state"] == 0),
                "ownership/base phase changed in tail",
            )
        elif phase == "final":
            require(
                building["owner"] == 1 and (not base or building["base_state"] == 0),
                "final ownership/base state differs",
            )
            if not base:
                require(
                    actors[selected - 1].get("alive") is True and all(q[0] != 7 for q in actors[selected - 1]["queue"]),
                    "factory capture order not released",
                )
        else:
            require(False, "unknown capture observation phase")
        require(record["milestones"] == progress, "source milestone schedule differs")
        if base:
            transferred = building["owner"] == 1
            expected_queue = [{"id": 6, "owner": 2}, {"id": 7, "owner": 2}] if owner and not transferred else []
            require(building["queue"] == expected_queue, "opaque queue order/lifetime differs")
            for resource, row in enumerate(record["balances"]):
                require(
                    row == [200, 100 + (2 * (11 + resource) if owner and transferred else 0)],
                    "base resource/refund ownership differs",
                )
        else:
            require(building["queue"] == [], "factory acquired an unexpected queue")
        last = record
    require(progress == required_progress, "measured capture milestones differ")
    require(
        ticks == list(range(1, frame_complete + 1)) and tails == list(range(frame_complete + 1, last_frame + 1)),
        "missing/extra map boundary or changed100-tick tail",
    )
    names = (
        ["capture-context"]
        if route == "context"
        else ["capture-key" if route == "keyboard" else "capture-button", "capture-confirm"]
    )
    require([r["name"] for r in commands] == names, "capture input schedule differs")
    if len(commands) == 2:
        require(
            commands[0]["trace_position"] == commands[1]["trace_position"], "selection/confirmation target ray differs"
        )
    require(
        phases == {"ready": 1, "command": 1, "tick": frame_complete, "tail": 100, "final": 1},
        "missing/extra capture phase",
    )
    schedule = (
        [("ready", 0)]
        + [(name, 0) for name in names]
        + [("command", 0)]
        + [("tick", i) for i in range(1, frame_complete + 1)]
        + [("tail", i) for i in range(frame_complete + 1, last_frame + 1)]
        + [("final", last_frame)]
    )
    require(events == schedule, "action/observation ordering differs")
    tail = records[-1]
    require(
        tail["frame"] == last_frame
        and tail["time_ms"] == last_frame * step
        and integer(tail.get("rng_draws"), 0, 2**63 - 1)
        and tail["rng_draws"] == last["state"]["rng_draws"],
        "terminal boundary/RNG differs",
    )
    return profile


def check_directory(directory, args, reference=False):
    inventory = list(expected())
    names = {n for n, _ in inventory}
    require({p.name for p in directory.glob("capture-command-*.jsonl")} == names, "finite48-file inventory differs")
    common = None
    pin = args.reference_revision if reference else args.revision
    for name, description in inventory:
        with (directory / name).open(encoding="utf-8") as stream:
            records = TRACES.read_records(stream, name)
        profile = validate(records, description, args, pin, reference)
        require(common is None or common == profile, "incoherent corpus identity/effective profile")
        common = profile
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
    parser.add_argument("--cheats", choices=("ON", "OFF"), required=True)
    parser.add_argument("--trace", choices=("ON", "OFF"), required=True)
    args = parser.parse_args(argv)
    args.cheats = args.cheats == "ON"
    args.trace = args.trace == "ON"
    try:
        for pin in (args.revision, args.reference_revision):
            require(
                pin is None or re.fullmatch(r"[0-9a-f]{40}", pin) is not None, "revision pin must be full lowercase hex"
            )
        require(not args.reference_revision or args.reference is not None, "reference pin requires directory")
        if not args.revision:
            print("Diagnostic candidate: no compiled revision pin supplied.", file=sys.stderr)
        if args.reference and not args.reference_revision:
            print("Diagnostic historical reference: unpinned; not accepted baseline evidence.", file=sys.stderr)
        reference = check_directory(args.reference, args, True) if args.reference else None
        count = 0
        for name, records in check_directory(args.directory, args):
            if reference:
                other_name, other = next(reference)
                require(other_name == name, "reference inventory differs")
                difference = TRACES.compare_records(records, other)
                require(difference is None, name + ": " + str(difference))
            count += 1
        print(f"Validated {count} registered capture recordings; no invented replay/route equalities")
        return 0
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, TypeError, StopIteration) as error:
        print(f"Invalid capture recordings: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
