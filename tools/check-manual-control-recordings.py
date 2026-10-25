#!/usr/bin/env python3
"""Validate the eighty existing chassis/manual-exit episodes without selecting a historical reference."""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys

SPEC = importlib.util.spec_from_file_location("battle_traces", Path(__file__).with_name("compare-battle-traces.py"))
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)

LIVE_FIELDS = {"id", "alive", "x_bits", "y_bits", "health_bits", "side", "state", "in_place", "group",
               "order", "target", "target_is_null", "destination", "queue"}
MANUAL_FIELDS = {"id", "present", "alive", "state", "chassis", "x_bits", "y_bits", "health_bits", "speed_bits",
                 "forward_x_bits", "forward_y_bits", "group", "in_place", "moving", "move", "place", "place_add",
                 "enemy_count", "target", "attack_target", "group_state", "queue", "weapon"}
RESULT_FIELDS = {"exit_ms", "retry_ms", "existing_frames", "existing_prefix_frames", "existing_digest_low",
                 "existing_digest_high", "observed_frames", "observation_digest_low", "observation_digest_high",
                 "terminal_rng_draws"}


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=None, maximum=None):
    return (type(value) is int and (minimum is None or value >= minimum)
            and (maximum is None or value <= maximum))


def bits(value):
    return integer(value, 0, 0xffffffff)


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(item) for item in value)


