"""Check the existing finite repair/bomb target lifecycles through strict schema-1 files."""
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
VARIANTS = [("repair", "patient", ending) for ending in ("complete", "remove", "stop")] + [
    ("bomb", "friendly", "complete"), ("bomb", "enemy", "complete"), ("bomb", "ground", "complete"),
    ("bomb", "friendly", "remove"), ("bomb", "enemy", "remove"),
    ("bomb", "enemy", "stop"), ("bomb", "ground", "stop"),
]
REPAIR, BOMB, ORDERING = 1 << 23, 1 << 22, 1 << 5
LIVE_FIELDS = {"x_bits", "y_bits", "side", "state", "health_bits", "in_place", "group", "order", "target",
               "target_is_null", "destination", "queue"}


def require(condition, message):
    if not condition:
        raise TRACES.TraceError(message)


def integer(value, minimum=-0x80000000, maximum=0x7fffffff):
    return TRACES._integer(value, minimum=minimum, maximum=maximum)


def bits(value):
    return TRACES._integer(value, maximum=0xffffffff)


def point(value):
    return type(value) is list and len(value) == 2 and all(integer(x) for x in value)


def object_id(value):
    return integer(value, -1, 4)


def floating(value):
    return struct.unpack("<f", struct.pack("<I", value))[0]


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def inventory():
    for family, target, ending in VARIANTS:
        for rng in ("pm", "crt"):
            for step in (10, 16):
                for route in ("keyboard", "button"):
                    name = f"player-target-{family}-{target}-{ending}-{rng}-{step}-{route}.jsonl"
                    yield name, dict(family=family, target=target, ending=ending, rng=rng, step=step, route=route)


def profile(revision=None, configuration=None, compiler=None, target=None, cheats=None):
    values = (revision, configuration, compiler, target, cheats)
    if all(value is None for value in values):
        return None
    require(all(value is not None for value in values), "supply revision, configuration, compiler, target and cheats")
    require(full_revision(revision), "revision must be a full lowercase 40-hex commit ID")
    require(configuration in ("Debug", "Release") and compiler in ("MinGW", "MSVC"), "invalid build profile")
    require(target in ("EXE", "DLL") and type(cheats) is bool, "invalid role/cheats profile")
    return dict(configuration=configuration, compiler=compiler, build_exe=target == "EXE", cheats=cheats, dirty=0)


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def validate_profile(header, expected, revision):
    if expected is None:
        return
    require(full_revision(header.get("revision")), "missing/invalid compiled revision")
    for key, value in dict(expected, **({"revision": revision} if revision is not None else {})).items():
        require(type(header.get(key)) is type(value) and header[key] == value, "compiled profile mismatch: " + key)


def session_state(state):
    require(type(state) is dict and type(state.get("robots")) is list, "missing Session observations")
    require(TRACES._integer(state.get("rng_draws"), maximum=0xffffffffffffffff), "invalid uint64 RNG counter")
    robots = state["robots"]
    require([r.get("id") for r in robots] == [1, 2, 3, 4] and
            all(type(r.get("id")) is int for r in robots), "Session actor identity inventory differs")
    for actor in robots:
        require(type(actor.get("alive")) is bool, "Session alive must be boolean")
        if not actor["alive"]:
            require("queue" not in actor, "dead Session actor must not have a queue")
            continue
        require(LIVE_FIELDS <= actor.keys(), "live Session fields missing")
        require(all(bits(actor[k]) for k in ("x_bits", "y_bits", "health_bits")), "invalid Session float bits")
        require(all(integer(actor[k]) for k in ("side", "state", "group", "order")), "invalid Session integer")
        require(integer(actor["order"], -1, 9), "invalid actor player order")
        require(all(type(actor[k]) is bool for k in ("in_place", "target_is_null")), "invalid Session flag")
        require(object_id(actor["target"]) and point(actor["destination"]), "invalid Session target/destination")
        require(type(actor["queue"]) is list and len(actor["queue"]) <= 5, "invalid Session queue")
        for order in actor["queue"]:
            require(type(order) is list and len(order) == 5 and all(integer(order[i], 0, 8) for i in (0, 1)) and
                    all(bits(order[i]) for i in (2, 3)) and object_id(order[4]), "invalid five-field Session order")
    groups = state.get("player_groups")
    require(type(groups) is list, "missing Session groups")
    slots = []
    for group in groups:
        require(type(group) is dict and {"slot", "stored_count", "members", "order", "target"} <= group.keys(),
                "missing Session group fields")
        require(integer(group["slot"], 0) and integer(group["stored_count"]) and integer(group["members"], 0) and
                integer(group["order"], 0, 9) and object_id(group["target"]), "invalid Session group fields")
        slots.append(group["slot"])
    require(len(slots) == len(set(slots)), "duplicate Session group slot")


