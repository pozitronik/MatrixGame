#!/usr/bin/env python3
"""Validate existing laser, plasma and turret-retarget recordings without changing their producers."""
import argparse
import importlib.util
from pathlib import Path
import sys


spec = importlib.util.spec_from_file_location("battle_trace_reader", Path(__file__).with_name("compare-battle-traces.py"))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)

LIVE_FIELDS = {"id", "alive", "x_bits", "y_bits", "side", "state", "health_bits", "in_place", "group",
               "order", "target", "target_is_null", "destination", "queue"}
TURRET_FIELDS = {"cannon_target_is_patient", "yaw_bits", "pitch_bits", "turret_yaw_bits", "cannon_health_bits"}


def require(condition, message):
    if not condition:
        raise trace.TraceError(message)


def integer(value):
    return type(value) is int


def inventory():
    for family in ("laser-attack", "plasma-flight", "cannon-retarget"):
        for suffix, rng in (("pm", "ParkMiller"), ("crt", "LegacyCRT")):
            for step in ((10, 16) if family == "laser-attack" else (10,)):
                yield f"{family}-{suffix}-{step}", dict(family=family, rng=rng, step=step)


def session_state(state, removed):
    require([robot.get("id") for robot in state["robots"]] == [1, 2, 3, 4]
            and all(integer(robot.get("id")) for robot in state["robots"]), "Session actor inventory differs")
    for robot in state["robots"]:
        require(type(robot.get("alive")) is bool and robot["alive"] == (robot["id"] != 1 or not removed),
                "Session actor lifetime differs")
        if not robot["alive"]:
            continue
        require(LIVE_FIELDS <= robot.keys(), "live Session observations missing")
        require(all(integer(robot[key]) for key in ("side", "state", "group", "order", "target")), "invalid live actor integer")
        require(type(robot["in_place"]) is bool and type(robot["target_is_null"]) is bool, "invalid live actor flag")
        require(isinstance(robot["destination"], list) and len(robot["destination"]) == 2
                and all(integer(x) for x in robot["destination"]), "invalid live actor destination")
        require(isinstance(robot["queue"], list) and all(isinstance(order, list) and len(order) == 5
                and all(integer(x) for x in order) and all(0 <= order[i] <= 0xffffffff for i in (2, 3))
                for order in robot["queue"]), "invalid live actor queue")
    groups = state.get("player_groups")
    require(isinstance(groups, list) and all(isinstance(group, dict)
            and {"slot", "stored_count", "members", "order", "target"} <= group.keys()
            and all(integer(group[key]) for key in ("slot", "stored_count", "members", "order", "target"))
            for group in groups), "Session player-group observations missing or invalid")


