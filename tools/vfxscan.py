# SPDX-License-Identifier: GPL-2.0-or-later
"""Per-frame light table for glowing BAMs (lighting phase 4, docs/VISUALS_PLAN.md 5.6, GemRB VFX_PLAN 7.2).

  python -I tools/vfxscan.py run [--only REGEX]   write <game>/pst_forever/assets/vfx/vfxauto.tsv
  python -I tools/vfxscan.py show BAM             print the rows of one BAM

Every BAM V1 that is not a character animation (tools/sprite_depth/character_bams.txt) and not GUI-only:
per cycle, per frame (frame index inside the cycle, as CVidCell keeps it at +0x118 / sequence +0x11A):
  colour   mean RGB of the opaque pixels weighted by luminance x (0.2 + saturation), scaled so the
           brightest channel is 1: the coloured halo sets the hue, not the white-hot core
  strength mean luminance over the whole frame rect (GemRB GetSpriteGlow)
  cx, cy   centroid of the pixels brighter than half the frame's brightest, weighted by luminance^2
  bright   number of those pixels
Palette index 0 is transparent (additive draws: black adds nothing anyway).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(HERE, "sprite_depth"))
sys.path.append(HERE)
import sprite_depth as sd  # noqa: E402  (BAM V1 decoder, resource reader)

OUT = os.path.join(sd.GAME, "pst_forever", "assets", "vfx", "vfxauto.tsv")
LUMA = np.array([0.299, 0.587, 0.114], np.float32)


def frame_row(fr: sd.Frame, pal: np.ndarray) -> tuple[float, float, float, float, float, float, int]:
  idx = fr.idx
  if fr.w == 0 or fr.h == 0:
    return (1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0)
  rgb = pal[idx].astype(np.float32) / 255.0  # [h, w, 3]
  lum = rgb @ LUMA
  lum[idx == 0] = 0.0
  total = float(lum.sum())
  strength = total / float(fr.w * fr.h)
  if total <= 1e-6:
    return (1.0, 1.0, 1.0, 0.0, fr.w / 2.0, fr.h / 2.0, 0)
  mx, mn = rgb.max(axis=2), rgb.min(axis=2)
  sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0.0)
  wc = lum * (0.2 + sat)
  col = (rgb * wc[..., None]).reshape(-1, 3).sum(0) / max(float(wc.sum()), 1e-6)
  col = col / max(float(col.max()), 1e-6)
  peak = float(lum.max())
  m = lum >= 0.5 * peak
  w2 = np.where(m, lum * lum, 0.0)
  ys, xs = np.mgrid[0:fr.h, 0:fr.w]
  s = float(w2.sum())
  cx = float((w2 * (xs + 0.5)).sum() / s)
  cy = float((w2 * (ys + 0.5)).sum() / s)
  return (float(col[0]), float(col[1]), float(col[2]), strength, cx, cy, int(m.sum()))


def bam_rows(name: str, bam: sd.Bam) -> list[str]:
  out = []
  for c in range(len(bam.cycles)):
    for f, fi in enumerate(bam.cycle_frames(c)):
      if fi >= len(bam.frames):
        continue
      r, g, b, st, cx, cy, n = frame_row(bam.frames[fi], bam.palette)
      out.append(f"{name}\t{c}\t{f}\t{r:.3f}\t{g:.3f}\t{b:.3f}\t{st:.4f}\t{cx:.1f}\t{cy:.1f}\t{n}")
  return out


def candidates(only: str | None) -> list[str]:
  chars = set(sd.read_list()) if os.path.exists(sd.LIST_FILE) else set()
  names = sorted(n for n in sd.all_bam_names() if n not in chars)
  if only:
    rx = re.compile(only, re.I)
    names = [n for n in names if rx.search(n)]
  return names


EMIT_OUT = os.path.join(sd.GAME, "pst_forever", "assets", "vfx", "vfxemit.txt")
EMIT_DATA = ("SPL", "ITM", "EFF", "PRO", "VVC", "BCS")
NOT_EFFECT = re.compile(r"^(WM|CUR|GU|FILL|RAD|CIRC|TACT|PAUS|A[0-9]{4})")  # HUD markers, GUI parts, area decor (A + area no.)
TOKEN = re.compile(rb"[A-Za-z0-9_#]{3,8}")


def emitters(candidates: list[str]) -> tuple[list[str], dict[str, int]]:
  """Effect BAMs that emit even when drawn with the normal blend: named by the exe (the hardcoded PST
  projectiles, CGameSprite::ApplyCastingEffect, ...) or by effect data (op369 and friends, PRO, VVC, scripts)."""
  cand = set(candidates)
  found: set[str] = set()
  exe = open(os.path.join(sd.GAME, "Torment.exe"), "rb").read()
  for m in re.finditer(rb"([A-Za-z0-9_#]{3,8})\x00", exe):
    n = m.group(1).decode("ascii").upper()
    if n in cand:
      found.add(n)
  n_exe = len(found)
  key = sd.ieres.load_key()
  for (name, ext) in key:
    if ext not in EMIT_DATA:
      continue
    d = sd.ieres.read(name, ext)
    if not d:
      continue
    for t in TOKEN.findall(d):
      n = t.decode("ascii").upper()
      if n in cand:
        found.add(n)
  out = sorted(n for n in found if not NOT_EFFECT.match(n))
  dark = [n for n in out if is_dark(n)]
  out = [n for n in out if n not in set(dark)]
  return out, {"exe": n_exe, "total": len(found), "kept": len(out), "dark": len(dark), "dark_names": dark}


DARK_LUMA = 0.18  # GemRB VFX_PLAN 7.2: smoke, shadows, darkness never emit (or soften: they would vanish)


def is_dark(name: str) -> bool:
  """Mean luminance of the opaque pixels over all frames below DARK_LUMA."""
  try:
    bam, _ = sd.load_bam(name)
  except Exception:
    return False
  tot, n = 0.0, 0
  for fr in bam.frames:
    if fr.w == 0 or fr.h == 0:
      continue
    m = fr.idx != 0
    if not m.any():
      continue
    lum = (bam.palette[fr.idx[m]].astype(np.float32) / 255.0) @ LUMA
    tot += float(lum.sum())
    n += int(m.sum())
  return n > 0 and tot / n < DARK_LUMA


def run(args) -> int:
  names = candidates(args.only)
  rows, used, skipped = [], 0, 0
  for n in names:
    try:
      bam, _ = sd.load_bam(n)
    except Exception:  # BAM V2 (GUI), missing, broken
      skipped += 1
      continue
    r = bam_rows(n, bam)
    if r:
      rows += r
      used += 1
  os.makedirs(os.path.dirname(OUT), exist_ok=True)
  with open(OUT, "w", encoding="ascii", newline="\n") as f:
    f.write("# SPDX-License-Identifier: GPL-2.0-or-later\n"
            "# Written by tools/vfxscan.py. BAM cycle frame r g b strength cx cy bright\n")
    f.write("\n".join(rows) + "\n")
  print(f"vfxscan: {used} BAMs, {len(rows)} frames, {skipped} skipped (V2/missing) -> {OUT}")
  if not args.only:
    names, st = emitters(candidates(None))
    with open(EMIT_OUT, "w", encoding="ascii", newline="\n") as f:
      f.write("# SPDX-License-Identifier: GPL-2.0-or-later\n"
              "# Written by tools/vfxscan.py: effect BAMs that emit with any blend (named in Torment.exe or in\n"
              f"# SPL/ITM/EFF/PRO/VVC/BCS data; no creature animations, no HUD markers). {len(names)} BAMs.\n"
              f"# Dark (mean luma < {DARK_LUMA}), left out: {' '.join(st['dark_names'])}\n")
      f.write("\n".join(names) + "\n")
    print(f"vfxscan: {st['kept']} emitting BAMs ({st['exe']} named in the exe, {st['total']} found, "
          f"{st['dark']} dark left out) -> {EMIT_OUT}")
  return 0


def show(args) -> int:
  bam, _ = sd.load_bam(args.bam.upper())
  print("\n".join(bam_rows(args.bam.upper(), bam)))
  return 0


def main(argv: list[str]) -> int:
  p = argparse.ArgumentParser()
  sub = p.add_subparsers(dest="cmd", required=True)
  r = sub.add_parser("run")
  r.add_argument("--only")
  s = sub.add_parser("show")
  s.add_argument("bam")
  a = p.parse_args(argv)
  return run(a) if a.cmd == "run" else show(a)


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
