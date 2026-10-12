"""Throwaway #76: count differing values and max ulp distance between dumps."""
import struct
import sys

N = 1000000


def sections(path):
    data = open(path, "rb").read()
    a = struct.unpack(f"<{N}q", data[: 8 * N])
    b = struct.unpack(f"<{N}q", data[8 * N: 16 * N])
    c = struct.unpack(f"<{N}i", data[16 * N: 16 * N + 4 * N])
    return {"RndFloat()": a, "RND(-3.7,11.2)": b, "FSRND(M_PI)": c}


def ordered(i, bits):
    # Map IEEE bit patterns to a monotonic integer line for ulp distance.
    sign = 1 << (bits - 1)
    return i if i >= 0 else -(i & (sign - 1))


def main():
    ref = sections(sys.argv[1])
    for other in sys.argv[2:]:
        cur = sections(other)
        for name in ref:
            bits = 32 if name.startswith("FSRND") else 64
            diffs = [abs(ordered(x, bits) - ordered(y, bits)) for x, y in zip(ref[name], cur[name]) if x != y]
            first = next((k for k, (x, y) in enumerate(zip(ref[name], cur[name])) if x != y), None)
            print(f"{sys.argv[1]} vs {other} {name}: differing={len(diffs)} max_ulp={max(diffs, default=0)} first_index={first}")


if __name__ == "__main__":
    main()
