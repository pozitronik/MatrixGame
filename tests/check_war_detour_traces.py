"""Validate the existing successful War detour selection; no baseline acceptance is implied."""
import argparse
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys

SPEC = importlib.util.spec_from_file_location(
    "battle_traces", Path(__file__).parents[1] / "tools/compare-battle-traces.py")
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)
CHANGES = ("control", "retirement", "replacement")
RESULT_FIELDS = {"war", "detour", "reached_firing_place", "first_hit", "terminal", "changed", "resumed", "arrival"}
PROFILE_FIELDS = ("revision", "dirty", "configuration", "compiler", "compiler_version", "precision_control",
                  "assert_off", "build_exe", "cheats", "trace", "delayed_damage", "group_counts", "place_containment",
                  "zero_x_trace", "audio", "render", "viewport")


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=-0x80000000, maximum=0x7fffffff):
    return TRACES._integer(value, minimum=minimum, maximum=maximum)


def bits(value):
    return integer(value, 0, 0xffffffff)


def floating(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(v) for v in value)


def object_id(value):
    return integer(value, -1, 4)


def inventory():
    for caller in CHANGES:
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for role in (("first", "replay") if caller == "control" else ("first", "replay", "intact")):
                    yield f"war-detour-{caller}-{rng}-{step}-{role}.jsonl", dict(
                        caller=caller, rng=rng, step=step, role=role,
                        change="control" if role == "intact" else caller)


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def profile(revision=None, configuration=None, compiler=None, target=None, cheats=None, trace=None):
    values = revision, configuration, compiler, target, cheats, trace
    if all(value is None for value in values):
        return None
    require(all(value is not None for value in values),
            "supply revision, configuration, compiler, target, cheats and trace")
    require(full_revision(revision), "revision must be a full lowercase 40-hex commit ID")
    require(configuration in ("Debug", "Release") and compiler in ("MinGW", "MSVC"), "invalid build profile")
    require(target in ("EXE", "DLL") and type(cheats) is bool and type(trace) is bool,
            "invalid role/cheats/trace profile")
    return dict(configuration=configuration, compiler=compiler, build_exe=target == "EXE", cheats=cheats,
                trace=trace, dirty=0)


def validate_profile(header, expected, revision):
    if expected is None:
        return
    require(full_revision(header.get("revision")), "missing/invalid compiled revision")
    for key, value in dict(expected, **({"revision": revision} if revision is not None else {})).items():
        require(type(header.get(key)) is type(value) and header[key] == value, "compiled profile mismatch: " + key)


