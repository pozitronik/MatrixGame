"""Throwaway #76: relate RND(-3.7, 11.2) differences to result magnitude and operand scale."""
import math
import struct
import sys

N = 1000000
LO, HI = -3.7, 11.2
BANDS = [(0.0, 1e-3), (1e-3, 1e-1), (1e-1, 1.0), (1.0, 4.0), (4.0, 16.0)]


def rnd_section(path):
    data = open(path, "rb").read()
    return struct.unpack(f"<{N}d", data[8 * N: 16 * N])


def ulp_distance(x, y):
    a, b = (struct.unpack("<q", struct.pack("<d", v))[0] for v in (x, y))
    a = a if a >= 0 else -(a & ((1 << 63) - 1))
    b = b if b >= 0 else -(b & ((1 << 63) - 1))
    return abs(a - b)


def main():
    ref, cur = rnd_section(sys.argv[1]), rnd_section(sys.argv[2])
    scale = math.ulp(max(abs(LO), abs(HI)))
    pairs = [(x, y) for x, y in zip(ref, cur) if x != y]
    print(f"{sys.argv[1]} vs {sys.argv[2]} RND({LO},{HI}): differing={len(pairs)} of {N}")
    if not pairs:
        return
    worst_abs = max(abs(x - y) for x, y in pairs)
    print(f"max_abs={worst_abs:.3e} = {worst_abs / scale:.2f} ulp of max(|from|,|to|) ({scale:.3e})")
    worst = max(pairs, key=lambda p: ulp_distance(*p))
    print(f"max_result_ulp={ulp_distance(*worst)} at value={worst[0]:.6e} abs_diff={abs(worst[0] - worst[1]):.3e}")
    for lo, hi in BANDS:
        band = [(x, y) for x, y in pairs if lo <= abs(x) < hi]
        if band:
            print(f"|value| in [{lo:g}, {hi:g}): differing={len(band)} max_result_ulp={max(ulp_distance(x, y) for x, y in band)}")


if __name__ == "__main__":
    main()
