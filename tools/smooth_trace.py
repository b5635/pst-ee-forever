# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarise a 'smooth trace:' block from pst_forever.log (PSTF_SmoothTrace / tests/ingame/smooth_lock.lua).

  python -I tools/smooth_trace.py [LOG] [--rows]

Per drawn frame: shown = engine pos + shift (world px), screen = shown - view. Reports
  frame interval (ms): mean / max / spread          -> pacing
  shown motion per frame (px): how often a walking creature stands still for a frame
  screen wobble (px): max |screen - median| while the view is locked -> the creature against the camera
Exit 1 when no trace is found.
"""
import statistics
import sys

DEFAULT = r"C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs\pst_forever.log"


def rows(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "smooth trace: " not in line:
                continue
            p = line.split("smooth trace: ", 1)[1].split()
            if len(p) != 7 or p[0] in ("end",):
                continue
            try:
                t, tick, a = float(p[0]), int(p[1]), float(p[2])
                sx, sy = map(int, p[3].split(","))
                dx, dy = map(int, p[5].split(","))
                vx, vy = map(int, p[6].split(","))
            except ValueError:
                continue
            if dx == -9999:
                continue  # not drawn this frame
            out.append((t, tick, a, sx + dx, sy + dy, vx, vy))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    r = rows(args[0] if args else DEFAULT)
    if len(r) < 10:
        print("smooth_trace: no trace rows")
        return 1
    dt = [b[0] - a[0] for a, b in zip(r, r[1:])]
    print(f"frames {len(r)}, ticks {sum(x[1] for x in r)}")
    print(f"frame ms: mean {statistics.mean(dt):.2f} min {min(dt):.2f} max {max(dt):.2f} "
          f"stdev {statistics.pstdev(dt):.2f}")
    mv = [abs(b[3] - a[3]) + abs(b[4] - a[4]) for a, b in zip(r, r[1:])]
    moving = [m for m in mv]
    # frames inside the walk (first to last frame with motion)
    idx = [i for i, m in enumerate(mv) if m]
    if idx:
        inner = mv[idx[0]:idx[-1] + 1]
        still = sum(1 for m in inner if m == 0)
        print(f"shown motion per frame (px, walk part): mean {statistics.mean(inner):.2f} max {max(inner)} "
              f"standing-still frames {still}/{len(inner)}")
    sc = [(x[3] - x[5], x[4] - x[6]) for x in r]
    mx = statistics.median(s[0] for s in sc)
    my = statistics.median(s[1] for s in sc)
    wob = max(max(abs(s[0] - mx), abs(s[1] - my)) for s in sc)
    print(f"screen position: median {mx:.0f},{my:.0f}, max wobble {wob:.0f} px")
    if "--rows" in sys.argv:
        for x, m in zip(r, [0] + mv):
            print(f"{x[0]:.2f} tick {x[1]} a {x[2]:.2f} shown {x[3]},{x[4]} view {x[5]},{x[6]} "
                  f"screen {x[3]-x[5]},{x[4]-x[6]} moved {m}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
