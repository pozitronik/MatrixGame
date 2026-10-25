"""Check real auto-order recordings after the focused engine suite has run."""
import argparse
from pathlib import Path
import sys

from test_battle_traces import TRACES, text, validated


MILESTONES = (
    "paint_ms", "ownership_ms", "stopped_ms", "hit_ms", "retired_ms", "replacement_ms",
    "command_ms", "arrival_ms", "terminal_order", "terminal_x_bits", "terminal_y_bits",
)


def expected_pairs():
    for scene in ("capture", "attack", "defence"):
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for action, replays in (("override", (0, 1)), ("control", (0, 1)), ("control", (2, 3))):
                    stem = f"player-auto-{scene}-{action}-{rng}-{step}-"
                    yield tuple(stem + str(replay) + ".jsonl" for replay in replays)


def validate_queues(records, source):
    """Check the five-cell live Session orders before comparing replay pairs."""
    for line, record in enumerate(records, 1):
        if record["type"] != "snapshot":
            continue
        for index, robot in enumerate(record["state"]["robots"]):
            actor_path = f"{source}:{line}: $.state.robots[{index}]"
            if type(robot.get("alive")) is not bool:
                raise TRACES.TraceError(f"{actor_path}.alive must be a boolean")
            # Session::snapshot omits the queue once the actor is no longer live.
            if not robot["alive"]:
                if "queue" in robot:
                    raise TRACES.TraceError(f"{actor_path}.queue must be omitted for a dead actor")
                continue
            path = f"{actor_path}.queue"
            queue = robot.get("queue")
            if type(queue) is not list or len(queue) > 5:
                raise TRACES.TraceError(f"{path} must be an array of at most 5 orders")
            for slot, order in enumerate(queue):
                entry = f"{path}[{slot}]"
                if type(order) is not list or len(order) != 5:
                    raise TRACES.TraceError(f"{entry} must be a five-cell order array")
                # OrderType and OrderPhase both enumerate their active values from 0 to 8.
                for cell, name in ((0, "type"), (1, "phase")):
                    if not TRACES._integer(order[cell], maximum=8):
                        raise TRACES.TraceError(f"{entry}[{cell}] must be an integer {name} in 0..8")
                for cell in (2, 3):
                    if not TRACES._integer(order[cell], maximum=0xffffffff):
                        raise TRACES.TraceError(f"{entry}[{cell}] must be a uint32 float bit pattern")
                # Session normalizes null, external objects and its four actors to 0, -1 and 1..4.
                if not TRACES._integer(order[4], minimum=-1, maximum=4):
                    raise TRACES.TraceError(f"{entry}[4] must be an integer object ID in -1..4")


def load(directory, name):
    path = directory / name
    with path.open(encoding="utf-8") as stream:
        records = TRACES.read_records(stream, str(path))
    validate_queues(records, str(path))
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