def real(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def position(actor):
    return real(actor["x_bits"]), real(actor["y_bits"])


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def clean_source(record, expected=None):
    require(full_revision(record.get("revision")), "require a full compiled source revision")
    require(type(record.get("dirty")) is int and record["dirty"] == 0, "dirty compiled source identity")
    if expected is not None:
        require(full_revision(expected) and record["revision"] == expected, "compiled source revision differs")


def inventory():
    for chassis, kind in (("track", 3), ("hovercraft", 4)):
        for moving in (False, True):
            for rng in ("pm", "crt"):
                for step in (10, 16):
                    for continuation in ("original", "removed", "live"):
                        profile = dict(chassis=chassis, kind=kind, moving=moving, rng=rng,
                                       step=step, continuation=continuation)
                        stem = (f"manual-control-{chassis}-{'moving' if moving else 'stationary'}-"
                                f"{rng}-{step}-{continuation}")
                        yield profile, tuple(f"{stem}-{replay}.jsonl"
                                             for replay in ((0,) if continuation == "original" else (0, 1)))


def timing(step):
    rounded = lambda duration: ((duration + step - 1) // step) * step
    result = {}
    elapsed = 0
    for name, duration in (("forward", 200), ("neutral", 100), ("turn", 100), ("fire", 1000),
                           ("released", 200), ("fire_again", 200), ("reset", 200), ("exit", 100),
                           ("held_move", 1000), ("settled", 12000), ("retry", 500),
                           ("free", 500), ("finished", 9000)):
        elapsed += rounded(duration)
        result[name] = elapsed
    return result


def queue(value, width, maximum_id, description):
    require(type(value) is list and len(value) <= 5, description + " requires at most five orders")
    for order in value:
        require(type(order) is list and len(order) == width and all(integer(cell) for cell in order),
                description + " has an incomplete or noninteger order")
        require(0 <= order[0] <= 8 and 0 <= order[1] <= 8, description + " has an invalid physical type/phase")
        for index in range(2, 4 if width == 5 else 5):
            require(bits(order[index]), description + " has invalid parameter bits")
        require(-1 <= order[-1] <= maximum_id, description + " has an unnormalized object")
        if width == 7:
            require(integer(order[5], -(1 << 31), (1 << 31) - 1), description + " has an invalid Param4")


def snapshot(record, profile, removed):
    state = record["state"]
    robots = state["robots"]
    require([r.get("id") for r in robots] == [1, 2, 3, 4]
            and all(integer(r.get("id")) for r in robots), "Session actor inventory differs")
    for robot in robots:
        alive = robot["id"] == 4 or (robot["id"] == 3 and not removed)
        require(type(robot.get("alive")) is bool and robot["alive"] == alive, "Session actor lifetime differs")
        if not alive:
            require("queue" not in robot, "retired Session actor retains a queue")
            continue
        require(LIVE_FIELDS <= robot.keys(), "missing live Session field")
        require(all(bits(robot[key]) for key in ("x_bits", "y_bits", "health_bits")), "invalid Session bits")
        require(all(integer(robot[key]) for key in ("side", "state", "group", "order", "target")),
                "invalid live Session scalar")
        require(-1 <= robot["order"] <= 9 and -1 <= robot["target"] <= 4, "invalid Session order/target")
        require(type(robot["in_place"]) is bool and type(robot["target_is_null"]) is bool
                and point(robot["destination"]), "invalid Session arrival/destination")
        queue(robot["queue"], 5, 4, "Session queue")
    groups = state.get("player_groups")
    require(type(groups) is list, "missing Session player groups")
    for group in groups:
        fields = {"slot", "stored_count", "members", "order", "target"}
        require(type(group) is dict and fields <= group.keys() and all(integer(group[key]) for key in fields),
                "invalid Session player group")
        require(0 <= group["order"] <= 9 and -1 <= group["target"] <= 4, "invalid Session group order/target")
    manual = record.get("manual")
    require(type(manual) is dict and MANUAL_FIELDS <= manual.keys(), "missing manual actor fields")
    require(type(manual["id"]) is int and manual["id"] == 5 and manual["present"] is True
            and manual["alive"] is True, "manual actor identity/lifetime differs")
    require(integer(manual["chassis"]) and manual["chassis"] == profile["kind"], "manual chassis differs")
    require(all(bits(manual[key]) for key in ("x_bits", "y_bits", "health_bits", "speed_bits",
                                            "forward_x_bits", "forward_y_bits")), "invalid manual actor bits")
    require(all(integer(manual[key]) for key in ("state", "group", "place", "enemy_count", "target", "attack_target")),
            "invalid manual actor scalar")
    require(-1 <= manual["target"] <= 5 and -1 <= manual["attack_target"] <= 5, "unnormalized manual target")
    require(type(manual["in_place"]) is bool and type(manual["moving"]) is bool
            and point(manual["move"]) and point(manual["place_add"]), "invalid manual movement/place")
    group = manual["group_state"]
    require(type(group) is dict and type(group.get("present")) is bool
            and group["present"] == (manual["group"] >= 0), "invalid manual group presence")
    if group["present"]:
        require({"order", "target", "destination", "war"} <= group.keys(), "missing manual group fields")
        require(integer(group["order"], 0, 9) and integer(group["target"], -1, 5)
                and point(group["destination"]) and type(group["war"]) is bool, "invalid manual group fields")
    queue(manual["queue"], 7, 5, "manual queue")
    weapon = manual["weapon"]
    fields = {"present", "type", "range_bits", "heat", "heat_period", "heat_mod",
              "cooldown_period", "cooldown_mod", "on"}
    require(type(weapon) is dict and fields <= weapon.keys() and weapon["present"] is True,
            "missing armed manual weapon")
    require(integer(weapon["type"]) and weapon["type"] == 98 and bits(weapon["range_bits"]),
            "manual laser type/range differs")
    require(all(integer(weapon[key]) for key in fields - {"present", "type", "range_bits"}),
            "invalid manual weapon scalar")
    control = record.get("control")
    require(type(control) is dict and {"id", "arcade", "robot_mode"} <= control.keys()
            and integer(control["id"], -1, 5) and all(type(control[key]) is bool for key in ("arcade", "robot_mode")),
            "missing manual control state")
    held = record.get("input")
    require(type(held) is dict and {"forward", "right", "enter", "mouse_left"} <= held.keys()
            and all(type(held[key]) is bool for key in ("forward", "right", "enter", "mouse_left")),
            "missing held manual input")
    aim = record.get("aim")
    require(type(aim) is dict and type(aim.get("defined")) is bool
            and aim.get("kind") in ("none", "landscape", "water", "object") and integer(aim.get("object"), -1, 5),
            "invalid supplied aim classification")
    require(aim["defined"] == (state["frame"] > 0), "supplied aim initialization differs")
    require(aim["object"] == 0 or aim["kind"] == "object", "aim sentinel conflated with object identity")
    if aim["defined"]:
        require(all(bits(aim.get(key)) for key in ("x_bits", "y_bits", "z_bits")), "missing supplied aim bits")
    else:
        require(not {"x_bits", "y_bits", "z_bits"} & aim.keys(), "uninitialized supplied aim was read")
    reservations = record.get("reservations")
    require(type(reservations) is list and [r.get("id") for r in reservations] == [3, 4]
            and all(integer(r.get("id")) for r in reservations), "reservation actor inventory differs")
    for entry in reservations:
        present = entry["id"] == 4 or not removed
        require(type(entry.get("present")) is bool and entry["present"] == present, "reservation lifetime differs")
        if present:
            require(entry.get("alive") is True and integer(entry.get("place")), "missing live reservation place")
        else:
            require(not {"alive", "place"} & entry.keys(), "retired reservation retains live fields")


def header(record, profile, identity=None):
    expected = dict(fixture=("manual-chassis-lifecycle-v1" if profile["continuation"] == "original"
                             else "manual-live-enemy-continuation-v1"),
                    manual_observation="direct-control-v1", navigation_layout="open-v1", seed=80,
                    step_ms=profile["step"], max_frames=3000, audio="unavailable", initially_selected=False,
                    rng="ParkMiller" if profile["rng"] == "pm" else "LegacyCRT",
                    chassis=profile["chassis"], chassis_kind=profile["kind"], moving_target=profile["moving"],
                    continuation=profile["continuation"], manual_actor=5, enter_key=13, mouse_button=1,
                    render="storage-device-no-draw-cursor-surrogate", viewport=[1280, 720])
    if identity:
        expected.update({key: value for key, value in identity.items() if key != "target"})
        expected["build_exe"] = identity["target"] == "EXE"
        clean_source(record, identity.get("revision"))
    for key, value in expected.items():
        require(TRACES._first_difference(record.get(key), value) is None, "header." + key + " differs")
    require(integer(record.get("forward_key"), 0, 255) and integer(record.get("right_key"), 0, 255)
            and record["forward_key"] != record["right_key"], "invalid effective manual key bindings")
    for key in ("revision", "compiler", "compiler_version", "configuration", "precision_control", "assert_off",
                "build_exe", "cheats", "trace", "delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(key in record, "missing effective profile field " + key)
    for key in ("revision", "compiler", "compiler_version", "configuration"):
        require(type(record[key]) is str and bool(record[key]), "invalid effective profile field " + key)
    require(integer(record["precision_control"], 0) and integer(record.get("dirty"), 0, 1),
            "invalid precision/source identity scalar")
    for key in ("assert_off", "build_exe", "cheats", "trace"):
        require(type(record[key]) is bool, "invalid effective profile flag " + key)
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(record[key] in ("Original", "Corrected"), "invalid effective compatibility mode " + key)


def command_specs(profile, bindings):
    t = timing(profile["step"])
    def key(name, when, code, down):
        return name, when, "keyboard", code, int(down), [-1, -1]
    def mouse(name, when, value):
        return name, when, "mouse", 1, value, [640, 360]
    specs = [key("enter-down", 0, 13, True), key("enter-up", 0, 13, False),
             key("forward-down", 0, bindings["forward_key"], True),
             key("forward-up", t["forward"], bindings["forward_key"], False),
             key("right-down", t["neutral"], bindings["right_key"], True),
             key("right-up", t["turn"], bindings["right_key"], False),
             mouse("fire-down", t["turn"], 0), mouse("fire-up", t["fire"], 1),
             mouse("fire-down", t["released"], 0),
             ("input-reset", t["fire_again"], "system", 2, -1, [-1, -1]),
             key("forward-down", t["reset"], bindings["forward_key"], True),
             key("exit-down", t["exit"], 13, True), key("exit-up", t["exit"], 13, False)]
    remove = lambda when: ("remove-nearby-enemy", when, "fixture-remove", 3, 1, [-1, -1])
    move = lambda name, when, point: (name, when, "context-ground", -1, -1, point)
    if profile["continuation"] != "live":
        specs.append(remove(t["exit"]))
    specs += [move("move-goal", t["exit"], [100, 100]),
              key("forward-up", t["held_move"], bindings["forward_key"], False)]
    if profile["continuation"] != "original":
        if profile["continuation"] == "live":
            specs.append(remove(t["settled"]))
        specs += [move("retry-goal", t["retry"], [100, 100]), move("free-goal", t["free"], [100, 300])]
    return specs


def prefix(records):
    end = next(i for i, r in enumerate(records) if r.get("phase") == "manual-exit")
    return records[1:end + 1]


def firing(actor):
    return [order for order in actor["queue"] if order[0] == 5]


def validate(records, profile, identity=None):
    header(records[0], profile, identity)
    t = timing(profile["step"])
    original = profile["continuation"] == "original"
    horizon = t["settled"] if original else t["finished"]
    specs = command_specs(profile, records[0])
    commands = [record for record in records if record["type"] == "command"]
    require(len(commands) == len(specs), "manual command inventory differs")
    for command, (name, when, kind, code, value, location) in zip(commands, specs):
        expected = dict(name=name, frame=when // profile["step"], time_ms=when,
                        kind=kind, code=code, value=value, point=location)
        for key, expected_value in expected.items():
            require(TRACES._first_difference(command.get(key), expected_value) is None,
                    "command " + name + " " + key + " differs")
        require(integer(command.get("rng_draws_before"), 0)
                and integer(command.get("rng_draws_after"), command["rng_draws_before"]),
                "invalid command RNG boundary")
    removed = False
    active = False
    held = dict(forward=False, right=False, enter=False, mouse_left=False)
    snapshots, pairs, previous, pending = [], [], None, None
    ticks = []
    checkpoints = {}
    for record in records[1:-1]:
        if record["type"] == "command":
            require(pending is None and previous is not None, "command lacks its preceding observation")
            require(record["rng_draws_before"] == previous["state"]["rng_draws"],
                    "command RNG differs from preceding observation")
            pending = record
            pairs.append([previous, record])
            name = record["name"]
            if record["kind"] == "keyboard":
                for field, code in (("forward", records[0]["forward_key"]),
                                    ("right", records[0]["right_key"]), ("enter", 13)):
                    if record["code"] == code:
                        held[field] = bool(record["value"])
            elif record["kind"] == "mouse":
                held["mouse_left"] = record["value"] == 0
            elif name == "input-reset":
                held = dict.fromkeys(held, False)
            if name == "enter-down":
                active = True
            elif name == "exit-down":
                active = False
            elif name == "remove-nearby-enemy":
                require(not removed, "nearby enemy removed twice")
                removed = True
            continue
        require(record["type"] == "snapshot", "unexpected manual recording body")
        state = record["state"]
        require(state["time_ms"] == state["frame"] * profile["step"], "snapshot frame/time differs")
        snapshot(record, profile, removed)
        require(record["input"] == held and all(type(value) is bool for value in record["input"].values()),
                "held input differs from actual command history")
        require(record["control"] == dict(id=5 if active else 0, arcade=active, robot_mode=active),
                "controlled identity/mode differs")
        command_result = pending is not None
        if pending:
            require(record.get("phase") == pending["name"]
                    and state["frame"] == pending["frame"]
                    and state["rng_draws"] == pending["rng_draws_after"], "command result observation differs")
            pairs[-1].append(record)
            pending = None
        elif previous and state["frame"] == previous["state"]["frame"]:
            require(state["rng_draws"] == previous["state"]["rng_draws"], "same-frame RNG changed without a command")
        phase = record.get("phase")
        require(type(phase) is str, "missing observation phase")
        if phase == "tick":
            ticks.append(state["frame"])
        elif phase in ("initial", "manual-exit", "move-command", "retry-move", "free-move", "finished"):
            require(phase not in checkpoints, "duplicate manual checkpoint " + phase)
            checkpoints[phase] = record
        else:
            require(command_result, "unknown observation phase")
        snapshots.append(record)
        previous = record
    require(pending is None, "command lacks its result observation")
    require(ticks == list(range(1, horizon // profile["step"] + 1)), "missing or duplicate whole map tick")
    expected_checkpoints = {"initial", "manual-exit", "move-command", "finished"}
    if not original:
        expected_checkpoints |= {"retry-move", "free-move"}
    require(checkpoints.keys() == expected_checkpoints, "manual checkpoint inventory differs")
    require(records[1] is checkpoints["initial"] and records[-2] is checkpoints["finished"],
            "initial/terminal observation placement differs")
    by_name = {}
    for before, command, after in pairs:
        by_name.setdefault(command["name"], []).append((before, after))
    initial = checkpoints["initial"]["manual"]
    forward_up = by_name["forward-up"][0][0]["manual"]
    require(position(forward_up)[0] > position(initial)[0] + 5, "manual Forward produced no physical progress")
    direction_before = by_name["right-down"][0][0]["manual"]
    direction_after = by_name["right-up"][0][0]["manual"]
    require((direction_before["forward_x_bits"], direction_before["forward_y_bits"]) !=
            (direction_after["forward_x_bits"], direction_after["forward_y_bits"]), "manual heading did not change")
    patient = lambda record: record["state"]["robots"][2]
    first_fire, second_fire = by_name["fire-down"]
    release = by_name["fire-up"][0]
    reset = by_name["input-reset"][0]
    require(real(patient(release[0])["health_bits"]) < real(patient(first_fire[0])["health_bits"])
            and firing(release[0]["manual"]), "held manual laser produced no real damage/fire order")
    require(patient(second_fire[0])["health_bits"] == patient(release[0])["health_bits"]
            and not firing(second_fire[0]["manual"]), "released manual fire continued damage")
    require(real(patient(reset[0])["health_bits"]) < real(patient(second_fire[0])["health_bits"]),
            "second manual press did not resume damage")
    after_reset = by_name["forward-down"][1][0]
    require(patient(after_reset)["health_bits"] == patient(reset[1])["health_bits"]
            and not firing(after_reset["manual"]) and not after_reset["input"]["mouse_left"],
            "input reset did not stop held fire/damage")
    require((position(patient(release[0])) != position(patient(first_fire[0]))) == profile["moving"],
            "stationary/moving target contract differs")
    before_exit = by_name["exit-down"][0][0]
    require(position(before_exit["manual"])[0] > position(after_reset["manual"])[0] + 1,
            "final manual drive produced no physical progress")
    require(by_name["enter-up"][0][1]["manual"]["speed_bits"] != initial["speed_bits"]
            and by_name["exit-up"][0][1]["manual"]["speed_bits"] == initial["speed_bits"],
            "manual speed entry/restoration differs")
    exit_state = checkpoints["manual-exit"]
    require(exit_state["state"]["time_ms"] == t["exit"] and exit_state["input"]["forward"],
            "held-input manual exit boundary differs")
    exit_group = exit_state["manual"]["group_state"]
    require(exit_group["present"] and exit_group["order"] == 3 and exit_group["target"] == 0,
            "manual exit must retain targetless Attack")
    if not original:
        require(exit_state["reservations"][0]["place"] == 0, "nearby opponent's existing reservation differs")
    move_state = checkpoints["move-command"]
    require(move_state["state"]["time_ms"] == t["exit"]
            and move_state["manual"]["group_state"]["order"] == 1
            and move_state["manual"]["group_state"]["destination"] == [8, 8], "ordinary post-manual Move differs")
    held_before_release = by_name["forward-up"][1][0]
    before_move = by_name["move-goal"][0][0]
    require(held_before_release["input"]["forward"] and
            position(held_before_release["manual"])[1] < position(before_move["manual"])[1] - 10,
            "held former manual key prevented autonomous progress")
    settled = next(record for record in snapshots if record.get("phase") == "tick"
                   and record["state"]["time_ms"] == t["settled"])
    require(settled["manual"]["in_place"], "robot did not settle at its assigned place")
    if profile["continuation"] == "live":
        require(position(settled["manual"]) == (60, 60) and settled["manual"]["place"] == -1
                and settled["manual"]["place_add"] == [4, 4], "live-enemy reserved arrival differs")
        orders = firing(settled["manual"])
        require(len(orders) == 1 and orders[0][5] == (0 if profile["moving"] else 2),
                "legacy live-enemy fire-order type differs")
        require(not settled["manual"]["group_state"]["war"], "ordinary Move unexpectedly entered War")
    else:
        require(position(settled["manual"]) == (100, 100) and settled["manual"]["place"] == 0
                and not firing(settled["manual"]), "enemy-removed assigned arrival differs")
    finished = checkpoints["finished"]
    destination = (100, 100) if original else (100, 300)
    require(position(finished["manual"]) == destination and finished["manual"]["in_place"]
            and not firing(finished["manual"]), "final physical arrival/fire state differs")
    require(finished["manual"]["group_state"]["order"] == 1
            and finished["manual"]["group_state"]["destination"] == ([8, 8] if original else [8, 28]),
            "final retained Move differs")
    if not original:
        require(not any(order[0] == 1 for order in finished["manual"]["queue"]), "free Move did not retire")
        retry = checkpoints["retry-move"]
        free = checkpoints["free-move"]
        require(retry["state"]["time_ms"] == t["retry"] and free["state"]["time_ms"] == t["free"],
                "retry/free Move boundary differs")
        require(free["manual"]["moving"] and free["manual"]["move"] == [8, 28], "actual free-point Move differs")
        if profile["continuation"] == "live":
            require(retry["manual"]["place"] == -1 and retry["manual"]["place_add"] == [4, 4]
                    and retry["reservations"][1]["place"] == 0, "reservation transfer/retry differs")
        else:
            require(retry["manual"]["place"] == 0, "removed control lost its assigned place")
    result = records[-1].get("manual_result")
    require(type(result) is dict and RESULT_FIELDS <= result.keys()
            and all(integer(result[key], 0) for key in RESULT_FIELDS), "missing or invalid terminal manual result")
    require(records[-1]["frame"] == horizon // profile["step"] and records[-1]["time_ms"] == horizon,
            "manual completion horizon differs")
    require(result["exit_ms"] == t["exit"] and result["retry_ms"] == (0 if original else t["retry"]),
            "manual terminal milestones differ")
    observed = horizon // profile["step"] + (2 if original else 4)
    require(result["observed_frames"] == observed
            and result["existing_frames"] == (0 if original else observed)
            and result["existing_prefix_frames"] == (0 if original else t["exit"] // profile["step"] + 1),
            "existing observation/prefix counts differ")
    require(result["terminal_rng_draws"] == finished["state"]["rng_draws"], "terminal manual RNG differs")
    for key in ("existing_digest_low", "existing_digest_high", "observation_digest_low", "observation_digest_high"):
        require(bits(result[key]), "invalid terminal digest scalar")
    return records


def read(path):
    with path.open(encoding="utf-8") as stream:
        return TRACES.read_records(stream, str(path))


def check_directory(directory, identity=None, reference=None, reference_revision=None):
    require(reference_revision is None or reference is not None, "--reference-revision requires --reference")
    require(reference_revision is None or full_revision(reference_revision), "require a full reference revision")
    groups = list(inventory())
    expected = {name for _, names in groups for name in names}
    actual = {path.name for path in directory.glob("manual-control-*.jsonl")}
    require(actual == expected, f"manual inventory differs: missing={sorted(expected - actual)}, "
            f"extra={sorted(actual - expected)}")
    if reference is not None:
        require({path.name for path in reference.glob("manual-control-*.jsonl")} == expected,
                "manual reference inventory differs")
    profile_identity = None
    pairs = prefixes = 0
    removed_prefix = None
    for profile, names in groups:
        records = []
        for name in names:
            try:
                value = validate(read(directory / name), profile, identity)
            except (ValueError, KeyError, IndexError, TypeError) as error:
                raise TRACES.TraceError(f"{name}: {error}") from error
            current = {key: value[0][key] for key in ("revision", "dirty", "configuration", "compiler",
                       "compiler_version", "precision_control", "assert_off", "build_exe", "cheats", "trace",
                       "delayed_damage", "group_counts", "place_containment", "zero_x_trace",
                       "render", "audio", "viewport",
                       "forward_key", "right_key", "enter_key", "mouse_button")}
            require(profile_identity is None or TRACES._first_difference(profile_identity, current) is None,
                    "manual corpus execution profiles differ")
            profile_identity = current
            if reference is not None:
                reference_identity = ({key: item for key, item in identity.items() if key != "revision"}
                                      if identity else None)
                try:
                    old = read(reference / name)
                    clean_source(old[0], reference_revision)
                    validate(old, profile, reference_identity)
                except (ValueError, KeyError, IndexError, TypeError) as error:
                    raise TRACES.TraceError(f"reference {name}: {error}") from error
                require(TRACES.compare_records(old, value) is None, name + ": fresh-process/reference divergence")
            records.append(value)
        if len(records) == 2:
            require(TRACES.compare_records(*records) is None, names[0] + ": manual replay divergence")
            pairs += 1
        if profile["continuation"] == "removed":
            removed_prefix = prefix(records[0])
        elif profile["continuation"] == "live":
            require(TRACES._first_difference(removed_prefix, prefix(records[0])) is None,
                    names[0] + ": existing live/removed manual prefix diverged")
            prefixes += 1
    return len(expected), pairs, prefixes


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
    arguments = parser.parse_args(argv)
    identity = {key: getattr(arguments, key) for key in ("revision", "configuration", "compiler", "target")}
    identity.update(cheats=arguments.cheats == "true", trace=arguments.trace == "true")
    try:
        require(full_revision(arguments.revision), "require a full source revision")
        files, pairs, prefixes = check_directory(arguments.directory, identity, arguments.reference,
                                                arguments.reference_revision)
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, TypeError) as error:
        print("Manual-control validation failed: " + str(error), file=sys.stderr)
        return 1
    print(f"Validated {files} manual-control files, {pairs} existing replay pairs and {prefixes} declared prefixes; "
          "no reference acceptance implied.")
    if arguments.reference is not None and arguments.reference_revision is None:
        print("Reference revision is unpinned; this comparison is diagnostic.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