def observation(record, spec):
    state = record["state"]
    session_state(state)
    value = record.get("target_lifecycle")
    fields = {"preorder_flags", "actor_core", "actor", "peer", "selected_orders", "heals", "damage_events",
              "interrupted_at", "requested_at", "dying_at", "retired_at", "destination", "stop_destination"}
    require(type(value) is dict and fields <= value.keys(), "target lifecycle observations missing")
    flags = value["preorder_flags"]
    require(bits(flags) and not flags & ~(REPAIR | BOMB | ORDERING), "invalid preorder mask")
    require(integer(value["actor_core"], 0, 1), "invalid actor core identity")
    actor = value["actor"]
    require(type(actor) is dict and all(type(actor.get(k)) is bool for k in ("present", "live")), "invalid actor flags")
    require(actor["present"] == (value["actor_core"] == 1) and (not actor["live"] or actor["present"]),
            "actor object/core lifetime differs")
    require(actor["live"] is state["robots"][0]["alive"], "actor/Session live status differs")
    if actor["present"]:
        require(integer(actor.get("state")) and type(actor.get("must_die")) is bool, "actor death state missing")
        require(actor["live"] == (actor["state"] != 5), "DIP/live state differs")
    else:
        require(not ({"state", "must_die"} & actor.keys()), "retired actor retains object state")
    live_fields = {"x_bits", "y_bits", "health_bits", "environment_target", "fire_order"}
    if actor["live"]:
        require(live_fields <= actor.keys() and all(bits(actor[k]) for k in ("x_bits", "y_bits", "health_bits")) and
                object_id(actor["environment_target"]) and type(actor["fire_order"]) is bool,
                "live actor observations missing/invalid")
        require(all(actor[k] == state["robots"][0][k] for k in ("x_bits", "y_bits", "health_bits")),
                "actor/Session physical observations differ")
    else:
        require(not live_fields & actor.keys(), "dead actor exposes live-layout observations")
    peer = value["peer"]
    peer_id = 2 if spec["family"] == "repair" else 3
    require(type(peer) is dict and type(peer.get("id")) is int and peer["id"] == peer_id and
            all(type(peer.get(k)) is bool for k in ("present", "live")), "invalid peer identity/lifetime")
    require(not peer["live"] or peer["present"], "live peer has no object")
    require(peer["live"] is state["robots"][peer_id - 1]["alive"], "peer/Session lifetime differs")
    if peer["present"]:
        require(integer(peer.get("state")), "peer object state missing")
    else:
        require("state" not in peer, "retired peer retains object state")
    peer_live_fields = {"health_bits", "max_health_bits", "need_repair"}
    if peer["live"]:
        require(peer_live_fields <= peer.keys() and bits(peer["health_bits"]) and bits(peer["max_health_bits"]) and
                type(peer["need_repair"]) is bool, "live peer observations missing/invalid")
        require(peer["health_bits"] == state["robots"][peer_id - 1]["health_bits"], "peer/Session health differs")
        require(peer["max_health_bits"] == float_bits(5), "authored peer maximum health differs")
    else:
        require(not peer_live_fields & peer.keys(), "dead peer exposes live-layout observations")
    selected = value["selected_orders"]
    require(type(selected) is list and len(selected) == 2, "selected order observations missing")
    for index, selected_actor in enumerate(selected):
        require(type(selected_actor) is dict and type(selected_actor.get("id")) is int and
                selected_actor["id"] == index + 1 and type(selected_actor.get("live")) is bool,
                "invalid selected actor identity")
        require(selected_actor["live"] is state["robots"][index]["alive"], "selected actor lifetime differs")
        if not selected_actor["live"]:
            require("orders" not in selected_actor, "dead actor exposes live orders")
            continue
        orders = selected_actor.get("orders")
        require(type(orders) is list and len(orders) <= 5, "full selected orders missing")
        for order in orders:
            require(type(order) is list and len(order) == 7 and all(integer(order[i], 0, 8) for i in (0, 1)) and
                    all(bits(order[i]) for i in (2, 3, 4)) and integer(order[5]) and object_id(order[6]),
                    "invalid seven-field selected order")
        require([[o[0], o[1], o[2], o[3], o[6]] for o in orders] == state["robots"][index]["queue"],
                "full/shared order projection differs")
        if index == 0:
            require(actor["fire_order"] == any(o[0] == 5 for o in orders), "fire order observation differs")
    for key in ("heals", "damage_events", "interrupted_at", "requested_at", "dying_at", "retired_at"):
        require(integer(value[key], 0), "invalid lifecycle counter/time: " + key)
    require(point(value["destination"]) and point(value["stop_destination"]), "invalid saved destination")
    return value


