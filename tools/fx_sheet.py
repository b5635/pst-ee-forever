# SPDX-License-Identifier: GPL-2.0-or-later
"""Contact sheets of the spell-effect shots of tests/ingame/lighting_spellfx.lua.

  python -I tools/fx_sheet.py [--logs DIR] [--out DIR] [--crop L,T,R,B] [--only RES,..] [--ticks 30,60,..]

One PNG per spell (fx_sheet_<RES>.png in --out, default the logs folder): a row per tick, a column per
day/mode ("night lit", "night off", ...). --crop cuts every shot to that box (shot px, before scaling).

--check: colour check of the `alt` pairs (lit and vanilla one frame apart). Over the pixels the vanilla frame
shows bright (an effect: max channel > 150), the lit frame's mean colour must keep the vanilla hue: the
ratio of each channel to the mean of the three may differ by at most --tol (default 0.12). A night tint
undone on an untinted effect raised red and green 2.5x (Spiritual Hammer went yellow): that fails here.
Exit 1 when a pair fails.
"""
import argparse
import collections
import os
import re
import sys

from PIL import Image, ImageDraw

GAME = r'C:\GOG Games\Planescape Torment - Enhanced Edition'
NAME = re.compile(r'^fx_([A-Z0-9]+)_(night|day)_([a-z0-9]+)_(\d+)\.bmp$', re.I)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--logs', default=os.path.join(GAME, 'pst_forever', 'logs'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--crop', default=None)
    ap.add_argument('--only', default=None)
    ap.add_argument('--ticks', default=None)
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--tol', type=float, default=0.12)
    a = ap.parse_args()
    out = a.out or a.logs
    only = set(a.only.upper().split(',')) if a.only else None
    ticks = set(int(t) for t in a.ticks.split(',')) if a.ticks else None
    crop = tuple(int(v) for v in a.crop.split(',')) if a.crop else None
    shots = collections.defaultdict(dict)  # res -> (tick, col) -> path
    cols = collections.defaultdict(list)
    for n in sorted(os.listdir(a.logs)):
        m = NAME.match(n)
        if not m:
            continue
        res, day, mode, t = m.group(1).upper(), m.group(2), m.group(3), int(m.group(4))
        if (only and res not in only) or (ticks and t not in ticks):
            continue
        col = day + ' ' + mode
        if col not in cols[res]:
            cols[res].append(col)
        shots[res][(t, col)] = os.path.join(a.logs, n)
    if a.check:
        return check(shots, crop, a.tol)
    for res, cells in shots.items():
        order = sorted(cols[res], key=lambda c: (c.split()[0] != 'night', c.split()[1] != 'lit', c))
        rows = sorted({t for t, _ in cells})
        first = Image.open(next(iter(cells.values())))
        if crop:
            first = first.crop(crop)
        w, h = int(first.width * a.scale), int(first.height * a.scale)
        sheet = Image.new('RGB', (len(order) * w, len(rows) * (h + 14) + 14), (40, 40, 40))
        d = ImageDraw.Draw(sheet)
        for ci, c in enumerate(order):
            d.text((ci * w + 4, 1), c, fill=(255, 255, 0))
        for ri, t in enumerate(rows):
            y = 14 + ri * (h + 14)
            for ci, c in enumerate(order):
                p = cells.get((t, c))
                if p:
                    im = Image.open(p).convert('RGB')
                    if crop:
                        im = im.crop(crop)
                    if a.scale != 1.0:
                        im = im.resize((w, h))
                    sheet.paste(im, (ci * w, y + 12))
                d.text((ci * w + 4, y), '%s t=%d' % (res, t), fill=(200, 200, 200))
        path = os.path.join(out, 'fx_sheet_%s.png' % res)
        sheet.save(path)
        print(path, sheet.size)
    return 0


def hue(px):
    import numpy as np
    m = px.mean(0)
    return m / max(m.mean(), 1e-3), m


def check(shots, crop, tol):
    import numpy as np
    bad = 0
    for res, cells in sorted(shots.items()):
        worst, worst_t = 0.0, None
        n = 0
        for (t, col), p in sorted(cells.items()):
            day, mode = col.split()
            if mode != 'lit' or (t, day + ' off') not in cells:
                continue
            lit = np.asarray(Image.open(p).convert('RGB'), dtype=float)
            off = np.asarray(Image.open(cells[(t, day + ' off')]).convert('RGB'), dtype=float)
            if crop:
                lit = lit[crop[1]:crop[3], crop[0]:crop[2]]
                off = off[crop[1]:crop[3], crop[0]:crop[2]]
            sel = off.max(2) > 150
            if sel.sum() < 200:
                continue
            hl, ml = hue(lit[sel])
            ho, mo = hue(off[sel])
            d = float(np.abs(hl - ho).max())
            n += 1
            if d > worst:
                worst, worst_t = d, (t, day, ml.round(0), mo.round(0))
        if n == 0:
            print('%-8s no bright effect pairs' % res)
            continue
        ok = worst <= tol
        bad += 0 if ok else 1
        t = worst_t
        print('%-8s %s worst hue shift %.3f over %d pairs%s' % (res, 'ok  ' if ok else 'FAIL', worst, n,
              '' if t is None else ' (t=%d %s lit %s vanilla %s)' % (t[0], t[1], t[2], t[3])))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
