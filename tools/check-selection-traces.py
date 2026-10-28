#!/usr/bin/env python3
"""Validate retained mixed-selection observations and exact session replays."""
import argparse
import importlib.util
from pathlib import Path
import sys


_spec = importlib.util.spec_from_file_location("battle_trace_reader", Path(__file__).with_name("compare-battle-traces.py"))
trace = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(trace)

FIXTURE = "mixed-selection-lifecycle-v1"
COMMANDS = (
    "clear-initial-selection", "select-cannon-only", "select-mixed-live", "lethal-damage",
    "select-mixed-dying", "clear-after-retirement", "select-after-retirement", "clear-final-selection",
)
PHASES = (
    "initial", "cleared-initial", "cannon-control", "live-selected", "dying-unselected",
    "building-selected-dying", "cleared-retired", "building-selected-retired", "cleared-final",
)


def require(condition, message):
    if not condition:
        raise trace.TraceError(message)


def validate(records, *, rng, step, building_first, click, revision=None, configuration=None, compiler=None,
             target=None, cheats=None, traced=None):
    """Check fixture-specific observations independently of the replay comparison."""
    header = records[0]
    expected = dict(fixture=FIXTURE, rng=rng, seed=80, step_ms=step, building_first=building_first,
                    click=click, actor_ids={"robot": 1, "building": 5, "cannon": 6}, audio="unavailable")
    for key, value in (("revision", revision), ("configuration", configuration), ("compiler", compiler)):
        if value is not None:
            expected[key] = value
    if revision is not None:
        expected["dirty"] = 0
    for key, value in (("build_exe", None if target is None else target == "EXE"), ("cheats", cheats), ("trace", traced)):
        if value is not None:
            expected[key] = value
    for key, value in expected.items():
        require(type(header.get(key)) is type(value) and header[key] == value, f"header.{key} differs from selected fixture/profile")
    require(all(type(value) is int for value in header["actor_ids"].values()), "actor IDs must be integers")
    area = header.get("area")
    require(isinstance(area, list) and len(area) == 4 and all(type(x) is int for x in area), "invalid selection area")
    width = 1 if click else 60
    require(area[2] - area[0] == width and area[3] - area[1] == width, "selection area does not match route")
    snapshots = [r for r in records if r["type"] == "snapshot"]
    commands = [r for r in records if r["type"] == "command"]
    ticks = 6000 // step
    require(len(snapshots) == ticks + len(PHASES), "missing selection boundary or retirement tick")
    expected_types = ["header", "snapshot"] + [kind for _ in range(5) for kind in ("command", "snapshot")]
    expected_types += ["snapshot"] * ticks + [kind for _ in range(3) for kind in ("command", "snapshot")] + ["complete"]
    require([r["type"] for r in records] == expected_types, "command/observation ordering differs")
    require([r["name"] for r in commands] == list(COMMANDS), "selection command schedule differs")
    mixed = [6, 5, 1] if building_first else [1, 5, 6]
    visible = [[], [6], mixed, [], mixed, [], [6, 5] if building_first else [5, 6], []]
    for index, (command, ids) in enumerate(zip(commands, visible)):
        frame = 0 if index < 5 else ticks
        require(command.get("frame") == frame and command.get("time_ms") == frame * step,
                "command boundary differs from actual selection schedule")
        require(isinstance(command.get("visible"), list) and all(type(value) is int for value in command["visible"])
                and command["visible"] == ids, "visible-object order differs")
        if index in (2, 4, 6):
            require(command.get("area") == area, "mixed-selection command area differs")
        elif index == 1:
            rect = command.get("area")
            require(isinstance(rect, list) and len(rect) == 4 and all(type(x) is int for x in rect)
                    and rect[2] - rect[0] == 40 and rect[3] - rect[1] == 40, "invalid cannon control area")
    expected_phases = list(PHASES[:6]) + ["retirement-tick"] * ticks + list(PHASES[6:])
    require(all(isinstance(r.get("selection"), dict) for r in snapshots), "snapshot selection object missing")
    require([r.get("selection", {}).get("phase") for r in snapshots] == expected_phases,
            "selection phase schedule differs")
    retired = False
    for index, record in enumerate(snapshots):
        state, selection = record["state"], record["selection"]
        phase = selection["phase"]
        frame = 0 if index < 6 else min(index - 5, ticks)
        require(state["frame"] == frame and state["time_ms"] == frame * step, "snapshot boundary differs")
        require(all(type(robot.get("id")) is int for robot in state["robots"])
                and [robot["id"] for robot in state["robots"]] == [1, 2, 3, 4], "session actor inventory differs")
        for robot in state["robots"]:
            require(type(robot.get("alive")) is bool, "session actor requires alive flag")
            require(robot["alive"] == (robot["id"] != 1 or index < 4), "session actor lifetime differs")
            if robot["alive"]:
                require({"x_bits", "y_bits", "side", "state", "health_bits", "in_place", "group", "order",
                         "target", "target_is_null", "destination", "queue"} <= robot.keys(), "live actor observations missing")
                require(isinstance(robot["queue"], list), "live actor queue must be an array")
                for order in robot["queue"]:
                    require(isinstance(order, list) and len(order) == 5,
                            "live actor queue entry must have five fields")
                    # Check serialized int representation without adding an order/phase whitelist.
                    require(all(trace._integer(order[i], minimum=-0x80000000, maximum=0x7fffffff) for i in (0, 1)),
                            "live actor queue type/phase must be int32")
                    require(all(trace._integer(order[i], maximum=0xffffffff) for i in (2, 3)),
                            "live actor queue parameters must be uint32 bit patterns")
                    require(trace._integer(order[4], minimum=-1, maximum=4),
                            "live actor queue object must be a normalized Session identity")
        require(isinstance(state.get("player_groups"), list), "session player groups missing")
        require(all(isinstance(group, dict) and {"slot", "stored_count", "members", "order", "target"} <= group.keys()
                    for group in state["player_groups"]), "player group observations missing")
        for key in ("kind", "active", "current_object", "current_index", "core_object", "actor_state", "enemy_kills"):
            require(type(selection.get(key)) is int, f"selection.{key} must be an integer")
        for key in ("ordered_members", "temporary_members"):
            ids = selection.get(key)
            require(isinstance(ids, list) and all(type(x) is int and 1 <= x <= 6 for x in ids)
                    and len(ids) == len(set(ids)), f"invalid selection.{key}")
        require(selection["current_index"] == 0, "unexpected current selection index")
        initial = phase == "initial"
        live = phase == "live-selected"
        building = phase in ("building-selected-dying", "retirement-tick", "building-selected-retired")
        require(selection["kind"] == (6 if initial else 1 if live else 2 if building else 4), "selection kind differs")
        require(selection["active"] == (5 if building else 0), "active identity differs")
        require(selection["current_object"] == (1 if initial or live else 0), "current identity differs")
        require(selection["ordered_members"] == ([1, 2] if initial else [1] if live else []), "ordered selection differs")
        require(selection["temporary_members"] == ([1, 2] if initial else []), "temporary selection differs")
        require(selection["enemy_kills"] == (0 if index < 4 else 1), "kill attribution differs")
        if index < 4:
            require(selection["core_object"] == 1 and selection["actor_state"] == 2, "live actor lifetime differs")
        elif phase in ("dying-unselected", "building-selected-dying"):
            require(selection["core_object"] == 1 and selection["actor_state"] == 5, "dying actor lifetime differs")
        elif phase == "retirement-tick":
            require((selection["core_object"], selection["actor_state"]) in ((1, 5), (0, -1)), "invalid retirement lifetime")
            require(not retired or selection["core_object"] == 0, "retired actor reappeared")
            retired |= selection["core_object"] == 0
        else:
            require(retired and selection["core_object"] == 0 and selection["actor_state"] == -1,
                    "actor did not retire before final selection")
    require(records[-1]["frame"] == ticks and records[-1]["time_ms"] == 6000, "wrong completion boundary")


