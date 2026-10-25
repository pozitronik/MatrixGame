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
    return records


def full_revision(value):
    return type(value) is str and len(value) == 40 and all(c in "0123456789abcdef" for c in value)


def strict_profile(revision, configuration, compiler, target, cheats):
    supplied = (revision, configuration, compiler, target, cheats)
    if all(value is None for value in supplied):
        return None
    if any(value is None for value in supplied):
        raise ValueError("strict profile requires revision, configuration, compiler, target and cheats")
    if not full_revision(revision):
        raise ValueError("revision must be a full lowercase 40-hex commit ID")
    if configuration not in ("Debug", "Release") or compiler not in ("MinGW", "MSVC"):
        raise ValueError("invalid strict configuration/compiler")
    if target not in ("EXE", "DLL") or type(cheats) is not bool:
        raise ValueError("invalid strict target/cheats")
    return dict(configuration=configuration, compiler=compiler, build_exe=target == "EXE", cheats=cheats, dirty=0)


def validate_profile(header, source, profile, revision):
    if not full_revision(header.get("revision")):
        raise ValueError(f"{source}: header revision must be a full lowercase 40-hex commit ID")
    expected = dict(profile)
    if revision is not None:
        expected["revision"] = revision
    for field, value in expected.items():
        if type(header.get(field)) is not type(value) or header[field] != value:
            raise ValueError(f"{source}: header {field} disagrees with the strict compiled profile")


def must_reject(records, description):
    try:
        validated(records)
    except TRACES.TraceError:
        return
    raise ValueError(f"accepted {description}")


def check(directory, reference=None, *, revision=None, configuration=None, compiler=None, target=None,
          cheats=None, reference_revision=None):
    profile = strict_profile(revision, configuration, compiler, target, cheats)
    if reference_revision is not None:
        if reference is None or profile is None:
            raise ValueError("reference revision requires a reference directory and complete strict profile")
        if not full_revision(reference_revision):
            raise ValueError("reference revision must be a full lowercase 40-hex commit ID")
    count = 0
    fault_scenes = set()
    for names in expected_pairs():
        pair = [load(directory, name) for name in names]
        if profile is not None:
            for name, records in zip(names, pair):
                validate_profile(records[0], str(directory / name), profile, revision)
        difference = TRACES.compare_records(*pair)
        if difference:
            raise ValueError(f"{names}: {difference}")
        for name, records in zip(names, pair):
            if reference:
                old = load(reference, name)
                if profile is not None:
                    validate_profile(old[0], str(reference / name), profile, reference_revision)
                difference = TRACES.compare_records(old, records)
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
    parser.add_argument("--revision")
    parser.add_argument("--configuration", choices=("Debug", "Release"))
    parser.add_argument("--compiler", choices=("MinGW", "MSVC"))
    parser.add_argument("--target", choices=("EXE", "DLL"))
    parser.add_argument("--cheats", choices=("true", "false"))
    parser.add_argument("--reference-revision")
    args = parser.parse_args(argv)
    try:
        check(args.directory, args.reference_directory, revision=args.revision, configuration=args.configuration,
              compiler=args.compiler, target=args.target,
              cheats=None if args.cheats is None else args.cheats == "true", reference_revision=args.reference_revision)
    except (OSError, UnicodeError, ValueError, KeyError, StopIteration) as error:
        print(f"Invalid auto-order recordings: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
