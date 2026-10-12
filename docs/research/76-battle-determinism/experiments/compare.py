"""Compare probe traces at common simulated times (throwaway #76 helper)."""
import re
import sys

PAT = re.compile(r"^frame t=(\d+) step=(\d+) draw=(-?\d+) puffs=(\d+) maxz=(\S+) hash=(\w+)")


def load(path):
    rows = {}
    for line in open(path, encoding="ascii"):
        m = PAT.match(line)
        if m:
            t, step, draw, puffs, maxz, h = m.groups()
            rows[int(t)] = (int(draw), int(puffs), float(maxz), h)
    return rows


def main():
    ref_path, *others = sys.argv[1:]
    ref = load(ref_path)
    for path in others:
        cur = load(path)
        common = sorted(set(ref) & set(cur))
        first_draw = next((t for t in common if ref[t][0] != cur[t][0]), None)
        first_puffs = next((t for t in common if ref[t][1] != cur[t][1]), None)
        first_state = next((t for t in common if ref[t] != cur[t]), None)
        maxdz = max((abs(ref[t][2] - cur[t][2]) for t in common if ref[t][1] == cur[t][1] and ref[t][1] > 0), default=0.0)
        last = common[-1]
        print(f"{path}: common={len(common)} first_state_diff_t={first_state} first_draw_diff_t={first_draw} "
              f"first_puffcount_diff_t={first_puffs} max_abs_dmaxz_same_count={maxdz:.6g} "
              f"final(t={last}) ref_draw={ref[last][0]} draw={cur[last][0]}")


if __name__ == "__main__":
    main()
