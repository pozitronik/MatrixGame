"""Validate real fire-feedback recordings with the existing strict schema-1 reader."""
import argparse
import collections
import copy
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import sys

SPEC = importlib.util.spec_from_file_location("battle_traces", Path(__file__).parents[1] / "tools" / "compare-battle-traces.py")
TRACES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACES)
FEEDBACK_FIELDS = {"id", "place", "place_not_found", "bad_places", "enemy_count", "target", "attack_target", "target_change", "last_fire", "last_hit", "health_bits", "moving", "move"}
RESULT_FIELDS = {"first_bad", "first_relocation", "first_recovery_hit", "final_health_bits"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, minimum=None):
    return type(value) is int and (minimum is None or value >= minimum)


def bits(value):
    return integer(value, 0) and value <= 0xffffffff


def session_observations(state):
    required = {"frame", "time_ms", "rng_draws", "robots", "player_groups"}
    require(type(state) is dict and required <= state.keys(), "missing Session state field")
    require(all(integer(state[key], 0) for key in ("frame", "time_ms", "rng_draws")), "invalid Session boundary type")
    require(type(state["robots"]) is list, "invalid Session actor array")
    robots = state["robots"]
    require([r["id"] for r in robots] == [1, 2, 3, 4] and all(integer(r["id"]) for r in robots), "Session actor inventory differs")
    require([r["alive"] for r in robots] == [True, True, True, False] and all(type(r["alive"]) is bool for r in robots), "live/retired actor identity differs")
    live_fields = {"id", "alive", "x_bits", "y_bits", "side", "state", "health_bits", "in_place", "group", "order", "target", "target_is_null", "destination", "queue"}
    for robot in robots[:3]:
        require(live_fields <= robot.keys(), "missing live Session actor field")
        require(all(bits(robot[key]) for key in ("x_bits", "y_bits", "health_bits")), "invalid live Session float bits")
        require(all(integer(robot[key]) for key in ("side", "state", "group", "order", "target")), "invalid live Session integer")
        require(-1 <= robot["target"] <= 4 and type(robot["target_is_null"]) is bool and type(robot["in_place"]) is bool, "invalid live Session target/arrival")
        require(type(robot["destination"]) is list and len(robot["destination"]) == 2 and all(integer(x) for x in robot["destination"]), "invalid Session destination")
        require(type(robot["queue"]) is list, "invalid Session queue")
        for order in robot["queue"]:
            require(type(order) is list and len(order) == 5 and all(integer(order[i]) for i in (0, 1, 4)) and all(bits(order[i]) for i in (2, 3)), "invalid Session queue entry")
            require(-1 <= order[4] <= 4, "unnormalized Session queue object")
    groups = state["player_groups"]
    require(type(groups) is list, "invalid Session player_groups")
    fields = {"slot", "stored_count", "members", "order", "target"}
    for group in groups:
        require(type(group) is dict and fields <= group.keys(), "missing Session player-group field")
        require(all(integer(group[key]) for key in fields) and group["slot"] >= 0 and group["members"] >= 0 and -1 <= group["target"] <= 4, "invalid Session player-group type")
    slots = [group["slot"] for group in groups]
    require(len(slots) == len(set(slots)), "duplicate Session player-group slot")
    require(all(robot["group"] in slots for robot in robots[:3] if robot["side"] == 1 and robot["group"] >= 0), "missing live Session player group")


def observations(record):
    session_observations(record["state"])
    robots = record["state"]["robots"]
    feedback = record["feedback"]
    require([r["id"] for r in feedback] == [1, 2, 3] and all(integer(r["id"]) for r in feedback), "feedback actor inventory differs")
    for index, value in enumerate(feedback):
        require(FEEDBACK_FIELDS <= value.keys(), "missing custom feedback field")
        for key in ("place", "place_not_found", "enemy_count", "target_change", "last_fire", "last_hit"):
            require(integer(value[key]), "invalid feedback integer: " + key)
        require(value["enemy_count"] >= 0, "negative enemy count")
        require(type(value["bad_places"]) is list and len(value["bad_places"]) <= 8 and all(integer(x, 0) for x in value["bad_places"]), "invalid bad-place observations")
        for key in ("target", "attack_target"):
            require(integer(value[key]) and -1 <= value[key] <= 3, "unnormalized feedback target")
        require(bits(value["health_bits"]) and value["health_bits"] == robots[index]["health_bits"], "feedback/Session health differs")
        require(type(value["moving"]) is bool and type(value["move"]) is list and len(value["move"]) == 2 and all(integer(x) for x in value["move"]), "invalid movement observation")
    require(type(record["war"]) is bool and type(record["ally_on_ray"]) is bool, "missing War/ray observation")


def completion(record, last):
    require(record["frame"] == last["state"]["frame"] and record["time_ms"] == last["state"]["time_ms"], "completion boundary differs")
    result = record["feedback_result"]
    require(RESULT_FIELDS <= result.keys(), "missing terminal feedback field")
    require(all(integer(result[key], 0) for key in RESULT_FIELDS - {"final_health_bits"}), "invalid terminal milestone")
    require(bits(result["final_health_bits"]) and result["final_health_bits"] == last["feedback"][2]["health_bits"], "terminal health differs")


