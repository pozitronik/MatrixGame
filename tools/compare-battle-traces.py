#!/usr/bin/env python3
"""Compare schema-1 battle JSONL traces without tolerances or skipped commands."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys


class TraceError(ValueError):
    """A trace does not satisfy the supported recording contract."""


def _integer(value, minimum=0, maximum=None):
    return type(value) is int and value >= minimum and (maximum is None or value <= maximum)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise TraceError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _constant(value):
    raise TraceError(f"non-finite JSON number {value}")


def _float(value):
    number = float(value)
    if not math.isfinite(number):
        raise TraceError(f"non-finite JSON number {value}")
    return number


def _bits(value, path="$"):
    if isinstance(value, dict):
        for key, item in value.items():
            if key.endswith("_bits") and not _integer(item, maximum=0xffffffff):
                raise TraceError(f"{path}.{key} must be a uint32 float bit pattern")
            _bits(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _bits(item, f"{path}[{index}]")


def read_records(stream, source="trace"):
    """Validate every record, including the tail, before comparing two recordings."""
    records = []
    snapshots = 0
    previous_frame = -1
    previous_time = -1
    previous_draws = -1
    complete = False
    for line_number, line in enumerate(stream, 1):
        try:
            record = json.loads(line, object_pairs_hook=_object, parse_constant=_constant, parse_float=_float)
            if not isinstance(record, dict):
                raise TraceError("record must be an object")
            kind = record.get("type")
            if complete:
                raise TraceError("record after completion")
            if line_number == 1:
                if kind != "header" or type(record.get("schema")) is not int or record["schema"] != 1:
                    raise TraceError("first record must be a schema-1 header")
                for key in ("fixture", "rng", "configuration", "compiler"):
                    if not isinstance(record.get(key), str) or not record[key]:
                        raise TraceError(f"header requires nonempty {key}")
                for key in ("seed", "precision_control"):
                    if not _integer(record.get(key)):
                        raise TraceError(f"header requires nonnegative integer {key}")
                if not _integer(record.get("step_ms"), minimum=1):
                    raise TraceError("header requires positive integer step_ms")
            elif kind == "command":
                if not _integer(record.get("frame")) or not isinstance(record.get("name"), str) or not record["name"]:
                    raise TraceError("command requires nonnegative frame and nonempty name")
                if record["frame"] < previous_frame:
                    raise TraceError("command frame moves backwards")
                previous_frame = record["frame"]
            elif kind == "snapshot":
                state = record.get("state")
                if not isinstance(state, dict):
                    raise TraceError("snapshot requires a state object")
                for key in ("frame", "time_ms", "rng_draws"):
                    if not _integer(state.get(key)):
                        raise TraceError(f"snapshot requires nonnegative integer {key}")
                if state["frame"] < previous_frame or state["time_ms"] < previous_time or state["rng_draws"] < previous_draws:
                    raise TraceError("snapshot frame, time or RNG count moves backwards")
                if not isinstance(state.get("robots"), list) or any(not isinstance(r, dict) for r in state["robots"]):
                    raise TraceError("snapshot requires a robots array of objects")
                previous_frame, previous_time, previous_draws = state["frame"], state["time_ms"], state["rng_draws"]
                snapshots += 1
            elif kind == "complete":
                if not _integer(record.get("frame")) or not _integer(record.get("time_ms")):
                    raise TraceError("completion requires integer frame and time_ms")
                if not snapshots or record.get("frame") != previous_frame or record.get("time_ms") != previous_time:
                    raise TraceError("completion must match the last snapshot boundary")
                if records[-1]["type"] != "snapshot":
                    raise TraceError("completion must immediately follow the last snapshot")
                complete = True
            else:
                raise TraceError(f"unexpected record type {kind!r}")
            _bits(record)
            records.append(record)
        except (ValueError, TypeError) as error:
            raise TraceError(f"{source}:{line_number}: {error}") from error
    if not records:
        raise TraceError(f"{source}: empty trace")
    if not snapshots:
        raise TraceError(f"{source}: trace contains no snapshots")
    if not complete:
        raise TraceError(f"{source}: missing completion record; the run may have failed or been truncated")
    return records


_MISSING = object()
_PROVENANCE_FIELDS = {"revision", "dirty"}
_BUILD_PROFILE_FIELDS = {
    "configuration", "compiler", "compiler_version", "assert_off",
    "precision_control", "build_exe", "cheats", "delayed_damage", "group_counts",
    "place_containment",
    "zero_x_trace",
}


def _first_difference(left, right, path="$"):
    """Compare fields alphabetically within the earliest differing record."""
    if type(left) is not type(right):
        return path, left, right
    if isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            if key not in left or key not in right:
                return f"{path}.{key}", left.get(key, _MISSING), right.get(key, _MISSING)
            difference = _first_difference(left[key], right[key], f"{path}.{key}")
            if difference:
                return difference
    elif isinstance(left, list):
        for index, (a, b) in enumerate(zip(left, right)):
            difference = _first_difference(a, b, f"{path}[{index}]")
            if difference:
                return difference
        if len(left) != len(right):
            return f"{path}.length", len(left), len(right)
    elif left != right:
        return path, left, right
    return None


def _value(value, path):
    if value is _MISSING:
        return "<missing>"
    if path.endswith("_bits") and _integer(value, maximum=0xffffffff):
        decoded = struct.unpack("<f", struct.pack("<I", value))[0]
        return f"{value} (0x{value:08x}, float32={decoded!r})"
    return json.dumps(value, ensure_ascii=True, sort_keys=True)


def _describe(difference):
    path, left, right = difference
    return f"{path}: left={_value(left, path)}; right={_value(right, path)}"


def _boundary(record):
    if record is None:
        return "end of trace"
    if record["type"] == "snapshot":
        return f"snapshot boundary {record['state']['frame']}"
    if record["type"] == "complete":
        return f"completed boundary {record['frame']}"
    return f"command boundary {record['frame']}"


def compare_records(left, right, allow_profile_difference=False):
    """Return the earliest difference, or None. Inputs must be validated records."""
    # The override permits cross-build diagnostics, never different scenarios.
    # Unknown future fields remain strict until explicitly classified.
    ignored = _PROVENANCE_FIELDS | (_BUILD_PROFILE_FIELDS if allow_profile_difference else set())
    a, b = ({k: v for k, v in header.items() if k not in ignored} for header in (left[0], right[0]))
    difference = _first_difference(a, b)
    if difference:
        return "Profile mismatch at line 1: " + _describe(difference)
    for index in range(1, max(len(left), len(right))):
        a = left[index] if index < len(left) else None
        b = right[index] if index < len(right) else None
        if a is None or b is None:
            return f"Divergence at line {index + 1}: left {_boundary(a)}; right {_boundary(b)}"
        difference = _first_difference(a, b)
        if difference:
            return f"Divergence at line {index + 1} (left {_boundary(a)}; right {_boundary(b)}): " + _describe(difference)
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--allow-profile-difference", action="store_true", help="allow only build-profile differences; scenario headers and records remain exact, and this is not same-profile baseline evidence")
    args = parser.parse_args(argv)
    try:
        records = []
        for path in (args.left, args.right):
            with path.open(encoding="utf-8") as stream:
                records.append(read_records(stream, str(path)))
        difference = compare_records(*records, allow_profile_difference=args.allow_profile_difference)
    except (OSError, UnicodeError, TraceError) as error:
        print(f"Invalid trace: {error}", file=sys.stderr)
        return 2
    if difference:
        print(difference)
        return 1
    print("Exact record match" + (" (build-profile comparison overridden; not same-profile evidence)" if args.allow_profile_difference else " (same execution profile)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
