# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/vfxscan.py: per-frame colour, strength and bright centroid on synthetic frames."""
import os
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(os.path.join(ROOT, "tools"))
import vfxscan  # noqa: E402
sd = vfxscan.sd


def pal():
  p = np.zeros((256, 3), np.uint8)
  p[1] = (255, 0, 0)      # pure red
  p[2] = (255, 128, 0)    # orange
  p[3] = (40, 40, 40)     # dim grey
  return p


def frame(idx):
  idx = np.asarray(idx, np.uint8)
  return sd.Frame(idx.shape[1], idx.shape[0], 0, 0, idx)


def test_red_square_colour_strength_centroid():
  idx = np.zeros((10, 10), np.uint8)
  idx[2:4, 6:8] = 1  # 2x2 red block, rows 2-3, cols 6-7
  r, g, b, st, cx, cy, n = vfxscan.frame_row(frame(idx), pal())
  assert (r, g, b) == (1.0, 0.0, 0.0)
  assert abs(st - 4 * 0.299 / 100) < 1e-6  # mean luminance over the whole 10x10 rect
  assert (cx, cy, n) == (7.0, 3.0, 4)       # pixel centres 6.5..7.5, 2.5..3.5


def test_dim_pixels_do_not_move_the_centroid_and_transparent_is_ignored():
  idx = np.zeros((8, 8), np.uint8)
  idx[0, 0] = 2       # bright orange at (0, 0)
  idx[7, 7] = 3       # dim grey, below half the peak
  r, g, b, st, cx, cy, n = vfxscan.frame_row(frame(idx), pal())
  assert (cx, cy, n) == (0.5, 0.5, 1)
  assert r == 1.0 and 0.4 < g < 0.6 and b < 0.1  # orange dominates the colour
  empty = vfxscan.frame_row(frame(np.zeros((4, 6), np.uint8)), pal())
  assert empty == (1.0, 1.0, 1.0, 0.0, 3.0, 2.0, 0)


def test_rows_are_per_cycle_and_frame_index():
  idx = np.zeros((2, 2), np.uint8)
  idx[0, 0] = 1
  bam = sd.Bam([frame(idx), frame(np.zeros((2, 2), np.uint8))], [(2, 0), (1, 2)], [1, 0, 0], pal())
  rows = vfxscan.bam_rows("TEST", bam)
  assert [r.split("\t")[:3] for r in rows] == [["TEST", "0", "0"], ["TEST", "0", "1"], ["TEST", "1", "0"]]
  assert rows[0].split("\t")[6] == "0.0000" and float(rows[1].split("\t")[6]) > 0  # cycle 0 frame 0 = frame 1 (empty)


def test_hud_and_area_decor_are_not_effects():
  for n in ("WMPICKL", "CURSARW", "GUIBTBUT", "FILLBAR", "RADWEDGE", "CIRCBUTT", "TACTBUTT", "PAUSBUTT", "A0405FAC"):
    assert vfxscan.NOT_EFFECT.match(n), n
  for n in ("A003INTR", "S031AXST", "SPMAGMIS", "SE10FIZZ", "GRNDGLOW"):
    assert not vfxscan.NOT_EFFECT.match(n), n


def test_generated_emit_list_has_the_spell_visuals():
  if not os.path.exists(vfxscan.EMIT_OUT):
    import pytest
    pytest.skip("run tools/vfxscan.py run first")
  names = {l.strip() for l in open(vfxscan.EMIT_OUT) if l.strip() and not l.startswith("#")}
  # named in Torment.exe: CGameSprite::ApplyCastingEffect, CProjectile::DecodeProjectile; data: SPWI310 op369
  assert {"A003INTR", "S031AXST", "S031EXPL", "SE10FIZZ"} <= names
  assert not any(n.startswith("WM") for n in names)
  chars = set(vfxscan.sd.read_list())
  assert not (names & chars)  # never a creature animation