def command_specs(spec):
    routed = "button" if spec["route"] == "button" else "key"
    key = lambda value: ord(value) if routed == "key" else 0
    if spec["family"] == "repair":
        commands = [("repair-preorder", routed, key("R"), 0, [-1, -1], True),
                    ("cancel-preorder", "key", ord("X"), 0, [-1, -1], True),
                    ("late-patient-click", "click", 0, 2, [-1, -1], False),
                    ("repair-preorder", routed, key("R"), 0, [-1, -1], True),
                    ("invalid-ground", "click", 0, 0, [500, 300], False),
                    ("enemy-click", "click", 0, 3, [-1, -1], False),
                    ("patient-click", "click", 0, 2, [-1, -1], True)]
    else:
        ground = spec["target"] == "ground"
        commands = [("bomb-preorder", routed, key("E"), 0, [-1, -1], True),
                    ("cancel-preorder", "key", ord("X"), 0, [-1, -1], True),
                    ("late-ground-click", "click", 0, 0, [500, 500], False),
                    ("bomb-preorder", routed, key("E"), 0, [-1, -1], True),
                    ("bomb-target-click", "click", 0, 0 if ground else 3,
                     [500, 500] if ground else [-1, -1], True)]
    if spec["ending"] == "remove":
        commands.append(("remove-patient" if spec["family"] == "repair" else "remove-target", "delete", 0,
                         2 if spec["family"] == "repair" else 3, [-1, -1], True))
    elif spec["ending"] == "stop":
        commands.append(("replace-stop", routed, key("S"), 0, [-1, -1], True))
    return commands