def pairs():
    for family in ("ineffective", "obstructed"):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for role, context, indices in (("moving", "main-case", (0, 1)), ("control", "main-case", (0, 1)), ("control", "standalone-control", (2, 3))):
                    names = [f"fire-{family}-{role}-{rng}-{step}-{index}.jsonl" for index in indices]
                    yield (family, rng, step, role, context), names


def validate(records, description, arguments):
    family, rng, step, role, context = description
    header = records[0]
    require(header["fixture"] == "moving-target-fire-feedback-v1" and header["scenario"] == f"fire-{family}-{role}" and header["session_context"] == context, "scenario/context mismatch")
    require(header["rng"] == ("ParkMiller" if rng == "pm" else "LegacyCRT") and header["seed"] == 84 and header["step_ms"] == step, "RNG/seed/step mismatch")
    speed = .1 if family == "obstructed" else .35 if role == "moving" else 0.
    require(header["target_speed_bits"] == struct.unpack("<I", struct.pack("<f", speed))[0], "declared target speed mismatch")
    require(header["configuration"] == arguments.configuration and header["compiler"] == arguments.compiler, "build mismatch")
    require(header["build_exe"] is (arguments.target == "EXE") and header["cheats"] is arguments.cheats, "role/cheats mismatch")
    require(header["revision"] == arguments.revision and header["dirty"] == 0, "stale or dirty compiled source identity")
    require(header["max_frames"] == 3000 and header["navigation_layout"] == "open-v1" and header["audio"] == "unavailable", "fixture execution policy mismatch")
    require(header["render"] == "storage-device-no-draw-cursor-surrogate" and header["viewport"] == [1280, 720] and header["initially_selected"] is True, "render/selection policy mismatch")
    for key in ("precision_control", "compiler_version", "assert_off", "delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(key in header, "missing effective profile field " + key)
    commands = []
    snapshots = []
    observed_rng = None
    previous_frame = None
    for record in records[1:-1]:
        if record["type"] == "command":
            require(record["time_ms"] == record["frame"] * step, "command frame/time mismatch")
            require(integer(record["rng_draws_before"], 0) and integer(record["rng_draws_after"], record["rng_draws_before"]), "invalid command RNG boundaries")
            require(observed_rng is None or record["rng_draws_before"] == observed_rng, "command RNG before differs from preceding observation")
            observed_rng = record["rng_draws_after"]
            commands.append(record)
        else:
            state = record["state"]
            require(state["time_ms"] == state["frame"] * step, "snapshot frame/time mismatch")
            require(observed_rng is None or state["rng_draws"] >= observed_rng, "snapshot RNG moved backwards")
            if previous_frame == state["frame"] or not snapshots:
                require(state["rng_draws"] == observed_rng, "command/observation RNG boundary differs")
            observations(record)
            snapshots.append(record)
            observed_rng = state["rng_draws"]
            previous_frame = state["frame"]
    names = ["hold-alternative-place", "initial-firing-place", "patrol"]
    if role == "moving":
        names.append("cross-firing-line" if family == "obstructed" else "free-alternative-place")
    require([r["name"] for r in commands] == names, "command schedule/route differs")
    require(commands[0]["frame"] == commands[1]["frame"] == 0 and commands[0]["world"] == [220, 40] and commands[1]["world"] == [100, 100], "setup command inputs differ")
    patrol_frame = (200 + step - 1) // step
    require(commands[2]["frame"] == patrol_frame and commands[2]["from"] == [8, 8] and commands[2]["to"] == [48, 8], "Patrol input/boundary differs")
    expected_duplicates = {patrol_frame}
    if role == "moving":
        intervention = commands[-1]
        require(intervention["world"] == ([220, 140] if family == "obstructed" else [100, 500]), "intervention input differs")
        if family == "obstructed":
            require(intervention["frame"] == (1000 + step - 1) // step, "obstruction command schedule differs")
        else:
            require(intervention["time_ms"] == records[-1]["feedback_result"]["first_bad"], "measured bad-place intervention schedule differs")
        expected_duplicates.add(intervention["frame"])
    final_frame = (25000 + step - 1) // step
    counts = collections.Counter(r["state"]["frame"] for r in snapshots)
    require(set(counts) == set(range(final_frame + 1)), "missing completed map boundary")
    require(all(counts[frame] == (2 if frame in expected_duplicates else 1) for frame in counts), "missing/extra command-boundary snapshot")
    completion(records[-1], snapshots[-1])
    require(records[-1]["frame"] == final_frame, "observation horizon differs")
    return header


def altered(records, index, edit):
    result = list(records)
    result[index] = copy.deepcopy(records[index])
    edit(result[index])
    return result


def sensitivity(path, records, other, description, arguments):
    index = next(i for i, r in enumerate(records) if r["type"] == "snapshot")
    mutations = [lambda r, key=key: r["feedback"][0].__setitem__(key, r["feedback"][0][key] + 1) for key in ("place", "place_not_found", "enemy_count", "target", "attack_target", "target_change", "last_fire", "last_hit")]
    mutations += [lambda r: r["feedback"][0]["bad_places"].append(0), lambda r: r["feedback"][0].__setitem__("health_bits", r["feedback"][0]["health_bits"] ^ 1), lambda r: r["feedback"][0].__setitem__("moving", not r["feedback"][0]["moving"]), lambda r: r["feedback"][0]["move"].__setitem__(0, r["feedback"][0]["move"][0] + 1)]
    mutations += [lambda r, key=key: r.__setitem__(key, not r[key]) for key in ("war", "ally_on_ray")]
    count = 0
    for edit in mutations:
        candidate = altered(records, index, edit)
        difference = TRACES.compare_records(records, candidate)
        require(difference is not None and any(field in difference for field in ("feedback", "war", "ally_on_ray")), "custom observation mutation was ignored")
        count += 1
    candidate = altered(records, -1, lambda r: r["feedback_result"].__setitem__("final_health_bits", r["feedback_result"]["final_health_bits"] ^ 1))
    require("completed boundary" in TRACES.compare_records(records, candidate), "terminal observation mutation was ignored")
    count += 1
    for edit in (lambda r: r["feedback"].pop(), lambda r: r["feedback"][0].pop("last_fire")):
        bad = [altered(source, index, edit) for source in (records, other)]
        require(TRACES.compare_records(*bad) is None, "representative damaged pair should still compare equally")
        for candidate in bad:
            try:
                observations(candidate[index])
            except (ValueError, KeyError):
                pass
            else:
                raise ValueError("equally incomplete feedback passed independent validation")
            count += 1
    for tail in ("missing", "invalid", "duplicate", "boundary", "command-frame"):
        with path.open(encoding="utf-8") as stream:
            if tail == "command-frame":
                header, command = next(stream), json.loads(next(stream))
                del command["frame"]
                lines = itertools.chain((header, json.dumps(command)), stream)
            elif tail == "missing":
                lines = itertools.islice(stream, len(records) - 1)
            elif tail == "boundary":
                footer = dict(records[-1], frame=records[-1]["frame"] + 1)
                lines = itertools.chain(itertools.islice(stream, len(records) - 1), [json.dumps(footer)])
            else:
                lines = itertools.chain(stream, ["invalid JSON" if tail == "invalid" else json.dumps(records[-1])])
            try:
                TRACES.read_records(lines, str(path) + "/" + tail)
            except TRACES.TraceError:
                pass
            else:
                raise ValueError("invalid recording accepted: " + tail)
            count += 1
    for field in ("frame", "rng_draws_before"):
        command_index = next(i for i, r in enumerate(records) if r["type"] == "command" and r["name"] == "patrol")
        candidate = altered(records, command_index, lambda r: r.__setitem__(field, r[field] + 1))
        try:
            validate(candidate, description, arguments)
        except ValueError:
            pass
        else:
            raise ValueError("false actual command boundary accepted: " + field)
        count += 1
    return count


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
    require(len(arguments.revision) == 40 and all(c in "0123456789abcdef" for c in arguments.revision), "require full source revision")
    groups = list(pairs())
    expected = {name for _, names in groups for name in names}
    actual = {p.name for pattern in ("fire-ineffective-*.jsonl", "fire-obstructed-*.jsonl") for p in arguments.directory.glob(pattern)}
    require(actual == expected, f"expected 48 fire-feedback files; missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    profile = None
    comparisons = controls = 0
    for description, names in groups:
        recordings = []
        for name in names:
            with (arguments.directory / name).open(encoding="utf-8") as stream:
                records = TRACES.read_records(stream, name)
            header = validate(records, description, arguments)
            current = {key: header[key] for key in ("revision", "dirty", "configuration", "compiler", "compiler_version", "precision_control", "assert_off", "build_exe", "cheats", "delayed_damage", "group_counts", "place_containment", "zero_x_trace", "render", "audio", "viewport")}
            require(profile is None or current == profile, "corpus execution profiles differ")
            profile = current
            recordings.append(records)
        difference = TRACES.compare_records(*recordings)
        require(difference is None, names[0] + ": " + str(difference))
        comparisons += 1
        # All positives are checked; large negative-tail replays use one actual representative per context/family.
        if description[1:3] == ("pm", 10):
            controls += sensitivity(arguments.directory / names[0], *recordings, description, arguments)
        print("Validated " + "/".join(map(str, description)))
    print(f"Validated 48 real recordings, {comparisons} same-context pairs and {controls} representative controls; no reference acceptance implied.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, UnicodeError, ValueError, KeyError, IndexError, TypeError) as error:
        print("Fire-feedback validation failed: " + str(error), file=sys.stderr)
        sys.exit(1)
