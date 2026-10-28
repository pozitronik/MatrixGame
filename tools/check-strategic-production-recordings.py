#!/usr/bin/env python3
"""Validate the twelve existing strategic purchase, deployment and base-removal recordings."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import sys

spec = importlib.util.spec_from_file_location("battle_trace_reader",
                                             Path(__file__).with_name("compare-battle-traces.py"))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
ENDINGS = ("deploy", "wait-base-removed", "paid-base-removed")
PROFILE = ("revision", "dirty", "configuration", "compiler", "compiler_version", "precision_control", "assert_off",
           "build_exe", "cheats", "delayed_damage", "group_counts", "place_containment", "zero_x_trace", "trace",
           "render", "audio", "viewport")


def require(condition, message):
    if not condition:
        raise trace.TraceError(message)


def integer(value, low=0, high=0x7fffffff):
    return type(value) is int and low <= value <= high


def exact(value, expected, message):
    require(trace._first_difference(value, expected) is None, message)


def bits(value):
    require(integer(value, 0, 0xffffffff), "invalid float bits")
    result = struct.unpack("<f", struct.pack("<I", value))[0]
    require(math.isfinite(result), "nonfinite sampled float")
    return result


def queue(value, width, max_id):
    require(type(value) is list and len(value) <= 5, "invalid order queue length")
    for order in value:
        require(type(order) is list and len(order) == width, "invalid order shape")
        require(all(integer(x, 0, 8) for x in order[:2]), "invalid physical order type or phase")
        require(all(integer(x, 0, 0xffffffff) for x in order[2:4 if width == 5 else 5]), "invalid order float bits")
        if width == 7:
            require(integer(order[5], -0x80000000), "invalid Param4")
        require(integer(order[-1], -1, max_id), "invalid order object identity")


def session(state, frame, step):
    require(type(state) is dict, "missing Session state")
    exact([state.get("frame"), state.get("time_ms")], [frame, frame * step], "Session clock differs")
    require(integer(state.get("rng_draws"), 0, 0xffffffffffffffff), "invalid Session RNG")
    robots = state.get("robots")
    require(type(robots) is list and len(robots) == 4, "Session actor inventory differs")
    for index, robot in enumerate(robots, 1):
        require(type(robot) is dict, "invalid Session actor")
        exact(robot.get("id"), index, "Session actor identity differs")
        require(type(robot.get("alive")) is bool, "missing Session liveness")
        if not robot["alive"]:
            exact(set(robot), {"id", "alive"}, "dead Session actor has live storage")
            continue
        for field in ("x_bits", "y_bits", "health_bits"):
            bits(robot.get(field))
        require(integer(robot.get("state"), 0, 6) and robot["state"] != 5, "live Session actor is DIP or invalid")
        exact(robot.get("side"), 1 if index < 3 else 2, "Session actor side differs")
        require(integer(robot.get("group"), -1, 60), "invalid actor group")
        require(integer(robot.get("order"), -1, 9) and integer(robot.get("target"), -1, 4), "invalid logical order")
        require(type(robot.get("in_place")) is bool and type(robot.get("target_is_null")) is bool,
                "missing actor flags")
        require(type(robot.get("destination")) is list and len(robot["destination"]) == 2
                and all(integer(x, -0x80000000) for x in robot["destination"]), "invalid actor destination")
        queue(robot.get("queue"), 5, 4)
    groups = state.get("player_groups")
    require(type(groups) is list, "missing player groups")
    slots = []
    for group in groups:
        require(type(group) is dict and integer(group.get("slot"), 0, 60), "invalid player group slot")
        slots.append(group["slot"])
        require(all(integer(group.get(key)) for key in ("stored_count", "members")), "invalid group counts")
        require(integer(group.get("order"), 0, 9) and integer(group.get("target"), -1, 4), "invalid group order")
    require(len(slots) == len(set(slots)), "duplicate player group")


def inventory():
    for ending in ENDINGS:
        for suffix, rng in (("pm", "ParkMiller"), ("crt", "LegacyCRT")):
            for step in (10, 16):
                yield f"production-strategic-{ending}-{suffix}-{step}.jsonl", dict(ending=ending, rng=rng, step=step)


def schedule(ending, step):
    result = [("snapshot", "initial", 0), ("command", "choose", 0), ("snapshot", "choice", 0)]
    paid = 100 if step == 10 else 65
    if ending == "wait-base-removed":
        result += [("command", "delete-base", 0), ("snapshot", "base-deleted", 0)]
        begin, end = 1, 100
    else:
        for frame in range(1, paid + 1):
            result += [("snapshot", "tick", frame), ("command", "build", frame), ("snapshot", "strategy", frame)]
        result += [("snapshot", "paid", paid), ("command", "queue-recheck", paid),
                   ("snapshot", "queue-recheck", paid)]
        if ending == "deploy":
            result += [("snapshot", "tick", frame) for frame in range(paid + 1, (584 if step == 10 else 369) + 1)]
            return result
        result += [("command", "delete-base", paid), ("snapshot", "base-deleted", paid)]
        begin, end = paid + 1, paid + 100
    for frame in range(begin, end + 1):
        result += [("snapshot", "tick", frame), ("command", "build", frame), ("snapshot", "strategy", frame)]
    return result


def clean_source(header, revision):
    require(type(header.get("revision")) is str and re.fullmatch("[0-9a-f]{40}", header["revision"]),
            "source revision must be full lowercase hexadecimal")
    exact(header.get("dirty"), 0, "dirty compiled source identity")
    if revision is not None:
        exact(header["revision"], revision, "source revision differs")


def header_contract(header, ending, rng, step, identity):
    expected = dict(type="header", schema=1, fixture="synthetic-strategic-purchase-v1", seed=83,
                    production_observation="strategic-production-v1", ending=ending, rng=rng, step_ms=step,
                    max_frames=2000, initially_selected=False, audio="unavailable", viewport=[1280, 720],
                    navigation_layout="central-obstacle-v1",
                    render="storage-device-unavailable-icons-no-draw-cursor-surrogate")
    for key, value in expected.items():
        exact(header.get(key), value, "header." + key + " differs")
    require(header.get("configuration") in ("Debug", "Release") and header.get("compiler") in ("MinGW", "MSVC"),
            "invalid build profile")
    for key in ("build_exe", "cheats", "assert_off", "trace"):
        require(type(header.get(key)) is bool, "invalid header flag " + key)
    exact(header["assert_off"], header["configuration"] == "Release", "assert profile differs")
    require(integer(header.get("precision_control")), "missing precision control")
    version = header.get("compiler_version")
    require(integer(version, 1) if header["compiler"] == "MSVC" else type(version) is str and bool(version),
            "invalid compiler_version")
    for key in ("delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(header.get(key) in ("Corrected", "Original"), "invalid behavior profile " + key)
    for key, value in identity.items():
        if value is not None and key != "revision":
            field = "build_exe" if key == "target" else key
            exact(header.get(field), value == "EXE" if key == "target" else value, "header." + field + " differs")
    require(type(header.get("revision")) is str and re.fullmatch("[0-9a-f]{40}", header["revision"]),
            "missing or malformed compiled source revision")
    require(integer(header.get("dirty"), 0, 1), "invalid compiled source dirty flag")
    if identity.get("revision") is not None:
        clean_source(header, identity["revision"])


def validate(records, *, ending, rng, step, **identity):
    require(len(records) >= 3, "truncated recording")
    header_contract(records[0], ending, rng, step, identity)
    expected = schedule(ending, step)
    require(len(records) == len(expected) + 2, "record count differs")
    initial = records[1]
    initial_draws = initial["state"]["rng_draws"]
    initial_stat = initial["strategy"]["build_count"]
    paid = 1000 if step == 10 else 1040
    milestones = dict(paid=paid, spawned=1100 if step == 10 else 1152, opened=2340 if step == 10 else 2400,
                      detached=4590 if step == 10 else 4656, closed=5840 if step == 10 else 5904)
    observed_draws, observed_frame = initial_draws, 0
    previous_resources = [90] * 4
    previous_payouts = 0
    digest = 14695981039346656037
    snapshot_count = 0
    for record, (kind, name, frame) in zip(records[1:-1], expected):
        exact(record.get("type"), kind, "record ordering differs")
        time = frame * step
        if kind == "command":
            exact([record.get("name"), record.get("frame"), record.get("time_ms")], [name, frame, time],
                  "command schedule differs")
            before, after = record.get("rng_draws_before"), record.get("rng_draws_after")
            require(integer(before, 0, 0xffffffffffffffff) and integer(after, 0, 0xffffffffffffffff),
                    "invalid command RNG")
            exact(before, observed_draws, "command RNG boundary differs")
            exact(after, before + (name == "choose"), "strategic command consumed unexpected RNG")
            observed_draws, observed_frame = after, frame
            continue
        exact(record.get("phase"), name, "snapshot phase differs")
        state = record.get("state")
        session(state, frame, step)
        draws = state["rng_draws"]
        require(draws >= observed_draws, "snapshot RNG moved backwards")
        if frame == observed_frame:
            exact(draws, observed_draws, "same-frame RNG boundary differs")
        if ending != "deploy" or time <= paid:
            exact(draws, initial_draws + (name != "initial"), "decision/removal RNG interval differs")
        observed_draws, observed_frame = draws, frame
        resources = record.get("resources")
        require(type(resources) is list and len(resources) == 4 and all(integer(x) for x in resources),
                "invalid resource balances")
        require(resources == [resources[0]] * 4, "asymmetric fixture income")
        progress = record.get("progress")
        require(type(progress) is dict and all(integer(progress.get(k), -1) for k in ("payouts", *milestones)),
                "missing progress observations")
        purchased = ending != "wait-base-removed" and (time > paid or time == paid and name != "tick")
        paid_sample = purchased and (name != "strategy" or time > paid)
        for key, when in milestones.items():
            seen = (key == "paid" and paid_sample) or (ending == "deploy" and key != "paid" and time >= when)
            exact(progress[key], when if seen else -1, "milestone " + key + " differs")
        if ending == "wait-base-removed":
            exact(progress["payouts"], 0, "removed base paid income")
        elif time <= paid:
            delta = resources[0] - previous_resources[0]
            if name == "tick":
                require(delta in (0, 3), "income increment differs")
                previous_payouts += delta // 3
            elif name == "strategy" and time == paid:
                exact(delta, -120, "purchase debit differs")
            elif name == "base-deleted":
                exact(delta, 120, "purchase refund differs")
            else:
                exact(delta, 0, "unexpected resource change")
            exact(progress["payouts"], previous_payouts, "income payout observation differs")
        else:
            exact(progress["payouts"], 10, "post-purchase payout counter differs")
            if ending == "deploy":
                require(resources[0] - previous_resources[0] in (0, 3), "deployment income differs")
        previous_resources = resources
        deleted = ending == "wait-base-removed" and name not in ("initial", "choice") or (
            ending == "paid-base-removed" and (time > paid or name == "base-deleted"))
        if ending == "wait-base-removed":
            exact(resources, [90] * 4, "waiting-base deletion balances differ")
        if ending == "paid-base-removed" and deleted:
            exact(resources, [120] * 4, "queue refund differs")
        strategy = record.get("strategy")
        exact(strategy, dict(waiting=-1 if name == "initial" or purchased else 0,
                             last_built=0 if purchased else -1,
                             build_count=initial_stat + (ending == "deploy" and time >= milestones["spawned"])),
              "strategy state differs")
        base = record.get("base")
        require(type(base) is dict, "missing base observation")
        exact(base.get("id"), 5, "base identity differs")
        exact(base.get("present"), not deleted, "base lifetime differs")
        queued = purchased and not deleted and (ending != "deploy" or time < milestones["spawned"])
        if deleted:
            exact(set(base), {"id", "present"}, "retired base has live fields")
        else:
            for key, value in dict(in_logic=True, side=1, queue_count=int(queued), queue=[6] if queued else []).items():
                exact(base.get(key), value, "base." + key + " differs")
            require(integer(base.get("state"), 0, 3) and type(base.get("spawning")) is bool, "invalid base state")
            floor = bits(base.get("floor_bits"))
            require(0 <= floor <= 1, "base floor outside animation")
            if not purchased or time < milestones["spawned"]:
                exact([base["state"], floor, base["spawning"]], [0, 0.0, False], "closed base differs")
            elif ending == "deploy":
                if time < milestones["opened"]:
                    exact([base["state"], base["spawning"]], [1, True], "opening base differs")
                elif time < milestones["detached"]:
                    exact([base["state"], floor, base["spawning"]], [2, 1.0, True], "open base differs")
                elif time < milestones["closed"]:
                    exact([base["state"], base["spawning"]], [3, False], "closing base differs")
                else:
                    exact([base["state"], floor, base["spawning"]], [0, 0.0, False], "final closed base differs")
        robot = record.get("robot")
        created = purchased
        initialized = ending == "deploy" and time >= milestones["spawned"]
        robot_fields = dict(id=6, created=created, present=created and not deleted, initialized=initialized,
                            in_queue=queued, in_logic=initialized)
        require(type(robot) is dict, "missing robot observation")
        for key, value in robot_fields.items():
            exact(robot.get(key), value, "robot." + key + " differs")
        if not initialized:
            exact(set(robot), set(robot_fields), "opaque/retired robot exposes live fields")
        else:
            detached = time >= milestones["detached"]
            robot_state = 2 if detached else (1 if time >= milestones["opened"] else 0)
            for key, value in dict(alive=True, state=robot_state, side=1,
                                   base=0 if detached else 5, manual_disabled=not detached).items():
                exact(robot.get(key), value, "initialized robot." + key + " differs")
            bits(robot.get("x_bits"))
            bits(robot.get("y_bits"))
            queue(robot.get("queue"), 7, 6)
        if paid_sample:
            exact(record.get("assembly"), dict(price=[120] * 4, weapons=[[2, 2, 10], [3, 2, 10]]),
                  "assembly observations differ")
        else:
            require("assembly" not in record, "premature assembly sample")
        if ending == "wait-base-removed" and deleted:
            exact(record.get("capacity_sample"), 0, "deleted-base capacity sample missing")
        else:
            require("capacity_sample" not in record, "unexpected capacity sample")
        if ending == "deploy" and time >= milestones["detached"]:
            require(bits(record.get("detach_distance_squared_bits")) >= 70 * 70, "detachment sample differs")
        else:
            require("detach_distance_squared_bits" not in record, "premature detachment sample")
        payload = {key: value for key, value in record.items() if key != "type"}
        for byte in (json.dumps(payload, separators=(",", ":")) + "\n").encode():
            digest = ((digest ^ byte) * 1099511628211) & 0xffffffffffffffff
        snapshot_count += 1
    final = records[-2]["state"]
    complete = records[-1]
    exact(complete.get("type"), "complete", "completion missing")
    for key in ("frame", "time_ms", "rng_draws"):
        exact(complete.get(key), final[key], "completion " + key + " differs")
    exact(complete.get("observations"), snapshot_count, "completion count differs")
    exact([complete.get("observation_digest_low"), complete.get("observation_digest_high")],
          [digest & 0xffffffff, digest >> 32], "observation digest differs")
    exact(final["rng_draws"], initial_draws + (4 if ending == "deploy" else 1), "terminal RNG differs")
    if ending == "deploy":
        exact(records[-2]["resources"], [144 if step == 10 else 138] * 4, "terminal resources differ")


def read(path):
    with path.open(encoding="utf-8") as stream:
        return trace.read_records(stream, str(path))


def check_directory(directory, *, reference=None, reference_revision=None, **identity):
    require(reference is not None or reference_revision is None, "reference revision needs reference directory")
    for revision in (identity.get("revision"), reference_revision):
        require(revision is None or re.fullmatch("[0-9a-f]{40}", revision), "revision pin must be full hexadecimal")
    expected = {name for name, _ in inventory()}
    for folder in (directory,) if reference is None else (directory, reference):
        actual = {p.name for p in folder.glob("production-strategic-*.jsonl")}
        require(actual == expected, "strategic recording inventory differs")
    profiles = [None, None]
    for name, profile in inventory():
        candidate = read(directory / name)
        validate(candidate, **profile, **identity)
        sequences = [candidate]
        if reference is not None:
            old = read(reference / name)
            reference_identity = dict(identity, revision=reference_revision)
            validate(old, **profile, **reference_identity)
            clean_source(old[0], reference_revision)
            clean_source(candidate[0], identity.get("revision"))
            require(trace.compare_records(old, candidate) is None, name + ": reference divergence")
            sequences.append(old)
        for index, records in enumerate(sequences):
            current = {key: records[0].get(key) for key in PROFILE}
            if profiles[index] is not None:
                exact(current, profiles[index], "mixed corpus source/build identity")
            profiles[index] = current
    if identity.get("revision") is None:
        print("Candidate revision is unpinned; validation is diagnostic rather than final baseline evidence.",
              file=sys.stderr)
    if reference is not None and reference_revision is None:
        print("Reference revision is unpinned; comparison is diagnostic until the baseline reference is selected.",
              file=sys.stderr)
    return len(expected)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--reference-revision")
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", choices=("true", "false"))
    parser.add_argument("--trace", choices=("true", "false"))
    args = vars(parser.parse_args(argv))
    for field in ("cheats", "trace"):
        args[field] = None if args[field] is None else args[field] == "true"
    try:
        count = check_directory(**args)
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        print(f"Strategic-production validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {count} existing strategic-production recordings; no replay pairs are claimed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
