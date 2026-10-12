"""Throwaway #76 source model of CMatrixMapLogic::Takt scheduling arithmetic.

Mirrors MatrixGame/src/MatrixLogic.cpp:2927-3011 at 540251d (m_Time, GatherInfo cadence,
ProceedLogic portions and side catch-up) and the 100 ms cap in MatrixLib/3G/3g.cpp:383.
It is a model of the control flow only; no engine code runs here.
"""
import collections
import sys

PERIOD = 10


def frames(name, total):
    if name == "jitter":
        x, t, out = 2463534242, 0, []
        while t < total:
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            d = min(1 + x % 100, total - t)
            out.append(d)
            t += d
        return out
    if name.startswith("stall"):
        # 16 ms frames with one 250 ms stall at t~500 ms (capped to 100 ms per frame).
        out, t = [], 0
        while t < total:
            d = 250 if 496 <= t < 512 else 16
            d = min(d, 100, total - t)
            out.append(d)
            t += d
        return out
    d = int(name)
    return [min(d, total - t) for t in range(0, total, d)]


def model(steps):
    time = 0
    takt_next = 0
    gather_last = 0
    proceed = collections.Counter()
    side_ticks = 0
    gathers = []
    max_side_burst = 0
    for step in steps:
        time += step
        if time - gather_last > 100:
            gather_last = time
            gathers.append(time)
        for _ in range(step // PERIOD):
            proceed[PERIOD] += 1
        rem = step - (step // PERIOD) * PERIOD
        if rem:
            proceed[rem] += 1
        burst = 0
        while time > takt_next:
            takt_next += PERIOD
            side_ticks += 1
            burst += 1
        max_side_burst = max(max_side_burst, burst)
    return time, proceed, side_ticks, gathers, max_side_burst


def main():
    total = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    print("schedule | frames | game ms | ProceedLogic calls by ms | side LogicTakt ticks | max side ticks per frame | GatherInfo count | GatherInfo spacing")
    for name in ["10", "16", "17", "33", "50", "100", "jitter", "stall"]:
        steps = frames(name, total)
        time, proceed, ticks, gathers, burst = model(steps)
        spacing = sorted({b - a for a, b in zip(gathers, gathers[1:])})
        calls = ", ".join(f"{k}:{v}" for k, v in sorted(proceed.items()))
        print(f"{name} | {len(steps)} | {time} | {calls} | {ticks} | {burst} | {len(gathers)} | {spacing[:6]}")


if __name__ == "__main__":
    main()
