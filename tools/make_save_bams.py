# SPDX-License-Identifier: GPL-2.0-or-later
"""Saving-throw banner icons ([tbc] save_banner; deployed into the game's override folder).

    python -I tools/make_save_bams.py [--preview OUT.png] [--size N]

  PFSVOK.BAM   save made:   48x48, frames 0..31 at 30 fps, the last one is held
  PFSVBAD.BAM  save failed: the same, red

Timeline (measured frame by frame from the reference clip, 30 fps): the hexagon frame is stroked in clockwise
from its top-right corner while a wireframe d20 tumbles inside (0-0.22 s, grey -> bone white), the die slows and
settles face-on (central triangle up) by 0.45 s, holds; at 0.70 s the frame turns to the verdict colour and the
inside darkens; 0.72-0.88 s an open hand is wiped in from the top while the die fades to a faint ghost behind it.
Colours follow the PST:EE UI: bone ivory lines, dark umber fill, verdigris-cyan for a save made, blood red for a
failure, a thin dark rim round everything so it reads over any floor.
Every frame is drawn 4x and box-filtered down; palette alpha (BAMC, write_bam from make_dice_bams.py).
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_dice_bams import OUT, write_bam  # noqa: E402

FPS = 30
FRAMES = 32
SS = 4
BONE = np.array((232, 222, 200), np.float64)
GREY = np.array((150, 144, 134), np.float64)
RIM = np.array((20, 13, 9), np.float64)
FILL = np.array((22, 15, 11), np.float64)
VERDICT = {
  True: (np.array((56, 170, 200), np.float64), np.array((150, 222, 238), np.float64)),   # core, highlight
  False: (np.array((178, 34, 54), np.float64), np.array((226, 84, 100), np.float64)),
}
HAND = np.array((222, 212, 194), np.float64)
HAND_DARK = np.array((128, 112, 96), np.float64)
T_RING, T_SETTLE, T_VERDICT, T_HAND0, T_HAND1 = 0.22, 0.45, 0.70, 0.72, 0.88


def clamp(v, a=0.0, b=1.0): return max(a, min(b, v))
def smooth(v): v = clamp(v); return v * v * (3 - 2 * v)


# --- geometry ------------------------------------------------------------------------------------------
def hexagon(cx, cy, r):
  """Pointy-top hexagon corners, clockwise from the top."""
  return [(cx + r * math.sin(math.radians(60 * k)), cy - r * math.cos(math.radians(60 * k))) for k in range(6)]


def ico():
  p = (1 + 5 ** 0.5) / 2
  v = []
  for a in (-1, 1):
    for b in (-p, p):
      v += [(0, a, b), (a, b, 0), (b, 0, a)]
  v = np.array(v, np.float64)
  e = [(i, j) for i in range(12) for j in range(i + 1, 12) if abs(np.linalg.norm(v[i] - v[j]) - 2) < 1e-6]
  f = [(i, j, k) for i, j in e for k in range(j + 1, 12) if (i, k) in e and (j, k) in e]
  return v / np.linalg.norm(v[0]), e, f


def rot(axis, ang):
  axis = np.asarray(axis, np.float64) / np.linalg.norm(axis)
  x, y, z = axis
  c, s = math.cos(ang), math.sin(ang)
  return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                   [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                   [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])


V, E, F = ico()


def front_pose():
  """Face-on (a face's normal to the viewer, +z), that face's apex straight up: the classic d20 glyph."""
  i, j, k = F[0]
  n = V[i] + V[j] + V[k]
  n /= np.linalg.norm(n)
  z = np.array((0, 0, 1.0))
  ax = np.cross(n, z)
  R = rot(ax, math.acos(clamp(float(n @ z), -1, 1))) if np.linalg.norm(ax) > 1e-9 else np.eye(3)
  P = V @ R.T
  # screen y grows downwards: the apex is the face vertex with the smallest y after the spin about z
  top = min((i, j, k), key=lambda q: P[q][1])
  ang = math.atan2(P[top][0], -P[top][1])
  return rot((0, 0, 1), -ang) @ R


R_FRONT = front_pose()


def pose(t):
  s = clamp(t / T_SETTLE)
  spin = 2.3 * math.pi * (1 - s) ** 3
  return R_FRONT @ rot((0.45, 1.0, 0.25), spin)


# --- drawing (4x, coverage masks) ----------------------------------------------------------------------
class Canvas:
  def __init__(self, n):
    self.n = n
    self.rgb = np.zeros((n, n, 3))
    self.a = np.zeros((n, n))

  def over(self, mask, color, alpha=1.0):
    """mask: float array 0..1 (already at 1x), color: rgb or an (n,n,3) array."""
    m = np.clip(mask * alpha, 0, 1)
    col = np.broadcast_to(np.asarray(color, np.float64), (self.n, self.n, 3))
    out_a = m + self.a * (1 - m)
    num = col * m[..., None] + self.rgb * (self.a * (1 - m))[..., None]
    self.rgb = np.where(out_a[..., None] > 1e-6, num / np.maximum(out_a, 1e-6)[..., None], 0)
    self.a = out_a

  def rgba(self):
    return np.dstack([np.clip(self.rgb, 0, 255), np.clip(self.a * 255, 0, 255)]).round().astype(np.uint8)


def mask(n, draw_fn):
  """Draws at SS x with draw_fn(ImageDraw, scale) and returns the box-filtered coverage at 1x."""
  img = Image.new("L", (n * SS, n * SS), 0)
  draw_fn(ImageDraw.Draw(img), SS)
  return np.array(img.resize((n, n), Image.BOX), np.float64) / 255


def grow(m, px):
  img = Image.fromarray((m * 255).astype(np.uint8), "L")
  for _ in range(px): img = img.filter(ImageFilter.MaxFilter(3))
  return np.array(img, np.float64) / 255


def ring_path(pts, prog):
  """The closed hexagon from its top-right corner clockwise, cut at prog (0..1)."""
  start = 1
  seq = [pts[(start + k) % 6] for k in range(7)]
  total = 6 * prog
  out = [seq[0]]
  for k in range(6):
    if total <= 0: break
    a, b = seq[k], seq[k + 1]
    f = min(1.0, total)
    out.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f))
    total -= 1
  return out


def hand_mask(n, s):
  """An open right palm, fingers up, thumb out to the left (48 px design units)."""
  k = n / 48

  def capsule(d, a, b, w, sc):
    (x0, y0), (x1, y1) = a, b
    d.line([(x0 * k * sc, y0 * k * sc), (x1 * k * sc, y1 * k * sc)], fill=255, width=int(w * k * sc))
    for x, y in (a, b):
      r = w * k * sc / 2
      d.ellipse([x * k * sc - r, y * k * sc - r, x * k * sc + r, y * k * sc + r], fill=255)

  def draw(d, sc):
    d.ellipse([17.6 * k * sc, 20.5 * k * sc, 31.0 * k * sc, 35.5 * k * sc], fill=255)       # palm
    d.rectangle([18.2 * k * sc, 22.0 * k * sc, 30.4 * k * sc, 29.0 * k * sc], fill=255)
    capsule(d, (19.8, 23.5), (18.6, 14.4), 3.5, sc)   # index
    capsule(d, (23.3, 22.5), (23.2, 12.2), 3.7, sc)   # middle
    capsule(d, (26.7, 23.0), (27.6, 13.4), 3.5, sc)   # ring
    capsule(d, (29.4, 25.5), (31.4, 18.0), 3.1, sc)   # little
    capsule(d, (19.5, 31.0), (14.6, 24.2), 3.8, sc)   # thumb
  return mask(n, draw)


def crease_mask(n):
  k = n / 48

  def draw(d, sc):
    w = max(1, int(0.9 * k * sc))
    d.arc([19.5 * k * sc, 22.5 * k * sc, 31 * k * sc, 34 * k * sc], 200, 300, fill=255, width=w)  # head line
    d.arc([17.0 * k * sc, 25.5 * k * sc, 27.5 * k * sc, 37 * k * sc], 250, 345, fill=255, width=w)  # life line
    for x in (19.3, 23.25, 27.0):  # knuckle ticks
      d.line([((x - 0.9) * k * sc, 19.2 * k * sc), ((x + 0.9) * k * sc, 19.2 * k * sc)], fill=140, width=w)
  return mask(n, draw)


def render(t, good, n=48):
  c = Canvas(n)
  cx = cy = n / 2
  r_out = n * 0.44
  ring_w = n * 0.075
  pts = hexagon(cx, cy, r_out - ring_w / 2)
  verdict = t >= T_VERDICT
  vblend = smooth((t - T_VERDICT) / 0.07)
  core, hi = VERDICT[good]

  # dark interior: comes with the verdict
  if verdict:
    inner = mask(n, lambda d, s: d.polygon([(x * s, y * s) for x, y in pts], fill=255))
    yy, xx = np.mgrid[0:n, 0:n]
    rr = np.hypot(xx - cx + 0.5, yy - cy + 0.5) / r_out
    shade = FILL[None, None, :] * (1.25 - 0.45 * rr[..., None])
    c.over(inner, shade, 0.9 * vblend)

  # the tumbling die
  die_a = 0.3 + 0.7 * smooth((t - 0.04) / 0.25)
  if t >= T_HAND0: die_a *= 1 - 0.82 * smooth((t - T_HAND0) / (T_HAND1 - T_HAND0))
  col = GREY + (BONE - GREY) * smooth(t / 0.32)
  R = pose(t)
  P = V @ R.T
  scale = n * 0.255
  sp = [(cx + p[0] * scale, cy + p[1] * scale, p[2]) for p in P]
  front, back = [], []
  for i, j in E:
    z = (sp[i][2] + sp[j][2]) / 2
    (front if z > -0.15 else back).append((sp[i][:2], sp[j][:2]))
  lw = max(1, round(n * 0.03 * SS))
  m_back = mask(n, lambda d, s: [d.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], fill=255, width=lw) for a, b in back])
  m_front = mask(n, lambda d, s: [d.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], fill=255, width=lw) for a, b in front])
  m_die = np.maximum(m_back, m_front)
  c.over(grow(m_die, 1), RIM, 0.55 * die_a * (0 if verdict else 1))  # rim only over the open floor
  # back edges show while it tumbles, then sink away: the settled glyph is the face-on d20 (video)
  c.over(m_back, col * 0.8, (0.45 - 0.33 * smooth((t - 0.3) / 0.15)) * die_a)
  c.over(m_front, col, die_a)

  # the hand, wiped in from the top
  if t >= T_HAND0:
    hm = hand_mask(n, SS)
    top, bot = n * 0.24, n * 0.76
    edge = top + (bot - top + n * 0.1) * smooth((t - T_HAND0) / (T_HAND1 - T_HAND0))
    yy = np.arange(n)[:, None] + 0.5
    wipe = np.clip((edge - yy) / (n * 0.08), 0, 1) * np.ones((1, n))
    yy2, xx2 = np.mgrid[0:n, 0:n]
    g = np.clip(1.08 - 0.28 * ((yy2 - n * 0.25) / (n * 0.5)) - 0.1 * ((xx2 - n * 0.3) / (n * 0.5)), 0.7, 1.08)
    shade = HAND[None, None, :] * g[..., None]
    c.over(grow(hm, 1) * wipe, RIM, 0.85)
    c.over(hm * wipe, shade)
    c.over(crease_mask(n) * hm * wipe, HAND_DARK, 0.75)

  # the hexagon frame: stroked in, then the verdict colour
  prog = smooth(t / T_RING)
  ring_a = 0.35 + 0.65 * smooth(t / 0.25)
  path = ring_path(pts, prog)
  rw = max(1, round(ring_w * SS))

  def stroke(width):
    def draw(d, s):
      q = [(x * s, y * s) for x, y in path]
      if len(q) >= 2: d.line(q, fill=255, width=width, joint="curve")
    return draw
  m_ring = mask(n, stroke(rw))
  m_rim = mask(n, stroke(rw + 2 * SS))
  m_hi = mask(n, stroke(max(1, rw // 3)))
  ring_col = BONE * (1 - vblend) + core * vblend
  hi_col = BONE * (1 - vblend) + hi * vblend
  c.over(m_rim, RIM, 0.8 * ring_a)
  c.over(m_ring, ring_col, ring_a)
  c.over(m_hi, hi_col, ring_a * (0.25 + 0.55 * vblend))
  return c.rgba()


def frames(good, n):
  return [render(i / FPS if i < FRAMES - 1 else 1.2, good, n) for i in range(FRAMES)]


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("--preview", help="PNG strip of both animations (4x zoom, on a floor-like grey)")
  ap.add_argument("--size", type=int, default=48)
  a = ap.parse_args()
  out = {}
  for good, name in ((True, "PFSVOK"), (False, "PFSVBAD")):
    fr = frames(good, a.size)
    out[good] = fr
    write_bam(OUT / f"{name}.BAM", fr)
    print(f"{name}.BAM: {len(fr)} frames {a.size}x{a.size}")
  if a.preview:
    n, z = a.size, 3
    W = FRAMES * n * z
    img = Image.new("RGB", (W, 2 * n * z), (58, 48, 40))
    for row, good in enumerate((True, False)):
      for i, f in enumerate(out[good]):
        im = Image.fromarray(f, "RGBA").resize((n * z, n * z), Image.NEAREST)
        img.paste(im, (i * n * z, row * n * z), im)
    img.save(a.preview)
    print("preview", a.preview)


if __name__ == "__main__":
  main()