def validate(records, *, family, rng, step, revision=None, configuration=None, compiler=None, target=None, cheats=None):
    turret = family == "cannon-retarget"
    removal_frame = 100 if turret else (1600 if family == "plasma-flight" else 1000) // step
    final_frame = 200 if turret else removal_frame + 2000 // step
    header = records[0]
    expected = dict(fixture="synthetic-armed-boxes-v2", combat_observation="status-health-v1", scenario=family,
                    rng=rng, step_ms=step, seed=82, audio="unavailable", initially_selected=not turret,
                    max_frames=3000 if turret else 1000)
    for key, value in (("revision", revision), ("configuration", configuration), ("compiler", compiler),
                       ("build_exe", None if target is None else target == "EXE"), ("cheats", cheats)):
        if value is not None:
            expected[key] = value
    for key, value in expected.items():
        require(type(header.get(key)) is type(value) and header[key] == value, "header." + key + " differs")
    if revision is not None:
        require(header.get("dirty") == 0 and integer(header.get("dirty")), "dirty compiled source identity")
    snapshots = [record for record in records if record["type"] == "snapshot"]
    commands = [record for record in records if record["type"] == "command"]
    names = ["remove-target(1)"] if turret else ["context-target(3)", "remove-shooter(1)"]
    require([command["name"] for command in commands] == names, "command schedule differs")
    require([snapshot["state"]["frame"] for snapshot in snapshots] == list(range(final_frame + 1)),
            "missing or duplicated completed map boundary")
    expected_types = ["header"]
    for frame in range(final_frame + 1):
        if frame == removal_frame or (not turret and frame == 0):
            expected_types.append("command")
        expected_types.append("snapshot")
    expected_types.append("complete")
    require([record["type"] for record in records] == expected_types, "command/observation ordering differs")
    for command, frame in zip(commands, [removal_frame] if turret else [0, removal_frame]):
        require(command["frame"] == frame and command.get("accepted") is True, "command frame or acceptance differs")
        require(integer(command.get("rng_draws_before")) and integer(command.get("rng_draws_after"))
                and 0 <= command["rng_draws_before"] <= command["rng_draws_after"], "invalid command RNG observations")
        require(integer(command.get("resulting_order")) and integer(command.get("resulting_target"))
                and isinstance(command.get("resulting_destination"), list) and len(command["resulting_destination"]) == 2
                and all(integer(x) for x in command["resulting_destination"]), "command result observations missing")
    observed_draws = observed_frame = None
    for record in records[1:-1]:
        if record["type"] == "command":
            require(observed_draws is None or record["rng_draws_before"] >= observed_draws, "command RNG moved backwards")
            observed_draws, observed_frame = record["rng_draws_after"], record["frame"]
            continue
        state = record["state"]
        frame = state["frame"]
        require(state["time_ms"] == frame * step, "snapshot clock differs")
        if observed_frame == frame:
            require(state["rng_draws"] == observed_draws, "command/snapshot RNG boundary differs")
        observed_draws, observed_frame = state["rng_draws"], frame
        session_state(state, frame >= removal_frame)
        combat = record.get("combat")
        require(isinstance(combat, dict) and {"health_bits", "ablaze_ttl", "shorted_ttl", "robot_state"} <= combat.keys(),
                "combat observations missing")
        patient = 2 if turret and frame >= removal_frame else 1 if turret else 3
        require(combat["health_bits"] == state["robots"][patient - 1]["health_bits"], "combat patient health differs")
        require(all(integer(combat[key]) for key in ("ablaze_ttl", "shorted_ttl", "robot_state")), "invalid combat integer")
        if turret:
            require(TURRET_FIELDS <= combat.keys() and type(combat["cannon_target_is_patient"]) is bool,
                    "turret observations missing")
        elif frame < removal_frame:
            require(integer(combat.get("heat")) and combat["heat"] in (0, 10), "weapon heat observations missing or invalid")
    require(records[-1]["frame"] == final_frame and records[-1]["time_ms"] == final_frame * step,
            "completion horizon differs")


def read(path):
    with path.open(encoding="utf-8") as stream:
        return trace.read_records(stream, str(path))


def check_directory(directory, *, reference=None, **identity):
    expected = {f"{name}-{replay}.jsonl" for name, _ in inventory() for replay in (0, 1)}
    actual = {path.name for family in ("laser-attack", "plasma-flight", "cannon-retarget")
              for path in directory.glob(family + "-*.jsonl")}
    require(actual == expected, f"combat inventory differs: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    if reference is not None:
        actual_reference = {path.name for family in ("laser-attack", "plasma-flight", "cannon-retarget")
                            for path in reference.glob(family + "-*.jsonl")}
        require(actual_reference == expected, "combat reference inventory differs")
    profile_identity = None
    for name, profile in inventory():
        pair = []
        for replay in (0, 1):
            filename = f"{name}-{replay}.jsonl"
            records = read(directory / filename)
            validate(records, **profile, **identity)
            current = {key: records[0][key] for key in ("revision", "dirty", "configuration", "compiler", "compiler_version",
                "precision_control", "assert_off", "build_exe", "cheats", "delayed_damage", "group_counts",
                "place_containment", "zero_x_trace", "render", "audio", "viewport")}
            if profile_identity is not None:
                changed = [key for key in current if type(current[key]) is not type(profile_identity[key])
                           or current[key] != profile_identity[key]]
                require(not changed, "combat corpus profile differs: " + ", ".join(changed))
            profile_identity = current
            if reference is not None:
                old = read(reference / filename)
                validate(old, **profile, **{key: value for key, value in identity.items() if key != "revision"})
                difference = trace.compare_records(old, records)
                require(difference is None, f"{filename}: reference divergence: {difference}")
            pair.append(records)
        difference = trace.compare_records(*pair)
        require(difference is None, f"{name}: replay divergence: {difference}")
    return len(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", choices=("true", "false"))
    args = parser.parse_args()
    args.cheats = None if args.cheats is None else args.cheats == "true"
    try:
        count = check_directory(**vars(args))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Combat recording validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {count} armed-combat recordings and eight exact replay pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