def inventory():
    for suffix, rng in (("pm", "ParkMiller"), ("crt", "LegacyCRT")):
        for step in (10, 16):
            for building_first in (False, True):
                for click in (False, True):
                    name = f"player-selection-{suffix}-{step}-{'building' if building_first else 'robot'}-{'click' if click else 'rectangle'}"
                    yield name, dict(rng=rng, step=step, building_first=building_first, click=click)


def read(path):
    with path.open(encoding="utf-8") as stream:
        return trace.read_records(stream, str(path))


def check_directory(directory, *, reference=None, revision=None, configuration=None, compiler=None,
                    target=None, cheats=None, traced=None):
    expected = {f"{name}-{replay}.jsonl" for name, _ in inventory() for replay in (0, 1)}
    actual = {p.name for p in directory.glob("player-selection-*.jsonl")}
    require(actual == expected, f"selection inventory differs: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    if reference is not None:
        require({p.name for p in reference.glob("player-selection-*.jsonl")} == expected, "reference inventory differs")
    for name, profile in inventory():
        pair = []
        for replay in (0, 1):
            filename = f"{name}-{replay}.jsonl"
            records = read(directory / filename)
            validate(records, **profile, revision=revision, configuration=configuration, compiler=compiler,
                     target=target, cheats=cheats, traced=traced)
            if reference is not None:
                old = read(reference / filename)
                validate(old, **profile, configuration=configuration, compiler=compiler, target=target, cheats=cheats, traced=traced)
                require(trace.compare_records(old, records) is None,
                        f"{filename}: reference divergence: {trace.compare_records(old, records)}")
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
    parser.add_argument("--trace", choices=("true", "false"), dest="traced")
    args = parser.parse_args()
    for key in ("cheats", "traced"):
        value = getattr(args, key)
        setattr(args, key, None if value is None else value == "true")
    try:
        count = check_directory(**vars(args))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Selection trace validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {count} mixed-selection recordings and 16 exact session replay pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
