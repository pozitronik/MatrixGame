"""Check real auto-order recordings after the focused engine suite has run."""
import argparse
from pathlib import Path
import sys

from test_battle_traces import TRACES, text, validated


MILESTONES = (
    "paint_ms", "ownership_ms", "stopped_ms", "hit_ms", "retired_ms", "replacement_ms",
    "command_ms", "arrival_ms", "terminal_order", "terminal_x_bits", "terminal_y_bits",
)


def observation_int32(value):
    return TRACES._integer(value, minimum=-0x80000000, maximum=0x7fffffff)


def observation_object(value):
    # Custom observer identities differ from the shared Session queue identities.
    return type(value) is int and value in {-3, -1, 0, 1, 2, 3, 4, 5}


def validate_observations(records, source):
    action_fields = {"target", "attack_target", "enemies", "place", "additional_place", "last_fire", "last_hit",
                     "target_change", "failed_place", "moving", "move", "fire_order", "group_target", "war"}
    factory_fields = {"owner", "paint", "color", "capturer", "actor_capture", "opponent_capture",
                      "opponent_capture_order"}
    capture = records[0]["auto_order"] == "capture"
    for line, record in enumerate(records, 1):
        if record["type"] != "snapshot":
            continue
        flags = record.get("auto_flags")
        if not TRACES._integer(flags, maximum=0xffffffff) or flags & ~14:
            raise TRACES.TraceError(f"{source}:{line}: auto_flags must contain only AUTO mask bits 1, 2 and 3")
        selected = record.get("selected")
        if type(selected) is not list or not all(observation_object(value) for value in selected):
            raise TRACES.TraceError(f"{source}:{line}: selected must be an array of custom object identities")
        action = record.get("action")
        if type(action) is not dict or not action_fields <= action.keys():
            raise TRACES.TraceError(f"{source}:{line}: required action observations missing")
        if not all(observation_object(action[key]) for key in ("target", "attack_target", "group_target")):
            raise TRACES.TraceError(f"{source}:{line}: invalid action custom object identity")
        if not all(observation_int32(action[key]) for key in
                   ("enemies", "place", "last_fire", "last_hit", "target_change", "failed_place")):
            raise TRACES.TraceError(f"{source}:{line}: action counters, places and times must be int32")
        if not all(type(action[key]) is bool for key in ("moving", "fire_order", "war")):
            raise TRACES.TraceError(f"{source}:{line}: action movement, fire and War flags must be booleans")
        for key in ("additional_place", "move"):
            point = action[key]
            if type(point) is not list or len(point) != 2 or not all(observation_int32(value) for value in point):
                raise TRACES.TraceError(f"{source}:{line}: action {key} must be a two-int32 coordinate array")
        if not action["moving"] and action["move"] != [-1, -1]:
            raise TRACES.TraceError(f"{source}:{line}: inactive movement must retain its initialized [-1,-1] point")
        resources = record.get("resources")
        if type(resources) is not list or len(resources) != 8 or not all(observation_int32(x) for x in resources):
            raise TRACES.TraceError(f"{source}:{line}: resources must retain eight int32 balances")
        if not capture:
            if "factory" in record:
                raise TRACES.TraceError(f"{source}:{line}: only capture scenes record a factory")
            continue
        factory = record.get("factory")
        if type(factory) is not dict or not factory_fields <= factory.keys():
            raise TRACES.TraceError(f"{source}:{line}: required capture factory observations missing")
        if type(factory["owner"]) is not int or factory["owner"] not in (0, 1):
            raise TRACES.TraceError(f"{source}:{line}: capture factory owner must be neutral or player side")
        if not TRACES._integer(factory["paint"], maximum=14):
            raise TRACES.TraceError(f"{source}:{line}: capture factory paint must be an integer in 0..14")
        if not TRACES._integer(factory["color"], maximum=0xffffffff):
            raise TRACES.TraceError(f"{source}:{line}: capture factory color must be a uint32 DWORD")
        if not observation_object(factory["capturer"]):
            raise TRACES.TraceError(f"{source}:{line}: invalid factory capturer custom object identity")
        if not all(type(factory[key]) is bool for key in
                   ("actor_capture", "opponent_capture", "opponent_capture_order")):
            raise TRACES.TraceError(f"{source}:{line}: capture assignment/order flags must be booleans")