def header_contract(header, spec):
    expected = dict(fixture="reachable-war-detours-v1", recording="successful-war-detour-v1",
                    navigation_layout="open-v1", caller=spec["caller"], role=spec["role"], change=spec["change"],
                    rng="ParkMiller" if spec["rng"] == "pm" else "LegacyCRT", seed=84, step_ms=spec["step"],
                    max_frames=24000 // spec["step"], change_time_ms=800, initial_leg=[48, 8], patrol_to=[48, 38],
                    initially_selected=True, viewport=[1280, 720], audio="unavailable",
                    render="storage-device-no-draw-cursor-surrogate")
    for key, value in expected.items():
        require(type(header.get(key)) is type(value) and header[key] == value, "fixture/header mismatch: " + key)
    require(all(point(header[k]) for k in ("initial_leg", "patrol_to", "viewport")), "invalid integer header point")
    for key in ("revision", "delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(type(header.get(key)) is str and bool(header[key]), "profile metadata missing: " + key)
    require(all(header[k] in ("Original", "Corrected") for k in (
        "delayed_damage", "group_counts", "place_containment", "zero_x_trace")), "invalid compatibility identity")
    require(header.get("configuration") in ("Debug", "Release"), "invalid configuration")
    compiler, version = header.get("compiler"), header.get("compiler_version")
    require(compiler in ("MinGW", "MSVC"), "invalid compiled compiler")
    require(integer(version, 1) if compiler == "MSVC" else type(version) is str and bool(version),
            "invalid compiled compiler_version")
    require(type(header.get("dirty")) is int and header["dirty"] in (-1, 0, 1), "invalid source dirty marker")
    require(integer(header.get("precision_control"), 0), "invalid precision control")
    require(all(type(header.get(key)) is bool for key in ("assert_off", "build_exe", "cheats", "trace")),
            "invalid compiled boolean")


def observation(record):
    state = record.get("state")
    require(type(state) is dict and type(state.get("robots")) is list, "missing Session observations")
    require(integer(state.get("rng_draws"), 0, 0xffffffffffffffff), "invalid RNG counter")
    robots = state["robots"]
    require(len(robots) == 4 and all(type(r) is dict for r in robots) and
            [r.get("id") for r in robots] == [1, 2, 3, 4] and all(type(r["id"]) is int for r in robots),
            "invalid Session actor inventory")
    for robot in robots:
        require(type(robot.get("alive")) is bool, "invalid Session live flag")
        if not robot["alive"]:
            require(set(robot) == {"id", "alive"}, "dead Session actor contains live data")
            continue
        fields = {"x_bits", "y_bits", "health_bits", "side", "state", "in_place", "group", "order", "target",
                  "target_is_null", "destination", "queue"}
        require(fields <= robot.keys(), "missing live Session fields")
        require(all(bits(robot[k]) for k in ("x_bits", "y_bits", "health_bits")), "invalid Session float bits")
        require(all(integer(robot[k]) for k in ("side", "group")) and integer(robot["state"], 0, 7) and
                robot["state"] != 5 and integer(robot["order"], -1, 9), "invalid Session integer/order")
        require(all(type(robot[k]) is bool for k in ("in_place", "target_is_null")), "invalid Session boolean")
        require(object_id(robot["target"]) and point(robot["destination"]), "invalid Session target/destination")
        require(type(robot["queue"]) is list and len(robot["queue"]) <= 5, "invalid Session order queue")
        for order in robot["queue"]:
            require(type(order) is list and len(order) == 5 and all(integer(order[i], 0, 8) for i in (0, 1)) and
                    all(bits(order[i]) for i in (2, 3)) and object_id(order[4]), "invalid five-field Session order")
    groups = state.get("player_groups")
    require(type(groups) is list, "missing Session player groups")
    slots = []
    for group in groups:
        require(type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys(),
                "missing Session group fields")
        require(all(integer(group[k], 0) for k in ("slot", "members")) and integer(group["stored_count"]) and
                integer(group["order"], 0, 9) and object_id(group["target"]), "invalid Session group values")
        slots.append(group["slot"])
    require(len(slots) == len(set(slots)), "duplicate Session group slot")
    value = record.get("war_detour")
    fields = {"moving", "move", "fire_order", "environment_target", "attack_target", "previous_firing_target",
              "pulses", "last_health_bits", "firing_place", "target_core", "full_orders"}
    require(type(value) is dict and fields <= value.keys(), "missing War detour observations")
    require(all(type(value[k]) is bool for k in ("moving", "fire_order")), "invalid movement/fire flag")
    require(point(value["move"]) and point(value["firing_place"]), "invalid physical place coordinates")
    require(value["moving"] or value["move"] == [-1, -1], "stationary movement coordinates must be sentinel")
    require(all(object_id(value[k]) for k in ("environment_target", "attack_target", "previous_firing_target")),
            "invalid normalized environment identity")
    require(integer(value["pulses"], 0, 100) and bits(value["last_health_bits"]), "invalid damage observations")
    core = value["target_core"]
    require(type(core) is dict and {"present", "id", "live"} <= core.keys() and
            all(type(core[k]) is bool for k in ("present", "live")), "missing target-core lifetime")
    require(type(core["id"]) is int and core["id"] == (3 if core["present"] else 0), "target-core identity differs")
    require(core["live"] == robots[2]["alive"], "target-core and Session lifetime differ")
    if core["present"]:
        require(integer(core.get("state"), 0, 7) and type(core.get("must_die")) is bool, "missing core scalar state")
        require(core["live"] == (core["state"] != 5), "DIP must differ from live target")
        if core["live"]:
            require(bits(core.get("health_bits")) and core["health_bits"] == robots[2]["health_bits"],
                    "core/Session health differs")
        else:
            require("health_bits" not in core, "DIP must not include live fields")
    else:
        require(set(core) == {"present", "id", "live"} and not core["live"], "retired core contains object data")
    full = value["full_orders"]
    require(type(full) is list and len(full) == 4, "missing full order inventory")
    for robot, entry in zip(robots, full):
        require(type(entry) is dict and type(entry.get("id")) is int and entry["id"] == robot["id"] and
                type(entry.get("live")) is bool and entry["live"] == robot["alive"], "full order identity differs")
        if not entry["live"]:
            require(set(entry) == {"id", "live"}, "dead actor must not expose order storage")
            continue
        orders = entry.get("orders")
        require(type(orders) is list and len(orders) <= 5, "invalid full order queue")
        for order in orders:
            require(type(order) is list and len(order) == 7 and all(integer(order[i], 0, 8) for i in (0, 1)) and
                    all(bits(order[i]) for i in (2, 3, 4)) and integer(order[5]) and object_id(order[6]),
                    "invalid seven-field initialized order")
        require([[o[0], o[1], o[2], o[3], o[6]] for o in orders] == robot["queue"], "full/Session order differs")
    movement = [order for order in full[0]["orders"] if order[0] == 1]
    require(bool(movement) == value["moving"], "physical movement flag/queue differ")
    if movement:
        # Source GetMoveToCoords returns the first MoveTo order's integer coordinates.
        require([floating(movement[0][i]) for i in (2, 3)] == value["move"], "physical movement order differs")
    require(any(order[0] == 5 for order in full[0]["orders"]) == value["fire_order"], "fire flag/queue differ")
    return value


def validate(records, spec):
    header_contract(records[0], spec)
    step, change = spec["step"], spec["change"]
    index, previous = 1, None
    ticks = []
    def snapshot(phase, frame):
        nonlocal index, previous
        require(index < len(records), "missing snapshot")
        record = records[index]
        require(record.get("type") == "snapshot" and record.get("phase") == phase, "snapshot phase/order differs")
        state = record["state"]
        require(type(state["frame"]) is int and state["frame"] == frame and
                type(state["time_ms"]) is int and state["time_ms"] == frame * step, "snapshot frame/time differs")
        observation(record)
        previous = record
        index += 1
        return record
    def command(name, frame):
        nonlocal index
        require(index < len(records), "missing authored command")
        record = records[index]
        patrol, remove = name == "setup-patrol", name == "target-retirement"
        expected = dict(type="command", name=name, frame=frame, time_ms=frame * step,
                        route="player-group" if patrol else "delete" if remove else "context-ground",
                        target=3 if remove else 0,
                        point=[48, 38] if patrol else [-1, -1] if remove else
                        [500, 100] if name == "setup-move" else [500, 400])
        for key, value in expected.items():
            require(type(record.get(key)) is type(value) and record[key] == value, "command field differs: " + key)
        require(point(record["point"]), "command coordinates must be integers")
        before, after = record.get("rng_draws_before"), record.get("rng_draws_after")
        require(integer(before, 0, 0xffffffffffffffff) and integer(after, before, 0xffffffffffffffff) and
                before == previous["state"]["rng_draws"], "command RNG boundary differs")
        index += 1
        result = snapshot("command", frame)
        require(result["state"]["rng_draws"] == after, "post-command RNG differs")
        actor, value = result["state"]["robots"][0], result["war_detour"]
        if name in ("setup-move", "replacement-move"):
            require(actor["order"] == 1 and actor["destination"] == ([48, 8] if name == "setup-move" else [48, 38]),
                    "authored Move was not applied")
            require(value["moving"] and value["move"] == actor["destination"], "authored physical Move differs")
        elif patrol:
            require(actor["order"] == 4 and actor["destination"] == [48, 38] and value["move"] == [48, 8],
                    "Patrol must retain its existing Move-derived first leg")
        else:
            require(not value["target_core"]["present"] and value["environment_target"] == value["attack_target"] == 0,
                    "retirement did not clear core/environment references")
        return result
    initial = snapshot("initial", 0)
    baseline = initial["state"]["robots"]
    command("setup-move", 0)
    command("setup-patrol", 0)
    frame = 0
    while index < len(records) and records[index].get("type") != "complete":
        if frame * step == 800 and change != "control":
            command("target-retirement" if change == "retirement" else "replacement-move", frame)
        frame += 1
        require(frame <= 24000 // step, "original frame limit exceeded")
        ticks.append(snapshot("tick", frame))
    require(bool(ticks) and index == len(records) - 1, "missing or extra terminal record")
    terminal = records[index]
    require(terminal.get("type") == "complete" and type(terminal.get("frame")) is int and
            terminal["frame"] == frame and type(terminal.get("time_ms")) is int and terminal["time_ms"] == frame * step,
            "completion boundary differs")
    require(integer(terminal.get("rng_draws"), 0, 0xffffffffffffffff) and
            terminal["rng_draws"] == ticks[-1]["state"]["rng_draws"], "terminal RNG differs")
    milestones = {key: -1 for key in RESULT_FIELDS}
    pulses, health, fired_on_detour = 0, 20.0, False
    firing_place = [-1, -1]
    retired = False
    for record in ticks:
        state, value, detail = record["state"], record["war_detour"], record.get("detail")
        time, actor = state["time_ms"], state["robots"][0]
        require(type(detail) is dict and {"time_ms", "war", "place", "failed_at", "enemy_count", "target",
                "attack_target", "core_live", "patrol_from", "move"} <= detail.keys(), "missing original detail")
        require(type(detail["time_ms"]) is int and detail["time_ms"] == time, "original detail time differs")
        require(all(integer(detail[k], 0, 1) for k in ("war", "target", "attack_target", "core_live")),
                "original detail flags must retain integer representation")
        require(integer(detail["place"], -1, 3) and integer(detail["failed_at"]) and detail["failed_at"] < 0 and
                integer(detail["enemy_count"], 0), "invalid tactical place/search observations")
        require(point(detail["patrol_from"]) and point(detail["move"]) and detail["move"] == value["move"],
                "original and physical move observations differ")
        core = value["target_core"]
        require(detail["core_live"] == int(core["present"]), "retained core presence differs")
        require(detail["target"] == int(value["environment_target"] == 3) and
                detail["attack_target"] == int(value["attack_target"] == 3), "normalized target observations differ")
        require(not retired or not core["present"], "retired target reappeared")
        retired |= not core["present"]
        require(actor["alive"] and actor["health_bits"] == baseline[0]["health_bits"],
                "observer health/lifetime changed")
        x, y = floating(actor["x_bits"]), floating(actor["y_bits"])
        require(0 <= x < 640 and 0 <= y < 640, "observer outside original map bound")
        for index in (1, 3):
            robot = state["robots"][index]
            require(robot["alive"] and all(robot[k] == baseline[index][k] for k in ("x_bits", "y_bits", "health_bits")),
                    "uninvolved robot moved or lost health")
        if change != "control" and time > 800:
            milestones["changed"] = 800
        replaced = change == "replacement" and time > 800
        require(actor["order"] == (1 if replaced else 4) and actor["destination"] == [48, 38], "player intent changed")
        if not replaced:
            require(detail["patrol_from"] == [48, 8], "original Patrol From differs")
        if detail["war"]:
            require(not replaced, "replacement Move must clear War")
            if milestones["war"] < 0:
                milestones["war"] = time
            if detail["place"] >= 0 and value["moving"] and value["move"] != [48, 8] and core["live"]:
                require(value["attack_target"] == 3 and value["move"] == [16, 22], "successful firing place differs")
                if milestones["detour"] < 0:
                    milestones["detour"], firing_place = time, value["move"]
                require(value["move"] == firing_place, "tactical physical place changed")
        require(value["firing_place"] == firing_place, "persisted firing-place history differs")
        if milestones["detour"] >= 0 and (milestones["changed"] < 0 or change == "retirement") and not value["moving"]:
            if [x, y] == [firing_place[0] * 10 + 20, firing_place[1] * 10 + 20] and (
                    milestones["reached_firing_place"] < 0):
                milestones["reached_firing_place"] = time
        current_health = floating(value["last_health_bits"])
        require(0 <= current_health <= health, "target health/cache increased or became nonfinite")
        if current_health < health:
            require(core["present"] and value["previous_firing_target"] == 3, "damage receiver lifetime differs")
            pulses += 1
            require(abs(current_health - (200 - 2 * pulses) / 10) < .00001, "damage pulse progression differs")
            if milestones["first_hit"] < 0:
                milestones["first_hit"] = time
            fired_on_detour |= milestones["detour"] > 0 and milestones["changed"] < 0
        require(value["pulses"] == pulses, "persisted damage pulse counter differs")
        if change != "control" and time == 800:
            require(milestones["detour"] > 0 and milestones["first_hit"] > 0 and
                    milestones["reached_firing_place"] < 0 and core["live"] and value["moving"] and
                    value["move"] == firing_place, "800 ms intervention preconditions differ")
        health = current_health
        if core["live"]:
            require(core["health_bits"] == value["last_health_bits"] and
                    [floating(state["robots"][2][k]) for k in ("x_bits", "y_bits")] == [240, 280],
                    "live target health/position differs")
        if (not core["present"] or not core["live"]) and milestones["terminal"] < 0:
            milestones["terminal"] = time
        if milestones["terminal"] >= 0 and time > milestones["terminal"] + step:
            require(value["environment_target"] == value["attack_target"] == 0 and detail["enemy_count"] == 0 and
                    not value["fire_order"], "terminal target/fire references remain")
        destination = [48, 38] if change == "replacement" else [48, 8]
        if value["moving"] and value["move"] == destination and milestones["detour"] > 0 and (
                replaced or milestones["terminal"] >= 0) and milestones["resumed"] < 0:
            milestones["resumed"] = time
        arrived = (replaced and actor["in_place"] and [x, y] == [500, 400]) if change == "replacement" else (
            milestones["terminal"] >= 0 and milestones["resumed"] >= 0 and x > 490 and abs(y - 100) < 5)
        require(not arrived or record is ticks[-1], "recording continues after original arrival break")
        if arrived:
            milestones["arrival"] = time
    last = ticks[-1]["war_detour"]
    require(milestones["war"] > 0 and milestones["detour"] >= milestones["war"] and
            milestones["first_hit"] > 0 and fired_on_detour, "missing successful combat detour")
    require(milestones["resumed"] > milestones["detour"] and milestones["arrival"] > milestones["resumed"],
            "missing physical continuation/arrival")
    require(last["environment_target"] == last["attack_target"] == 0 and not last["fire_order"], "arrival still firing")
    if change == "control":
        require(pulses == 100 and health == 0 and milestones["detour"] < milestones["reached_firing_place"] <
                milestones["terminal"] and milestones["resumed"] - milestones["terminal"] <= 100 + step,
                "uninterrupted detour completion differs")
    elif change == "retirement":
        require(milestones["changed"] == 800 and milestones["terminal"] == 800 + step and
                not last["target_core"]["present"] and 0 < pulses < 100 and
                milestones["reached_firing_place"] > 800 and
                milestones["resumed"] >= milestones["reached_firing_place"] and
                milestones["resumed"] - milestones["reached_firing_place"] <= 100 + step,
                "retirement continuation delay differs")
    else:
        require(milestones["changed"] == 800 and milestones["terminal"] == -1 and last["target_core"]["live"] and
                0 < pulses < 100 and health > 0 and milestones["resumed"] == 800 + step,
                "replacement Move continuation differs")
    result = terminal.get("milestones")
    require(type(result) is dict and RESULT_FIELDS | {"pulses", "last_health_bits", "firing_place"} <= result.keys(),
            "missing terminal milestones")
    require(all(integer(result[k], -1) and result[k] == value for k, value in milestones.items()),
            "persisted milestone differs from physical observations")
    require(type(result["pulses"]) is int and result["pulses"] == pulses and bits(result["last_health_bits"]) and
            result["last_health_bits"] == last["last_health_bits"] and point(result["firing_place"]) and
            result["firing_place"] == firing_place, "terminal damage/place differs")
    return ticks


def load(path, spec):
    with path.open(encoding="utf-8") as source:
        records = TRACES.read_records(source, str(path))
    validate(records, spec)
    return records


def projection(records):
    return [{"state": r["state"], "detail": r["detail"], "war_detour": r["war_detour"]} for r in records
            if r["type"] == "snapshot" and r.get("phase") == "tick"]


def controls(records, spec):
    changed = copy.deepcopy(records)
    del next(r for r in changed if r["type"] == "snapshot")["war_detour"]["target_core"]
    # Equality alone cannot reject the same omission in both sides.
    require(TRACES.compare_records(changed, copy.deepcopy(changed)) is None, "correlated control setup differs")
    mutations = [changed]
    changed = copy.deepcopy(records)
    next(r for r in changed if r["type"] == "snapshot")["war_detour"]["full_orders"][0]["orders"] = "missing"
    mutations.append(changed)
    changed = copy.deepcopy(records)
    changed[-1]["milestones"]["resumed"] = changed[-1]["milestones"]["detour"]
    mutations.append(changed)
    for changed in mutations:
        try:
            validate(changed, spec)
        except (TRACES.TraceError, KeyError):
            pass
        else:
            raise TRACES.TraceError("semantic fault control was accepted")
    try:
        TRACES.read_records(io.StringIO("\n".join(map(json.dumps, records[:-1]))), "truncated control")
    except TRACES.TraceError:
        pass
    else:
        raise TRACES.TraceError("truncated control was accepted")
    return 4


def check(directory, reference=None, expected=None, revision=None, reference_revision=None):
    items = list(inventory())
    names = {name for name, _ in items}
    require({p.name for p in directory.glob("war-detour-*.jsonl")} == names, "expected exactly 32 War detour files")
    require(reference_revision is None or (
            reference is not None and expected is not None and full_revision(reference_revision)),
            "reference revision requires a reference directory and complete strict profile")
    if reference is not None:
        require({p.name for p in reference.glob("war-detour-*.jsonl")} == names, "reference finite inventory differs")
    groups = {}
    candidate_profile = reference_profile = None
    faults = 0
    for name, spec in items:
        records = load(directory / name, spec)
        validate_profile(records[0], expected, revision)
        current = {key: records[0][key] for key in PROFILE_FIELDS}
        require(candidate_profile is None or TRACES._first_difference(candidate_profile, current) is None,
                "mixed candidate source/build profiles")
        candidate_profile = current
        if reference is not None:
            old = load(reference / name, spec)
            validate_profile(old[0], expected, reference_revision)
            current = {key: old[0][key] for key in PROFILE_FIELDS}
            require(reference_profile is None or TRACES._first_difference(reference_profile, current) is None,
                    "mixed reference source/build profiles")
            reference_profile = current
            difference = TRACES.compare_records(old, records)
            require(difference is None, f"reference mismatch in {name}: {difference}")
        key = spec["caller"], spec["rng"], spec["step"]
        if spec["role"] == "first":
            groups[key] = projection(records)
            if spec["rng"] == "pm" and spec["step"] == 10 and spec["caller"] in ("control", "retirement"):
                faults += controls(records, spec)
        elif spec["role"] == "replay":
            require(TRACES._first_difference(groups[key], projection(records)) is None, "original replay differs")
            if spec["caller"] == "control":
                del groups[key]
        else:
            prefix = 800 // spec["step"]
            require(TRACES._first_difference(groups.pop(key)[:prefix], projection(records)[:prefix]) is None,
                    "original intact prefix differs")
    print(f"Validated {len(items)} War detour files, 12 original replay and 8 intact-prefix comparisons; "
          f"{faults} fault controls")


def boolean(value):
    if value in ("true", "false"):
        return value == "true"
    raise argparse.ArgumentTypeError("use true or false")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference-directory", type=Path)
    parser.add_argument("--reference-revision")
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", type=boolean)
    parser.add_argument("--trace", type=boolean)
    args = parser.parse_args(argv)
    try:
        expected = profile(args.revision, args.configuration, args.compiler, args.target, args.cheats, args.trace)
        check(args.directory, args.reference_directory, expected, args.revision, args.reference_revision)
        return 0
    except (TRACES.TraceError, OSError, ValueError, KeyError, TypeError) as error:
        print("Invalid War detour recordings: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