def validate(records, spec):
    header = records[0]
    repair = spec["family"] == "repair"
    horizon = 2000 if repair else 30000
    step = spec["step"]
    expected = dict(fixture="repair-target-lifecycle-v1" if repair else "bomb-target-lifecycle-v1",
                    recording="player-target-lifecycle-v1", target_kind=spec["target"], ending=spec["ending"],
                    route=spec["route"], rng="ParkMiller" if spec["rng"] == "pm" else "LegacyCRT", seed=80,
                    step_ms=step, horizon_ms=horizon, max_frames=1000 if repair else 4000,
                    audio="unavailable", render="storage-device-no-draw-cursor-surrogate",
                    viewport=[1280, 720], initially_selected=True)
    for key, value in expected.items():
        require(type(header.get(key)) is type(value) and header[key] == value, "fixture/header mismatch: " + key)
    require(point(header["viewport"]), "viewport must retain integer coordinates")
    require(header.get("navigation_layout", "central-obstacle-v1") == "central-obstacle-v1", "layout differs")
    for key in ("revision", "compiler_version", "delayed_damage", "group_counts", "place_containment", "zero_x_trace"):
        require(type(header.get(key)) is str and bool(header[key]), "profile metadata missing: " + key)
    require(type(header.get("dirty")) is int and header["dirty"] in (-1, 0, 1), "invalid source dirty marker")
    for key in ("assert_off", "build_exe", "cheats"):
        require(type(header.get(key)) is bool, "invalid compiled boolean: " + key)
    commands = command_specs(spec)
    interrupted = spec["ending"] != "complete"
    prefix = 7 if repair else 5
    intervention = ((300 if repair else 1000) + step - 1) // step
    index = 1
    previous = None
    snapshots = []
    def snapshot(phase, frame):
        nonlocal index, previous
        require(index < len(records), "missing snapshot")
        record = records[index]
        require(record["type"] == "snapshot" and record.get("phase") == phase, "snapshot phase/order differs")
        require(record["state"]["frame"] == frame and record["state"]["time_ms"] == frame * step,
                "snapshot frame/time differs")
        observation(record, spec)
        snapshots.append(record)
        previous = record
        index += 1
        return record
    def command(expected_command, frame):
        nonlocal index
        require(index < len(records), "missing command")
        record = records[index]
        require(record["type"] == "command", "command ordering differs")
        fields = ("name", "input", "key", "target", "point", "accepted")
        for field, value in zip(fields, expected_command):
            require(type(record.get(field)) is type(value) and record[field] == value,
                    "command field differs: " + field)
        require(point(record["point"]) and type(record["key"]) is int and type(record["target"]) is int,
                "invalid command coordinates/identities")
        require(type(record.get("frame")) is int and record["frame"] == frame and
                type(record.get("time_ms")) is int and record["time_ms"] == frame * step, "command boundary differs")
        before, after = record.get("rng_draws_before"), record.get("rng_draws_after")
        require(TRACES._integer(before, maximum=0xffffffffffffffff) and
                TRACES._integer(after, maximum=0xffffffffffffffff) and after >= before and
                before == previous["state"]["rng_draws"], "command RNG boundary differs")
        index += 1
        result = snapshot("command", frame)
        require(result["state"]["rng_draws"] == after, "command/post-state RNG differs")
        name = record["name"]
        value = result["target_lifecycle"]
        flags = value["preorder_flags"]
        if name.endswith("preorder") and name != "cancel-preorder":
            require(flags & (REPAIR if repair else BOMB), "preorder not enabled")
        if name in ("invalid-ground", "enemy-click"):
            require(flags & REPAIR, "rejected repair target lost preorder")
        if name in ("late-patient-click", "late-ground-click", "patient-click", "bomb-target-click"):
            require(flags == 0, "accepted/canceled target retained preorder")
        if name in ("patient-click", "bomb-target-click"):
            actor = result["state"]["robots"][0]
            require(actor["order"] == (5 if repair else 6) and actor["target"] == record["target"],
                    "target dispatch did not retain command")
        if name in ("late-patient-click", "late-ground-click", "invalid-ground", "enemy-click"):
            require(TRACES._first_difference(result["state"], snapshots[0]["state"]) is None,
                    "rejected/canceled target changed the prior Session state")
        if name == "bomb-target-click":
            require(value["destination"] == result["state"]["robots"][0]["destination"],
                    "saved bomb destination differs from dispatch")
        if name == "replace-stop":
            actor = result["state"]["robots"][0]
            require(actor["alive"] and actor["order"] == 0 and actor["target_is_null"], "Stop dispatch differs")
            if not repair:
                require(actor["destination"] == value["stop_destination"], "saved Stop destination differs")
        if name in ("remove-patient", "remove-target"):
            require(not value["peer"]["present"] and not value["peer"]["live"], "deleted peer remains present")
        if frame > 0:
            require(value["interrupted_at"] == frame * step, "intervention time not recorded")
        return result
    snapshot("initial", 0)
    for expected_command in commands[:prefix]:
        command(expected_command, 0)
    for frame in range(1, horizon // step + 1):
        snapshot("tick", frame)
        if interrupted and frame == intervention:
            value = previous["target_lifecycle"]
            actor = previous["state"]["robots"][0]
            require(actor["alive"] and actor["order"] == (5 if repair else 6), "interruption missed active command")
            if repair:
                require(value["peer"]["live"] and floating(value["peer"]["health_bits"]) > 3 and
                        value["peer"]["need_repair"], "repair interruption missed partial healing")
            else:
                live = value["actor"]
                require(value["actor_core"] == 1 and not live["must_die"] and
                        (floating(live["x_bits"]) - 100) ** 2 + (floating(live["y_bits"]) - 200) ** 2 > 100,
                        "bomb interruption missed physical progress before detonation")
            command(commands[-1], frame)
    require(index == len(records) - 1 and records[index]["type"] == "complete", "extra/missing episode records")
    footer = records[index]
    last = snapshots[-1]
    require(footer["frame"] == horizon // step and footer["time_ms"] == horizon, "completion horizon differs")
    require(TRACES._first_difference(footer.get("result"), last["target_lifecycle"]) is None,
            "completion result differs from final observed state")
    validate_lifecycle(snapshots, spec, intervention * step)


def validate_lifecycle(snapshots, spec, intervention):
    repair = spec["family"] == "repair"
    ending = spec["ending"]
    last = snapshots[-1]
    final = last["target_lifecycle"]
    require(final["interrupted_at"] == (0 if ending == "complete" else intervention), "intervention milestone differs")
    previous_peer = float_bits(3 if repair else 5)
    changes = 0
    first_request = first_dying = first_retirement = 0
    interrupted_at = 0
    stop_position = None
    for record in snapshots:
        time = record["state"]["time_ms"]
        value = record["target_lifecycle"]
        actor, peer = value["actor"], value["peer"]
        if ending != "complete" and record["phase"] == "command" and time == intervention:
            interrupted_at = intervention
        require(value["interrupted_at"] == interrupted_at, "intervention history differs")
        if peer["live"] and peer["health_bits"] != previous_peer:
            changes += 1
            expected = (30 + 4 * changes) / 10 if repair else 4.6 if spec["target"] == "friendly" else 4.0
            require(peer["health_bits"] == float_bits(expected), "repair/damage pulse amount differs")
            previous_peer = peer["health_bits"]
        require(value["heals" if repair else "damage_events"] == changes, "health-event counter differs")
        if repair:
            require(value["damage_events"] == 0 and all(value[k] == 0 for k in
                    ("requested_at", "dying_at", "retired_at")), "repair has bomb milestones")
        else:
            require(value["heals"] == 0, "bomb has repair milestones")
            if actor["present"] and actor["must_die"] and not first_request:
                first_request = time
                require(actor["live"] and (floating(actor["x_bits"]) - 500) ** 2 +
                        (floating(actor["y_bits"]) - 500) ** 2 < 150 ** 2, "detonation request outside target area")
            if actor["present"] and actor["state"] == 5 and not first_dying:
                first_dying = time
            if not value["actor_core"] and not first_retirement:
                first_retirement = time
            require((value["requested_at"], value["dying_at"], value["retired_at"]) ==
                    (first_request, first_dying, first_retirement), "request/DIP/core milestones differ")
        if interrupted_at and ending == "stop":
            first = record["state"]["robots"][0]
            require(actor["live"] and not actor["must_die"] and first["order"] == 0 and first["target_is_null"],
                    "replacement Stop lost its live targetless contract")
            if not repair:
                require(first["destination"] == value["stop_destination"], "replacement Stop destination changed")
        if ending == "remove" and time >= intervention + 100 + spec["step"]:
            first = record["state"]["robots"][0]
            if repair:
                require(first["order"] == 0 and first["target_is_null"] and actor["environment_target"] == 0 and
                        not actor["fire_order"], "removed repair target retained intent/fire/reference")
            elif not value["requested_at"]:
                require(first["order"] == 6 and first["target_is_null"] and
                        first["destination"] == value["destination"],
                        "removed bomb target lost saved destination/intent")
        if not repair and ending == "stop" and record["phase"] == "tick":
            position = (floating(actor["x_bits"]), floating(actor["y_bits"]))
            if time >= 25000:
                require(position == stop_position, "late Stop movement continued")
            stop_position = position
    if repair:
        require(final["actor"]["live"] and last["state"]["robots"][0]["order"] == 0 and
                last["state"]["robots"][0]["target_is_null"], "repair did not return to Stop")
        if ending == "remove":
            require(0 < changes < 5 and not final["peer"]["present"], "removed patient lifetime differs")
        else:
            require(changes == 5 and final["peer"]["live"] and not final["peer"]["need_repair"],
                    "repair did not finish five pulses, including autonomous Stop repair")
    elif ending == "stop":
        require(not first_request and not first_dying and not first_retirement and changes == 0 and
                final["peer"]["live"], "canceled bomb exploded or lost its peer")
    else:
        require(1000 < first_request < first_dying < first_retirement <= 30000, "bomb lifecycle did not complete")
        survivor = last["state"]["robots"][1]
        require(survivor["alive"] and survivor["order"] == 0 and survivor["target_is_null"], "survivor did not Stop")
        require((not final["peer"]["present"] if ending == "remove" else changes == 1 and final["peer"]["live"]),
                "bomb peer damage/retirement differs")


def load(path, spec, expected=None, revision=None):
    with path.open(encoding="utf-8") as stream:
        records = TRACES.read_records(stream, str(path))
    validate(records, spec)
    validate_profile(records[0], expected, revision)
    return records


def controls(records, spec):
    """Use two bounded real-corpus representatives without rereading their files."""
    bad = copy.deepcopy(records[1])
    del bad["target_lifecycle"]["actor_core"]
    try:
        observation(bad, spec)
    except TRACES.TraceError:
        pass
    else:
        raise TRACES.TraceError("correlated omission of core lifetime could pass")
    bad = copy.deepcopy(records[1])
    bad["target_lifecycle"]["actor_core"] = True
    try:
        observation(bad, spec)
    except TRACES.TraceError:
        pass
    else:
        raise TRACES.TraceError("boolean core identity could pass")
    missing_tick = list(records)
    del missing_tick[next(i for i, r in enumerate(records) if r.get("phase") == "tick")]
    try:
        validate(missing_tick, spec)
    except TRACES.TraceError:
        pass
    else:
        raise TRACES.TraceError("missing tick could pass")
    framing = [records[0], records[-2], records[-1], {"type": "unexpected"}]
    try:
        TRACES.read_records(io.StringIO("\n".join(map(json.dumps, framing))))
    except TRACES.TraceError:
        pass
    else:
        raise TRACES.TraceError("post-completion record could pass")
    return 4


def check(directory, reference=None, *, revision=None, configuration=None, compiler=None, target=None, cheats=None,
          reference_revision=None):
    expected = profile(revision, configuration, compiler, target, cheats)
    if reference_revision is not None:
        require(reference is not None and expected is not None and full_revision(reference_revision),
                "reference revision requires a directory, full strict profile and full commit ID")
    names = {name for name, _ in inventory()}
    require({path.name for path in directory.glob("player-target-*.jsonl")} == names,
            "target inventory must have 80 files")
    if reference is not None:
        require({path.name for path in reference.glob("player-target-*.jsonl")} == names, "reference inventory differs")
    fault_controls = 0
    for name, spec in inventory():
        records = load(directory / name, spec, expected, revision)
        if reference is not None:
            old = load(reference / name, spec, expected, reference_revision)
            difference = TRACES.compare_records(old, records)
            require(difference is None, name + ": " + str(difference))
        if (spec["rng"], spec["step"], spec["route"], spec["ending"]) == ("pm", 10, "keyboard", "complete") and (
                spec["family"] == "repair" or spec["target"] == "enemy"):
            fault_controls += controls(records, spec)
    print("Validated 80 complete target-lifecycle recordings; original 28 C++ route comparisons remain required"
          + f"; {fault_controls} representative fault controls"
          + ("; 80 exact corresponding reference comparisons" if reference is not None else ""))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference-directory", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", choices=("true", "false"))
    parser.add_argument("--reference-revision")
    arguments = parser.parse_args(argv)
    try:
        check(arguments.directory, arguments.reference_directory, revision=arguments.revision,
              configuration=arguments.configuration, compiler=arguments.compiler, target=arguments.target,
              cheats=None if arguments.cheats is None else arguments.cheats == "true",
              reference_revision=arguments.reference_revision)
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, IndexError) as error:
        print("Invalid target-lifecycle recordings: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