def expected_pairs():
    for scene in ("capture", "attack", "defence"):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for action, replays in (("override", (0, 1)), ("control", (0, 1)), ("control", (2, 3))):
                    stem = f"player-auto-{scene}-{action}-{rng}-{step}-"
                    yield tuple(stem + str(replay) + ".jsonl" for replay in replays)


def load(directory, name):
    path = directory / name
    with path.open(encoding="utf-8") as stream:
        records = TRACES.read_records(stream, str(path))
    if records[0]["fixture"] != "player-auto-order-continuation-v1":
        raise ValueError(f"{path}: unexpected fixture")
    _, _, scene, action, rng, step, _ = name.removesuffix(".jsonl").split("-")
    for field, expected in (("auto_order", scene), ("manual_override", action == "override"),
                            ("rng", {"pm": "ParkMiller", "crt": "LegacyCRT"}[rng]), ("step_ms", int(step))):
        if type(records[0].get(field)) is not type(expected) or records[0][field] != expected:
            raise ValueError(f"{path}: header {field} disagrees with the expected file profile")
    completion = records[-1]
    missing = set(MILESTONES) - completion.keys()
    if missing:
        raise ValueError(f"{path}: missing milestone fields {sorted(missing)}")
    for field in MILESTONES:
        if type(completion[field]) is not int:
            raise ValueError(f"{path}: noninteger milestone {field}")
    actor = next(robot for robot in records[-2]["state"]["robots"] if robot["id"] == 1)
    for field, snapshot_field in (("terminal_order", "order"), ("terminal_x_bits", "x_bits"),
                                  ("terminal_y_bits", "y_bits")):
        if completion[field] != actor[snapshot_field]:
            raise ValueError(f"{path}: {field} differs from final snapshot")
    validate_observations(records, str(path))
    return records


def must_reject(records, description):
    try:
        validated(records)
    except TRACES.TraceError:
        return
    raise ValueError(f"accepted {description}")


def check(directory, reference=None):
    count = 0
    fault_scenes = set()
    for names in expected_pairs():
        pair = [load(directory, name) for name in names]
        difference = TRACES.compare_records(*pair)
        if difference:
            raise ValueError(f"{names}: {difference}")
        for name, records in zip(names, pair):
            if reference:
                difference = TRACES.compare_records(load(reference, name), records)
                if difference:
                    raise ValueError(f"fresh-process {name}: {difference}")
            # Every real file is parsed above. Exercise derived parser faults on
            # one actual recording per scene, rather than reparsing every replay.
            scene = records[0]["auto_order"]
            if scene not in fault_scenes:
                must_reject(records[:-1], f"missing completion in {name}")
                must_reject(records[:len(records) // 2], f"well-formed truncation in {name}")
                old_tail = dict(records[-1], type="milestones")
                old_tail.pop("frame")
                must_reject(records[:-1] + [old_tail], f"obsolete milestone record in {name}")
                must_reject(records + [{"type": "unexpected"}], f"tail after completion in {name}")
                field = MILESTONES[len(fault_scenes)]
                changed = records[:-1] + [dict(records[-1], **{field: records[-1][field] + 1})]
                difference = TRACES.compare_records(records, validated(changed))
                if not difference or "$." + field not in difference:
                    raise ValueError(f"comparison missed milestone mutation {name}: {field}")
                fault_scenes.add(scene)
            count += 1
    print(f"Validated {count} real traces, {count // 2} exact replay pairs, "
          f"{len(fault_scenes)} scene representatives with {len(fault_scenes) * 4} rejected incomplete/obsolete tails "
          f"and {len(fault_scenes)} milestone mutations"
          + (f", {count} exact fresh-process comparisons" if reference else ""))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference-directory", type=Path)
    args = parser.parse_args(argv)
    try:
        check(args.directory, args.reference_directory)
    except (OSError, UnicodeError, ValueError, KeyError, StopIteration) as error:
        print(f"Invalid auto-order recordings: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
