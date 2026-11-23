# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/sprite_depth: BAM decode, the SPD1 file, the dome. No GPU needed; the decode test needs the game."""
import os, struct, sys, zlib

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(os.path.join(ROOT, "tools", "sprite_depth"))
import sprite_depth as sd  # noqa: E402

needs_game = pytest.mark.skipif(not os.path.exists(os.path.join(sd.GAME, "chitin.key")), reason="game not installed")


@needs_game
def test_decode_known_bam():
  # Abishai walk: 144 frame entries, 9 cycles (directions) of 16, first frame 70x92 centre (27, 79)
  bam, raw = sd.load_bam("CWLKABSB")
  assert len(raw) == 485285 and raw[:8] == b"BAM V1  "
  assert len(bam.frames) == 144
  assert bam.cycles == [(16, i * 16) for i in range(9)]
  f = bam.frames[0]
  assert (f.w, f.h, f.cx, f.cy) == (70, 92, 27, 79)
  assert f.idx.shape == (92, 70)
  assert int((f.idx == 0).sum()) == 3697
  assert bam.lookup[:3] == [112, 113, 114]
  assert tuple(bam.palette[0]) == (0, 255, 0)  # the transparent green


@needs_game
def test_character_list():
  names = sd.read_list()
  assert len(names) == 1154 and len(set(names)) == len(names)
  for n in ("CWLKABSB", "DWLKNOAB", "DAT1BAUB", "DVHSTDB", "IGHEAD", "POSMAIN"):
    assert n in names
  for n in ("POSSHAD", "GEARS1", "CWLKNOFB"):
    assert n not in names


def frame(idx, cx=0, cy=0):
  idx = np.asarray(idx, np.uint8)
  return sd.Frame(idx.shape[1], idx.shape[0], cx, cy, idx)


def disc(n=41, rad=15, inner=9):
  yy, xx = np.mgrid[:n, :n] - n // 2
  rr = np.hypot(xx, yy)
  return np.where(rr <= rad, inner, 0).astype(np.uint8), rr


def test_synthetic_disc_is_a_dome():
  idx, rr = disc()
  bam = sd.Bam([frame(idx)], [(1, 0)], [0], np.zeros((256, 3), np.uint8))
  h = sd.bam_heights(bam, None).heights[0]
  c = idx.shape[0] // 2
  inside = idx > 1
  assert h[c, c] == h.max() and h[c, c] > 10  # centre highest (r = 14: the disc's own radius minus the edge ring)
  edge = inside & (rr > 14.5)
  assert edge.any() and np.all(h[edge] == 0)  # outermost ring 0
  assert np.all(h[~inside] == 0)
  # monotone toward the centre along a ray
  ray = h[c, c:c + 16]
  assert np.all(np.diff(ray) <= 1e-5)


def test_dome_formula():
  d = np.array([0, 2.5, 5, 10, 20], np.float32)
  np.testing.assert_allclose(sd.dome(d, 10), [0, 10 * np.sqrt(1 - 0.75 ** 2), 10 * np.sqrt(0.75), 10, 10], rtol=1e-6)


def test_transparent_and_shadow_pixels_are_zero():
  idx, _ = disc()
  idx = idx.copy()
  idx[18:23, 18:23] = 1      # shadow spot inside the body
  idx[35:39, 5:30] = 1       # ground shadow outside it
  f = frame(idx)
  bam = sd.Bam([f], [(1, 0)], [0], np.zeros((256, 3), np.uint8))
  h = sd.bam_heights(bam, None).heights[0]
  assert np.all(h[idx == 0] == 0) and np.all(h[idx == 1] == 0)
  # the inner shadow spot does not dent the dome around it: its neighbours stay near the top
  assert h[17, 20] > 0.9 * h.max()
  # and through the file
  blob = sd.encode_dep([f], [h], bam.cycles, bam.lookup, 1, 2)
  back = sd.decode_dep(blob).heights[0]
  assert np.all(back[idx <= 1] == 0)


def test_fit_to_dome_recovers_scale():
  idx, _ = disc()
  m = sd.surface_mask(idx)
  dm = sd.dome(sd.edge_distance(m), 14.0)
  p = dm * 0.01 + 3.0  # model disparity with another scale and offset
  np.testing.assert_allclose(sd.fit_to_dome(p, dm, m)[m], dm[m], atol=1e-3)


@pytest.mark.parametrize("u16", [True, False])
def test_file_round_trip(u16):
  rng = np.random.default_rng(1)
  frames = [frame(rng.integers(0, 6, (7, 5)), 3, -2), frame(np.zeros((4, 9))), frame(np.zeros((0, 0)))]
  hs = [np.where(frames[0].idx > 1, rng.random((7, 5)) * 20, 0).astype(np.float32),
        np.zeros((4, 9), np.float32), np.zeros((0, 0), np.float32)]
  cycles, lookup = [(2, 0), (1, 2)], [0, 1, 1]
  blob = sd.encode_dep(frames, hs, cycles, lookup, 0xDEADBEEF, 0x1234, u16)
  assert blob[:4] == b"SPD1" and struct.unpack_from("<H", blob, 4)[0] == 1
  assert sd.HDR.size == 32 and sd.FRM.size == 20
  d = sd.decode_dep(blob)
  assert (d.u16, d.bam_crc, d.params, d.cycles, d.lookup) == (u16, 0xDEADBEEF, 0x1234, cycles, lookup)
  assert [fr[:4] for fr in d.frames] == [(5, 7, 3, -2), (9, 4, 0, 0), (0, 0, 0, 0)]
  step = hs[0].max() / (65535 if u16 else 255)
  np.testing.assert_allclose(d.heights[0], hs[0], atol=step / 2 + 1e-6)
  assert np.all(d.heights[0][hs[0] == 0] == 0)
  assert d.heights[1].shape == (4, 9) and not d.heights[1].any()


def test_flicker_counts_only_static_pixels():
  a = frame(np.full((4, 4), 5))
  b = frame(np.full((4, 4), 5))
  b.idx[0, 0] = 6  # one pixel changed colour: excluded from the static metric
  ha = np.ones((4, 4), np.float32)
  hb = np.ones((4, 4), np.float32) * 2
  hb[0, 0] = 100
  s, n = sd.flicker([ha, hb], [a.idx > 1, b.idx > 1], [a, b], [0, 1], static=True)
  assert n == 1 and s == pytest.approx(1.0)
  s, n = sd.flicker([ha, hb], [a.idx > 1, b.idx > 1], [a, b], [0, 1], static=False)
  assert s == pytest.approx((15 * 1 + 99) / 16)
