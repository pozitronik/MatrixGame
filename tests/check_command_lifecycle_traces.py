"""Check the four existing command-lifecycle cases and their finite 76-file corpus."""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys


spec = importlib.util.spec_from_file_location(
    "c2_trace_reader", Path(__file__).resolve().parents[1] / "tools/compare-battle-traces.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def require(condition, message):
    if not condition:
        raise trace.TraceError(message)


def integer(value, minimum=-(1 << 31), maximum=(1 << 31) - 1):
    return type(value) is int and minimum <= value <= maximum


def bits(value):
    return integer(value, 0, 0xffffffff)


def number(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def inventory():
    for family in ("mixed", "spawning", "carried", "dying"):
        for suffix, rng in (("pm", "ParkMiller"), ("crt", "LegacyCRT")):
            for step in (10, 16):
                for flag in ((False,) if family == "dying" else (False, True)):
                    for route in ((-1,) if family == "dying" else (0, 1, 2)):
                        name = f"player-command-{family}-{suffix}-{step}"
                        if route >= 0:
                            variant = ("button" if flag else "key") if family == "mixed" else (
                                "stop" if flag else "keep")
                            name += f"-{variant}-route{route}"
                        yield name + ".jsonl", dict(family=family, rng=rng, step=step, route=route, flag=flag)


def queue(value, width, maximum):
    require(isinstance(value, list) and len(value) <= 5, "queue must be an array of at most five orders")
    for order in value:
        require(isinstance(order, list) and len(order) == width, "invalid queue row width")
        require(all(integer(order[i], 0, 8) for i in (0, 1)), "invalid physical order type or phase")
        require(all(bits(order[i]) for i in range(2, width - 1 if width == 5 else 5)), "invalid order bits")
        if width == 7:
            require(integer(order[5]), "invalid order parameter4")
        require(integer(order[-1], -1, maximum), "invalid normalized order object")


def actor(value):
    require(isinstance(value, dict) and integer(value.get("id"), 1, 5), "invalid actor identity")
    require(type(value.get("present")) is bool, "actor presence missing")
    if not value["present"]:
        require(set(value) == {"id", "present"}, "retired actor exposes stale state")
        return
    require(type(value.get("alive")) is bool and integer(value.get("state"), 0, 7), "invalid actor lifetime state")
    if not value["alive"]:
        require(value["state"] == 5 and set(value) == {"id", "present", "alive", "state"},
                "dying actor exposes inactive state")
        return
    for key in ("in_place", "has_base"):
        require(type(value.get(key)) is bool, "invalid actor " + key)
    for key in ("state", "group", "place"):
        require(integer(value.get(key)), "invalid actor " + key)
    require(integer(value.get("chassis"), 0, 5), "invalid live chassis")
    require(integer(value["state"], 0, 7) and integer(value["group"], -1, 60), "actor state/group outside domain")
    for key in ("x_bits", "y_bits", "maximum_speed_bits"):
        require(bits(value.get(key)), "invalid actor " + key)
    require(isinstance(value.get("map"), list) and len(value["map"]) == 2
            and all(integer(x) for x in value["map"]), "invalid actor map position")
    require("move_destination" in value and (value["move_destination"] is None
            or isinstance(value["move_destination"], list) and len(value["move_destination"]) == 2
            and all(integer(x) for x in value["move_destination"])), "missing unit Move destination")
    queue(value.get("queue"), 7, 5)


def observation(record, step):
    state, custom = record.get("state"), record.get("c2")
    require(isinstance(state, dict) and isinstance(custom, dict), "missing lifecycle observations")
    require(integer(state.get("frame"), 0) and state.get("time_ms") == state["frame"] * step,
            "invalid whole-frame clock")
    require(type(state.get("time_ms")) is int and integer(state.get("rng_draws"), 0, (1 << 64) - 1),
            "invalid clock or RNG count")
    robots = state.get("robots")
    require(isinstance(robots, list) and [r.get("id") for r in robots if isinstance(r, dict)] == [1, 2, 3, 4],
            "Session actor inventory differs")
    for robot in robots:
        require(type(robot.get("alive")) is bool and type(robot["id"]) is int, "invalid Session actor")
        if robot["alive"]:
            for key in ("x_bits", "y_bits", "health_bits"):
                require(bits(robot.get(key)), "missing Session " + key)
            for key in ("side", "state", "group", "order", "target"):
                require(integer(robot.get(key)), "missing Session " + key)
            require(integer(robot["order"], -1, 9) and integer(robot["target"], -1, 4)
                    and integer(robot["group"], -1, 60), "Session group intent or identity outside domain")
            require(type(robot.get("in_place")) is bool and type(robot.get("target_is_null")) is bool,
                    "invalid Session flags")
            require(isinstance(robot.get("destination"), list) and len(robot["destination"]) == 2
                    and all(integer(x) for x in robot["destination"]), "invalid Session destination")
            queue(robot.get("queue"), 5, 4)
        else:
            require(set(robot) == {"id", "alive"}, "retired Session actor retains state")
    require(isinstance(state.get("player_groups"), list), "missing Session groups")
    for group in state["player_groups"]:
        require(isinstance(group, dict) and all(integer(group.get(k))
                for k in ("slot", "stored_count", "members", "order", "target")), "invalid Session group")
        require(integer(group["slot"], 0, 60) and integer(group["order"], 0, 9)
                and integer(group["target"], -1, 4), "Session group order or identity outside domain")
    actors = custom.get("actors")
    require(isinstance(actors, list) and len(actors) in (4, 5), "missing extended actors")
    for value in actors:
        actor(value)
    require([value["id"] for value in actors] == list(range(1, len(actors) + 1)), "extended actor roles differ")
    require(isinstance(custom.get("phase"), str) and custom["phase"], "missing observation phase")
    selection = custom.get("selection")
    require(isinstance(selection, list) and all(integer(x, 1, len(actors)) for x in selection)
            and len(selection) == len(set(selection)), "invalid ordered selection")
    require(all(actors[entity - 1]["present"] and actors[entity - 1].get("alive") for entity in selection),
            "selection retains a dead or retired actor")
    groups = custom.get("groups")
    require(isinstance(groups, list), "missing extended groups")
    slots = []
    for group in groups:
        require(isinstance(group, dict) and all(integer(group.get(k))
                for k in ("slot", "stored_count", "members", "order", "target")), "invalid extended group")
        require(integer(group["slot"], 0, 60), "invalid group slot")
        require(integer(group["order"], 0, 9) and integer(group["target"], -1, 5),
                "extended group order or identity outside domain")
        require(isinstance(group.get("destination"), list) and len(group["destination"]) == 2
                and all(integer(x) for x in group["destination"]), "missing group destination")
        slots.append(group["slot"])
    require(slots == sorted(set(slots)), "group slots duplicated or unordered")
    require(type(custom.get("carrier_present")) is bool, "missing carrier lifetime")
    require(integer(custom.get("carried_actor"), 0, 5) and integer(custom.get("watched_core"), 0, 5)
            and integer(custom.get("enemy_kills"), 0), "missing lifetime or kill observations")
    require(custom["carrier_present"] or custom["carried_actor"] == 0, "retired carrier retains cargo")
    return actors


def group_for(record, actor_id):
    value = record["c2"]["actors"][actor_id - 1]
    require(value["present"], "required actor has retired")
    groups = [group for group in record["c2"]["groups"] if group["slot"] == value["group"]]
    require(len(groups) == 1, "required actor group missing")
    return groups[0]


def schedule(profile):
    family, step, route, flag = (profile[k] for k in ("family", "step", "route", "flag"))
    rounded = lambda duration: (duration + step - 1) // step * step
    if family == "mixed":
        stop = rounded(1000)
        last_move = stop + rounded(6000) + rounded(1000)
        return [("unrelated-move", 0), (f"move-{route}-500-300", 0),
                ("stop-button" if flag else "stop-key", stop), (f"move-{route}-500-100", last_move)], (
                    last_move + rounded(16000))
    if family in ("spawning", "carried"):
        last_move = rounded(30000) + rounded(1000) if flag else rounded(18000)
        commands = [("unrelated-move", 0), (f"move-{route}-500-100", 0)]
        if flag:
            commands.append(("stop-button" if route == 2 else "stop-key", 0))
        return commands + [(f"move-{route}-120-500", last_move)], last_move + rounded(16000)
    hit = rounded(500)
    commands = [("move-0-500-100", 0), ("lethal-damage", hit)]
    for current in range(3):
        commands.extend([(f"move-{current}-120-500", hit),
                         ("stop-button" if current == 2 else "stop-key", hit)])
    last_move = hit + rounded(6000)
    return commands + [("move-2-120-500", last_move)], last_move + rounded(16000)


def movement_contract(snapshots, profile, commands, ready):
    family, step, flag = (profile[k] for k in ("family", "step", "flag"))
    if family == "dying":
        return
    unrelated = None
    final_move = commands[-1][0]
    after_initial = False
    replaced = False
    stopped = None
    settled = None
    before_replacement = None
    for record in snapshots:
        phase, time = record["c2"]["phase"], record["state"]["time_ms"]
        if phase == "unrelated-move":
            unrelated = group_for(record, 2)["slot"]
        if unrelated is not None:
            group = group_for(record, 2)
            require(group["slot"] == unrelated and group["order"] == 1 and group["destination"] == [8, 48],
                    "unrelated group changed")
        if phase == commands[1][0]:
            after_initial = True
            if family == "mixed":
                require(group_for(record, 1)["slot"] == group_for(record, 5)["slot"]
                        and group_for(record, 1)["destination"] == [48, 28], "mixed Move assignment differs")
            else:
                require(group_for(record, 5)["order"] == 1
                        and group_for(record, 5)["destination"] == [48, 8], "nonready Move assignment differs")
        if phase in ("stop-key", "stop-button"):
            stopped = record
            if family == "mixed":
                require(all(number(record["c2"]["actors"][i]["x_bits"]) > 110 for i in (0, 4)),
                        "mixed initial physical progress missing")
        if phase == final_move:
            replaced = True
        if not after_initial or replaced:
            continue
        before_replacement = record
        if family == "mixed" and stopped is not None:
            for actor_id in (1, 5):
                require(group_for(record, actor_id)["order"] == 0, "mixed Stop intent lost")
            settle_time = commands[2][1] + 6000
            if time == settle_time:
                settled = record
                require(all(number(record["c2"]["actors"][i]["x_bits"]) < 400 for i in (0, 4)),
                        "mixed Stop settling bound differs")
        elif family in ("spawning", "carried"):
            group = group_for(record, 5)
            if not flag:
                require(group["order"] == 1, "nonready Move intent lost")
            elif stopped is not None:
                stop_place = stopped["c2"]["actors"][4]["map"]
                require(group["order"] == 0 and group["destination"] == stop_place,
                        "nonready Stop intent or destination changed")
                value = record["c2"]["actors"][4]
                if time >= ready:
                    require(value["map"] != [48, 8] and value["move_destination"] != [48, 8],
                            "canceled Move destination resumed")
                if phase == "tick" and time == 30000:
                    settled = record
                if phase == "tick" and time > 30000 and settled is not None:
                    require((value["x_bits"], value["y_bits"]) == (
                        settled["c2"]["actors"][4]["x_bits"], settled["c2"]["actors"][4]["y_bits"]),
                        "nonready Stop stationary interval changed")
    require(before_replacement is not None, "pre-replacement lifecycle state missing")
    if family == "mixed":
        require(settled is not None, "mixed settling boundary missing")
        for index in (0, 4):
            require(tuple(before_replacement["c2"]["actors"][index][key] for key in ("x_bits", "y_bits"))
                    == tuple(settled["c2"]["actors"][index][key] for key in ("x_bits", "y_bits")),
                    "mixed stationary endpoints differ")
    else:
        value = before_replacement["c2"]["actors"][4]
        require(not value["has_base"], "ready actor retains base")
        if not flag:
            require(value["in_place"] and number(value["x_bits"]) > 400 and number(value["y_bits"]) < 200,
                    "retained Move did not arrive")
        elif family == "carried":
            stop_place = stopped["c2"]["actors"][4]["map"]
            require(stop_place == [0, 0], "carried Stop origin differs")
            if step == 10:
                require(not value["in_place"] and value["map"][0] == 46 and 44 <= value["map"][1] <= 45,
                        "bounded carried Stop non-arrival differs")
            else:
                require(value["in_place"] and number(value["x_bits"]) == 40 and number(value["y_bits"]) == 40,
                        "carried Stop arrival differs")


def validate(records, profile, *, revision=None, configuration=None, compiler=None, target=None,
             cheats=None, traced=None):
    family, step, route, flag = (profile[k] for k in ("family", "step", "route", "flag"))
    header = records[0]
    fixture = "spawn" if family == "spawning" else family
    expected = dict(fixture=f"{fixture}-command-lifecycle-v1", c2_observation="command-lifecycle-v1",
                    family=family, rng=profile["rng"], seed=79, step_ms=step, max_frames=6000,
                    initially_selected=False, audio="unavailable", navigation_layout="open-v1",
                    move_route=route, flag=flag, extra_actor=0 if family == "dying" else 5)
    for key, value in (("revision", revision), ("configuration", configuration), ("compiler", compiler),
                       ("build_exe", None if target is None else target == "EXE"),
                       ("cheats", cheats), ("trace", traced)):
        if value is not None:
            expected[key] = value
    for key, value in expected.items():
        require(type(header.get(key)) is type(value) and header[key] == value, "header." + key + " differs")
    require(type(header.get("trace")) is bool, "missing compiled trace profile")
    require(isinstance(header.get("revision"), str) and header["revision"]
            and type(header.get("dirty")) is int and header["dirty"] in (0, 1), "invalid source provenance")
    require(type(header.get("build_exe")) is bool and type(header.get("cheats")) is bool,
            "missing compiled role or cheats profile")
    if revision is not None:
        require(type(header.get("dirty")) is int and header["dirty"] == 0, "dirty source identity")
    commands, end = schedule(profile)
    actual_commands = [record for record in records if record["type"] == "command"]
    require([(r["name"], r.get("time_ms")) for r in actual_commands] == commands, "command schedule differs")
    for record in actual_commands:
        require(type(record.get("time_ms")) is int and type(record["frame"]) is int
                and record["frame"] * step == record["time_ms"], "invalid command clock")
        require(integer(record.get("rng_draws_before"), 0, (1 << 64) - 1)
                and integer(record.get("rng_draws_after"), 0, (1 << 64) - 1)
                and record["rng_draws_before"] <= record["rng_draws_after"], "invalid command RNG boundary")
    snapshots = [record for record in records if record["type"] == "snapshot"]
    require(snapshots and snapshots[0]["c2"]["phase"] == "session-ready"
            and snapshots[-1]["c2"]["phase"] == "final", "missing lifecycle endpoints")
    require(len(snapshots[0]["c2"]["actors"]) == 4 and snapshots[0]["state"]["frame"] == 0,
            "initial actor boundary differs")
    ticks = []
    by_phase = {}
    last_draws = 0
    for index, record in enumerate(records[1:-1], 1):
        if record["type"] == "command":
            require(record["rng_draws_before"] >= last_draws, "command RNG moves backwards")
            following = records[index + 1]
            require(following["type"] == "snapshot" and following.get("c2", {}).get("phase") == record["name"]
                    and following["state"]["frame"] == record["frame"]
                    and following["state"]["rng_draws"] == record["rng_draws_after"],
                    "command result boundary missing")
            continue
        actors = observation(record, step)
        last_draws = record["state"]["rng_draws"]
        phase = record["c2"]["phase"]
        require(len(actors) == (4 if family == "dying" or phase == "session-ready" else 5),
                "extra actor lifetime inventory differs")
        require(phase in {"session-ready", "extra-created", "selection", "tick", "final"}
                or phase in {name for name, _ in commands}, "unknown lifecycle phase")
        by_phase.setdefault(phase, []).append(record)
        if phase == "tick":
            ticks.append(record["state"]["frame"])
        require(all(value["present"] and value["alive"] for value in actors[1:]), "unexpected actor retirement")
        if family != "dying":
            require(actors[0]["present"] and actors[0]["alive"], "unexpected original actor death")
    require(ticks == list(range(1, end // step + 1)), "whole-frame observations missing or duplicated")
    require(len(by_phase.get("session-ready", [])) == 1 and len(by_phase.get("final", [])) == 1,
            "duplicate lifecycle endpoints")
    selections = by_phase.get("selection", [])
    expected_selection = [[1, 2], [2]] if family == "dying" else [[2], [1, 5] if family == "mixed" else [5]]
    require([r["c2"]["selection"] for r in selections] == expected_selection, "selection schedule differs")
    require(records[-1]["frame"] == end // step and records[-1]["time_ms"] == end, "wrong completion horizon")
    tail = records[-1].get("c2")
    require(isinstance(tail, dict) and all(integer(tail.get(k)) for k in ("intermediate_ms", "ready_ms")),
            "completion milestones missing")
    final = snapshots[-1]
    if family in ("spawning", "carried"):
        expected_states = (0, 1) if family == "spawning" else (3, 4)
        require(len(by_phase.get("extra-created", [])) == 1, "extra actor creation missing")
        created = by_phase["extra-created"][0]["c2"]["actors"][-1]
        require(created["id"] == 5 and created["state"] == expected_states[0], "wrong initial lifecycle state")
        first_intermediate = next((r["state"]["time_ms"] for r in snapshots
                                   if len(r["c2"]["actors"]) == 5
                                   and r["c2"]["actors"][4].get("state") == expected_states[1]), -1)
        first_ready = next((r["state"]["time_ms"] for r in snapshots
                           if len(r["c2"]["actors"]) == 5 and r["c2"]["actors"][4].get("state") == 2), -1)
        require(0 < first_intermediate < first_ready
                and (tail["intermediate_ms"], tail["ready_ms"]) == (first_intermediate, first_ready),
                "lifecycle milestones differ")
    else:
        require(tail == dict(intermediate_ms=-1, ready_ms=-1), "unexpected lifecycle milestones")
    movement_contract(snapshots, profile, commands, tail["ready_ms"])
    if family == "mixed":
        require(len(by_phase.get("extra-created", [])) == 1, "track creation missing")
        created = by_phase["extra-created"][0]["c2"]["actors"]
        require(created[0]["chassis"] == 2 and created[4]["chassis"] == 3
                and created[0]["maximum_speed_bits"] == struct.unpack("<I", struct.pack("<f", .6))[0]
                and created[4]["maximum_speed_bits"] == struct.unpack("<I", struct.pack("<f", .5))[0],
                "mixed chassis/speed observations differ")
        stopped = by_phase["stop-button" if flag else "stop-key"][0]
        pools = sum(len(stopped["c2"]["actors"][i]["queue"]) for i in (0, 4))
        require((pools > 0) if flag else pools == 0, "Stop route queue distinction lost")
        for actor_id in (1, 5):
            value = final["c2"]["actors"][actor_id - 1]
            require(value["in_place"] and number(value["x_bits"]) > 400 and number(value["y_bits"]) < 200,
                    "mixed final arrival missing")
            require(group_for(final, actor_id)["order"] == 1, "mixed final Move intent missing")
    elif family == "dying":
        dead = by_phase["lethal-damage"][0]
        require(dead["c2"]["actors"][0]["present"] and not dead["c2"]["actors"][0]["alive"]
                and dead["c2"]["actors"][0]["state"] == 5
                and dead["c2"]["watched_core"] == 1, "dying retained-core boundary missing")
        require(not final["c2"]["actors"][0]["present"] and final["c2"]["watched_core"] == 0
                and final["c2"]["enemy_kills"] == 1 and not final["state"]["robots"][0]["alive"],
                "retirement or kill attribution missing")
        require(final["c2"]["actors"][1]["in_place"]
                and number(final["c2"]["actors"][1]["y_bits"]) > 450, "survivor recovery missing")
    else:
        value = final["c2"]["actors"][4]
        require(value["in_place"] and not value["has_base"]
                and number(value["x_bits"]) < 250 and number(value["y_bits"]) > 450,
                "fresh-command recovery missing")
        require(group_for(final, 5)["order"] == 1
                and group_for(final, 5)["destination"] == [10, 48], "fresh Move intent missing")
    if family != "dying":
        require(number(final["c2"]["actors"][1]["y_bits"]) > 450
                and group_for(final, 2)["order"] == 1
                and group_for(final, 2)["destination"] == [8, 48], "unrelated group lost its Move")
    # This is precisely the original pre-Stop comparison, not whole-route equality.
    prefix = []
    if family == "mixed":
        first_move = f"move-{route}-500-300"
        for record in snapshots:
            phase, frame = record["c2"]["phase"], record["state"]["frame"]
            if phase == first_move or (phase == "tick" and 0 < frame < commands[2][1] // step):
                track = record["c2"]["actors"][4]
                prefix.append([record["state"], [track["x_bits"], track["y_bits"], track["group"],
                                               len(track["queue"]), track["place"], track["in_place"]]])
        require(len(prefix) == (1000 + step - 1) // step, "mixed comparison window missing")
    return prefix


def read(path):
    with path.open(encoding="utf-8") as stream:
        return trace.read_records(stream, str(path))


def check_directory(directory, *, reference=None, **identity):
    selections = list(inventory())
    expected = {name for name, _ in selections}
    require({p.name for p in directory.glob("player-command-*.jsonl")} == expected, "C2 finite inventory differs")
    if reference is not None:
        require({p.name for p in reference.glob("player-command-*.jsonl")} == expected, "reference inventory differs")
    context, prefix, comparisons = None, None, 0
    for name, profile in selections:
        records = read(directory / name)
        current = validate(records, profile, **identity)
        if reference is not None:
            old = read(reference / name)
            validate(old, profile, **{k: v for k, v in identity.items() if k != "revision"})
            difference = trace.compare_records(old, records)
            require(difference is None, f"{name}: reference divergence: {difference}")
        if profile["family"] == "mixed":
            key = (profile["rng"], profile["step"], profile["flag"])
            if profile["route"] == 0:
                context, prefix = key, current
            else:
                require(context == key and current == prefix, f"{name}: pre-Stop route divergence")
                comparisons += 1
    return len(expected), comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", choices=("true", "false"))
    parser.add_argument("--trace", choices=("true", "false"), dest="traced")
    args = parser.parse_args()
    for key in ("cheats", "traced"):
        value = getattr(args, key)
        setattr(args, key, None if value is None else value == "true")
    try:
        files, comparisons = check_directory(**vars(args))
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f"Command-lifecycle validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {files} command-lifecycle recordings and {comparisons} existing pre-Stop route comparisons.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
