"""Validate the real N5 producer corpus through the existing strict schema-1 comparator."""
import argparse
import copy
import importlib.util
import itertools
import json
from pathlib import Path
import sys

SPEC = importlib.util.spec_from_file_location("battle_traces", Path(__file__).parents[1] / "tools" / "compare-battle-traces.py")
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)
MOVERS = [1, 2, 5, 6, 7, 8, 9, 10]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, minimum=None, maximum=None):
    return type(value) is int and (minimum is None or value >= minimum) and (maximum is None or value <= maximum)


def uint32(value):
    return integer(value, 0, 0xffffffff)


def expected():
    for family in ("intermediate", "trailing-reference", "crowded", "opposing"):
        physical = family in ("intermediate", "trailing-reference")
        roles = ("run0", "run1") if physical else ("inactive", "capture0", "capture1")
        for rng in ("pm", "crt"):
            for step in (10, 16):
                stem = f"n5-{'formation' if physical else 'work'}-{family}-{rng}-{step}-"
                yield family, physical, rng, step, [stem + role + ".jsonl" for role in roles]


def actors(snapshot, physical):
    return snapshot["formation" if physical else "navigation"]["actors"]


def validate(records, family, physical, rng, step, arguments):
    header, footer = records[0], records[-1]
    fixture = {"intermediate": "formation-moving-intermediate-v1", "trailing-reference": "formation-moving-reference-v1",
               "crowded": "crowded-destination-v1", "opposing": "opposing-flow-v1"}[family]
    require(header["fixture"] == fixture and header["rng"] == ("ParkMiller" if rng == "pm" else "LegacyCRT"), "fixture/RNG mismatch")
    require(header["seed"] == 81 and header["step_ms"] == step, "seed/timestep mismatch")
    require(header["configuration"] == arguments.configuration and header["compiler"] == arguments.compiler, "build mismatch")
    require(header["build_exe"] is (arguments.target == "EXE") and header["cheats"] is arguments.cheats, "role/cheats mismatch")
    require(header["revision"] == arguments.revision and header["dirty"] == 0, "stale or dirty compiled source identity")
    require(header["audio"] == "unavailable" and header["render"] == "storage-device-no-draw-cursor-surrogate", "execution policy mismatch")
    require(header["viewport"] == [1280, 720] and header["initially_selected"] is True, "view/selection mismatch")
    require(header["navigation_observation"] == ("physical-actors-v1" if physical else "work-actors-v1"), "observation version mismatch")
    require(header.get("navigation_layout", "central-obstacle-v1") == ("central-obstacle-v1" if family == "crowded" else "open-v1"), "topology mismatch")
    require(header["max_frames"] == (6000 if physical else 32000 // step), "fixture horizon mismatch")
    for key in ("compiler_version", "precision_control", "assert_off", "delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(key in header, f"missing effective profile field {key}")
    snapshots = [r for r in records if r["type"] == "snapshot"]
    commands = [r for r in records if r["type"] == "command"]
    require(len(commands) == (8 if family == "opposing" else 1), "missing/extra commands")
    require([r["type"] for r in records[1:1 + len(commands)]] == ["command"] * len(commands), "commands must precede initial snapshot")
    previous_draws = None
    for index, command in enumerate(commands):
        destination = ([48 if index < 4 else 8, 18 + index % 4 * 6] if family == "opposing" else [600, 260] if physical else [500, 300])
        require(command["frame"] == 0 and command["time_ms"] == 0 and command["destination"] == destination, "command schedule/input mismatch")
        require(command["name"] == ("PGOrderMoveTo" if family == "opposing" else "context-ground"), "command route mismatch")
        if not physical:
            require(command["actor"] == (MOVERS[index] if family == "opposing" else 0), "command actor mismatch")
        before, after = command["rng_draws_before"], command["rng_draws_after"]
        require(integer(before, 0) and integer(after, before), "invalid actual command RNG boundary")
        require(previous_draws is None or previous_draws == before, "command RNG boundaries are discontinuous")
        previous_draws = after
    require(snapshots and snapshots[0]["state"]["rng_draws"] == previous_draws, "initial snapshot does not follow actual commands")
    count = (5 if family == "intermediate" else 6) if physical else 10
    for frame, snapshot in enumerate(snapshots):
        state = snapshot["state"]
        require(state["frame"] == frame and state["time_ms"] == frame * step, "missing/duplicate whole-frame boundary")
        require([r["id"] for r in state["robots"]] == [1, 2, 3, 4] and isinstance(state["player_groups"], list), "original Session observations missing")
        observed = actors(snapshot, physical)
        ids = [r["id"] if physical else r[0] for r in observed]
        require(ids == list(range(1, count + 1)), "missing/duplicate/renumbered actor, including added movers")
        for actor in observed:
            if physical:
                for key in ("id", "alive", "side", "state", "x_bits", "y_bits", "forward_x_bits", "forward_y_bits", "health_bits", "speed_bits", "group_speed_bits", "maximum_speed_bits", "group", "in_place", "queue"):
                    require(key in actor, f"missing physical actor field {key}")
                require(actor["alive"] is True, "physical actor lifetime lost")
                require(all(uint32(actor[key]) for key in actor if key.endswith("_bits")), "invalid physical float bits")
                require(isinstance(actor["queue"], list), "missing physical queue")
                for order in actor["queue"]:
                    require(set(order) == {"type", "phase", "param1_bits", "param2_bits", "param3_bits", "param4", "object"}, "incomplete physical order")
                    require(all(uint32(order[key]) for key in ("param1_bits", "param2_bits", "param3_bits")), "invalid order bits")
                    require(order["object"] in range(count + 1), "unnormalized order object")
            else:
                # These are the original Scene::snapshot arrays, preserved without reinterpretation.
                require(len(actor) == 9 and all(uint32(value) for value in actor[1:6]), "incomplete work actor/float bits")
                require(integer(actor[6]) and actor[7] in (0, 1) and isinstance(actor[8], list), "invalid work group/arrival/queue")
                for order in actor[8]:
                    require(len(order) == 7 and all(uint32(value) for value in order[2:5]), "incomplete work order")
                    require(order[6] in range(count + 1), "unnormalized work order object")
        for original in state["robots"]:
            actor = observed[original["id"] - 1]
            x, y = (actor["x_bits"], actor["y_bits"]) if physical else (actor[1], actor[2])
            require(original["alive"] is True and (original["x_bits"], original["y_bits"]) == (x, y), "Session/extended actor identity mismatch")
    if physical:
        events = footer["formation"]
        require(events["stationary_frames"] == 20 and integer(events["arrival_ms"], 1, 50000), "missing arrival/stationary tail")
        require(footer["time_ms"] == events["arrival_ms"] + 20 * step and events["arrival_ms"] % step == 0, "incomplete stationary tail")
        require(events["reference_samples"] >= 10 and events["physical_samples"] >= 10, "missing physical/reference eligibility evidence")
        places = [[58, 24], [54, 20], [54, 28]] + ([[58, 20]] if count == 6 else [])
        require(events["assigned"] == places, "declared individual destinations changed")
        movers = [1, 2] + list(range(5, count + 1))
        for snapshot in snapshots[-21:]:
            require(all(actors(snapshot, True)[i - 1]["in_place"] for i in movers), "stationary-tail actor not in place")
        require(all(not actors(snapshots[-1], True)[i - 1]["queue"] for i in movers), "movement orders not retired")
    else:
        events = footer["events"]
        require(footer["time_ms"] == 32000 and len(snapshots) == 32000 // step + 1, "work horizon truncated")
        require([r["id"] for r in events["movers"]] == MOVERS, "terminal movers missing")
        require(len({tuple(r["assigned"]) for r in events["movers"]}) == 8, "assigned destinations not distinct")
        for mover in events["movers"]:
            actor = actors(snapshots[-1], False)[mover["id"] - 1]
            require(0 < mover["first_movement_ms"] <= mover["first_arrival_ms"] <= 32000, "missing movement/arrival")
            require((mover["x_bits"], mover["y_bits"]) == (actor[1], actor[2]) and actor[7] == 1, "terminal mover does not match final state")
        require(events["initial_rng_draws"] == events["final_rng_draws"], "work capture consumed RNG")
    require(events["initial_rng_draws"] == snapshots[0]["state"]["rng_draws"] and events["final_rng_draws"] == snapshots[-1]["state"]["rng_draws"], "terminal RNG boundaries mismatch")
    require(uint32(events["minimum_separation_bits"]), "missing separation observation")
    return header


def changed(records, index, edit):
    result = list(records)
    result[index] = copy.deepcopy(records[index])
    edit(result[index])
    return result


def controls(path, records, description, arguments):
    family, physical, rng, step = description
    first = next(i for i, record in enumerate(records) if record["type"] == "snapshot")
    def mutate_position(record):
        last = actors(record, physical)[-1]
        if physical:
            last["x_bits"] ^= 1
        else:
            last[1] ^= 1
    def mutate_order(record):
        order = actors(record, physical)[-1]["queue" if physical else 8][0]
        order["object" if physical else 6] = 1
    for edit in (mutate_position, mutate_order):
        candidate = changed(records, first, edit)
        difference = TRACES.compare_records(records, candidate)
        require(difference is not None and ("formation" if physical else "navigation") in difference, "added actor mutation was not compared")
    candidate = changed(records, -1, lambda r: r["formation" if physical else "events"].__setitem__("minimum_separation_bits", r["formation" if physical else "events"]["minimum_separation_bits"] ^ 1))
    require(TRACES.compare_records(records, candidate) is not None, "terminal observation mutation was ignored")
    for edit in (lambda r: actors(r, physical).__delitem__(slice(4, None)),
                 lambda r: actors(r, physical)[-1].__setitem__("x_bits" if physical else 1, "invalid bits")):
        candidate = changed(records, first, edit)
        try:
            validate(candidate, family, physical, rng, step, arguments)
        except (ValueError, TypeError, KeyError):
            pass
        else:
            raise ValueError("two equally incomplete/invalid actor exports could pass")
    for tail in ("missing", "invalid", "duplicate", "boundary"):
        with path.open(encoding="utf-8") as stream:
            if tail == "missing":
                lines = itertools.islice(stream, len(records) - 1)
            elif tail == "boundary":
                footer = dict(records[-1], frame=records[-1]["frame"] + 1)
                lines = itertools.chain(itertools.islice(stream, len(records) - 1), [json.dumps(footer)])
            else:
                lines = itertools.chain(stream, ["not JSON" if tail == "invalid" else json.dumps(records[-1])])
            try:
                TRACES.read_records(lines, str(path) + "/" + tail)
            except TRACES.TraceError:
                pass
            else:
                raise ValueError("invalid/incomplete tail was accepted: " + tail)
    return 9


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--configuration", required=True, choices=("Debug", "Release"))
    parser.add_argument("--compiler", required=True, choices=("MinGW", "MSVC"))
    parser.add_argument("--target", required=True, choices=("EXE", "DLL"))
    parser.add_argument("--cheats", required=True, choices=("true", "false"))
    parser.add_argument("--revision", required=True)
    arguments = parser.parse_args()
    arguments.cheats = arguments.cheats == "true"
    require(len(arguments.revision) == 40 and all(c in "0123456789abcdef" for c in arguments.revision), "revision must be a full commit ID")
    groups = list(expected())
    names = {name for *_, files in groups for name in files}
    actual = {path.name for path in arguments.directory.glob("n5-*.jsonl")}
    require(actual == names, f"expected 40 N5 files; missing={sorted(names - actual)}, extra={sorted(actual - names)}")
    comparisons = sensitivity = 0
    common_profile = None
    for family, physical, rng, step, files in groups:
        baseline = None
        for name in files:
            path = arguments.directory / name
            with path.open(encoding="utf-8") as stream:
                records = TRACES.read_records(stream, str(path))
            header = validate(records, family, physical, rng, step, arguments)
            profile = {key: header[key] for key in ("revision", "dirty", "configuration", "compiler", "compiler_version", "precision_control", "assert_off", "build_exe", "cheats", "delayed_damage", "group_counts", "place_containment", "zero_x_trace", "audio", "render", "viewport")}
            require(common_profile is None or profile == common_profile, "corpus execution profiles differ")
            common_profile = profile
            if baseline is None:
                baseline = records
                sensitivity += controls(path, records, (family, physical, rng, step), arguments)
            else:
                difference = TRACES.compare_records(baseline, records)
                require(difference is None, str(path) + ": " + str(difference))
                comparisons += 1
        print(f"Validated {family}/{rng}/{step}: {len(files)} complete matching recordings")
    print(f"Validated 40 real recordings, {comparisons} same-profile comparisons and {sensitivity} sensitivity controls; no reference acceptance implied.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, TypeError) as error:
        print("N5 trace validation failed: " + str(error), file=sys.stderr)
        sys.exit(1)