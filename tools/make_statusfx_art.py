# SPDX-License-Identifier: GPL-2.0-or-later
r"""Status-effect visuals: our own particle art, drawn from scratch (no game or third-party files).

  python -I tools/make_statusfx_art.py [--preview OUT.png]

Writes data/statusfx_atlas.bin (deployed to pst_forever\assets\statusfx) and data/override/PFVBLANK.BAM. The cell layout must match
src/modules/statusfx/statusfx_logic.h (Cell enum).

Look: Planescape: Torment's pre-rendered spell art, not clean vector shapes. Every cell is grainy (glittering
dust, speckle in the alpha), volumes are soft with a hot core, flames and smoke are domain-warped noise that
curls, the skull is shaded as a lit relief, edges break up. Cells are rendered at 2x and box-filtered down.

Atlas file: "PFA1", u16 width, u16 height, u16 cell, u16 columns, then zlib(RGBA8 rows, top row first).
Light cells (glints, bolts, rings, smoke, wisps) are white with alpha = brightness: the shader tints them
and draws them additive. Fire and the skull carry their own colour (normal blend).
"""
import math, os, struct, sys, zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CELL, COLS, ROWS = 128, 8, 5
SS = 2  # supersampling
N = CELL * SS
# Cell indices (statusfx_logic.h Cell): row 0 GLINT0..3 DUST STREAK BURST SHARD, row 1 BOLT0..3 RING0..3,
# row 2 SMOKE0..7, row 3 FIRE0..7, row 4 SKULL0 SKULL1 GLOW WISP0..3.
GLINT, DUST, STREAK, BURST, SHARD, BOLT, RING, SMOKE, FIRE, SKULL, GLOW, WISP = 0, 4, 5, 6, 7, 8, 12, 16, 24, 32, 34, 35

yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
X, Y = (xx - (N - 1) / 2) / (N / 2), (yy - (N - 1) / 2) / (N / 2)  # -1..1, y down
R = np.sqrt(X * X + Y * Y)
A = np.arctan2(Y, X)


# --- noise ------------------------------------------------------------------------------------------
def _grid(seed, n=257):
  return np.random.default_rng(seed).random((n, n)).astype(np.float32)


_GRIDS = {}


def vnoise(x, y, seed):
  """Smooth value noise at continuous coordinates (any shape arrays)."""
  g = _GRIDS.get(seed)
  if g is None: g = _GRIDS[seed] = _grid(seed)
  x = np.mod(x, 255.0)
  y = np.mod(y, 255.0)
  xi, yi = x.astype(int), y.astype(int)
  fx, fy = x - xi, y - yi
  fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
  a = g[yi, xi] * (1 - fx) + g[yi, xi + 1] * fx
  b = g[yi + 1, xi] * (1 - fx) + g[yi + 1, xi + 1] * fx
  return a * (1 - fy) + b * fy


def fbm(x, y, seed, oct=5, lac=2.0, gain=0.5):
  out, amp, tot, f = 0.0, 1.0, 0.0, 1.0
  for o in range(oct):
    out = out + amp * vnoise(x * f, y * f, seed + 31 * o)
    tot += amp
    amp *= gain
    f *= lac
  return out / tot


def warp(x, y, seed, amt, scale=2.5):
  wx = fbm(x * scale + 5.2, y * scale + 1.3, seed + 900, 4) - 0.5
  wy = fbm(x * scale + 9.7, y * scale + 2.8, seed + 950, 4) - 0.5
  return x + amt * wx, y + amt * wy


def speckle(seed, density, size=1):
  """Glittering dust: sparse bright points of random strength (the grain of PST's rendered effects)."""
  r = np.random.default_rng(seed)
  pts = (r.random((N, N)) < density).astype(np.float32) * r.random((N, N)).astype(np.float32) ** 0.5
  if size > 1:
    k = np.zeros_like(pts)
    for ox in range(-size + 1, size):
      for oy in range(-size + 1, size):
        k = np.maximum(k, np.roll(np.roll(pts, ox, 1), oy, 0) * (1.0 - 0.35 * (abs(ox) + abs(oy))))
    pts = k
  return pts


def grain(seed, amt):
  return 1.0 - amt + amt * np.random.default_rng(seed).random((N, N)).astype(np.float32) * 2 * 0.5 * 2


def gauss(r, s):
  return np.exp(-(r * r) / (2 * s * s))


def smooth(e0, e1, x):
  t = np.clip((x - e0) / (e1 - e0), 0, 1)
  return t * t * (3 - 2 * t)


def light(a, seed=0, g=0.25):
  """A white light cell: alpha = brightness, with film grain."""
  a = np.clip(a, 0, 1) * np.clip(grain(seed + 77, g), 0, 1.4)
  a = np.clip(a, 0, 1)
  return np.dstack([np.ones_like(a), np.ones_like(a), np.ones_like(a), a])


# --- cells --------------------------------------------------------------------------------------------
def glint(k):
  """A glint of light: hot core, faint short spikes, a halo of glittering dust."""
  rot = [0.0, 0.35, 0.78, 1.2][k]
  u, v = X * math.cos(rot) + Y * math.sin(rot), -X * math.sin(rot) + Y * math.cos(rot)
  ln = [0.75, 0.55, 0.65, 0.45][k]
  spike = (np.exp(-np.abs(v) / 0.01) * np.clip(1 - np.abs(u) / ln, 0, 1) ** 2.5 +
           np.exp(-np.abs(u) / 0.01) * np.clip(1 - np.abs(v) / ln, 0, 1) ** 2.5) * 0.45
  dust = speckle(100 + k, 0.035) * gauss(R, 0.22) * 1.3
  a = gauss(R, 0.035) * 1.6 + gauss(R, 0.11) * 0.55 + gauss(R, 0.3) * 0.12 + spike + dust
  return light(a, k, 0.18)


def dust():
  """A drifting mote cloud: many tiny glints in a soft round envelope."""
  a = speckle(150, 0.05, 2) * gauss(R, 0.32) * 1.2 + gauss(R, 0.28) * 0.25 * fbm(X * 3, Y * 3, 151)
  return light(a, 4)


def streak():
  """A tapered streak of energy (motion-blurred sparks and bolts), grain along its length."""
  w = 0.09 * np.clip(1 - np.abs(Y), 0, 1) ** 0.7
  core = np.exp(-(X / np.maximum(w, 1e-3)) ** 2) * np.clip(1 - np.abs(Y) ** 2.5, 0, 1)
  n = fbm(X * 4, Y * 14, 160, 4)
  return light(core * (0.6 + 0.6 * n) + speckle(161, 0.02) * core, 5)


def burst():
  """A radial energy burst: rays of uneven length, broken by noise, hot white core."""
  rays = fbm(np.cos(A) * 3 + 10, np.sin(A) * 3 + 10, 170, 5)
  rays = smooth(0.35, 0.85, rays) ** 1.5
  ln = 0.35 + 0.6 * fbm(np.cos(A) * 2 + 3, np.sin(A) * 2 + 3, 171, 3)
  a = rays * np.clip(1 - R / ln, 0, 1) ** 1.7 + gauss(R, 0.07) * 1.4 + gauss(R, 0.2) * 0.4
  a += speckle(172, 0.02) * gauss(R, 0.4)
  return light(a, 6)


def shard():
  """A pane of the spell mantle: an irregular translucent facet, caustics inside, bright broken rim."""
  wx, wy = warp(X, Y, 180, 0.18)
  d = np.maximum(np.abs(wx) * 1.05 + np.abs(wy) * 0.2, np.abs(wy) * 1.0 + np.abs(wx) * 0.15)
  inside = smooth(0.9, 0.82, d)
  rim = np.exp(-np.abs(d - 0.84) / 0.025) * (0.5 + 0.7 * fbm(X * 6, Y * 6, 181))
  caust = smooth(0.48, 0.62, fbm(wx * 4, wy * 4, 182, 5)) * 0.55
  a = inside * (0.14 + caust + gauss(R, 0.35) * 0.35) + rim + speckle(183, 0.02) * inside * 0.8
  return light(a, 7, 0.2)


def bolt(k):
  """Branching lightning: a jagged main channel and two forks, white core, soft halo."""
  r = np.random.default_rng(700 + k)

  def path(p0, p1, disp, depth):
    pts = [p0, p1]
    for _ in range(depth):
      out = []
      for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        nx, ny = -(y1 - y0), x1 - x0
        t = r.uniform(-disp, disp)
        out += [(x0, y0), ((x0 + x1) / 2 + nx * t, (y0 + y1) / 2 + ny * t)]
      out.append(pts[-1])
      pts, disp = out, disp * 0.6
    return pts

  main = path((r.uniform(-.1, .1), -0.95), (r.uniform(-.1, .1), 0.95), 0.35, 6)
  segs = [(main, 1.0)]
  for _ in range(2):
    i = r.integers(len(main) // 4, 3 * len(main) // 4)
    x0, y0 = main[i]
    segs.append((path((x0, y0), (x0 + r.uniform(-.6, .6), y0 + r.uniform(.2, .5)), 0.3, 4), 0.55))
  a = np.zeros_like(R)
  for pts, w in segs:
    d = np.full_like(R, 9.0)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
      vx, vy = x1 - x0, y1 - y0
      t = np.clip(((X - x0) * vx + (Y - y0) * vy) / (vx * vx + vy * vy + 1e-9), 0, 1)
      d = np.minimum(d, np.hypot(X - (x0 + t * vx), Y - (y0 + t * vy)))
    a = np.maximum(a, w * (np.exp(-d / 0.012) + 0.45 * np.exp(-d / 0.06) + 0.15 * np.exp(-d / 0.18)))
  return light(a * np.clip(1 - np.abs(Y) ** 8, 0, 1), 8 + k, 0.15)


def ring(k):
  """A shock ring of dust, seen from above (drawn squashed on the floor): speckles in a ragged band."""
  rad = [0.45, 0.58, 0.7, 0.82][k]
  wid = [0.07, 0.09, 0.11, 0.13][k]
  rag = (fbm(np.cos(A) * 4 + 7, np.sin(A) * 4 + 7, 190 + k, 4) - 0.5) * 0.14
  band = np.exp(-((R - rad - rag) / wid) ** 2)
  dots = speckle(195 + k, 0.10, 2) * band * 1.5
  haze = band * 0.35 * fbm(X * 5, Y * 5, 196 + k)
  fade = [1.0, 0.9, 0.75, 0.55][k]
  return light((dots + haze) * fade, 12 + k, 0.1)


def smoke(k):
  """A curling wisp of smoke: domain-warped noise, soft contrast, in a round envelope, grainy."""
  wx, wy = warp(X * 1.2, Y * 1.2, 300 + 11 * k, 0.9, 1.8)
  n = fbm(wx * 2.2 + k, wy * 2.2 - k, 320 + k, 6)
  env = smooth(0.95, 0.3, R + 0.25 * (fbm(X * 2, Y * 2, 330 + k) - 0.5))
  a = (smooth(0.3, 0.8, n) * 0.75 + 0.2) * env * 0.85 + speckle(340 + k, 0.01) * env * 0.35
  return light(a, 16 + k, 0.2)


def flame_field(k, seed):
  """Rising flame intensity: warped noise scrolling up; it must beat a threshold that climbs with height,
  so the top breaks into separate licking tongues and the base stays solid."""
  t = k / 8.0
  wx, wy = warp(X * 1.2, Y * 1.2 + t * 2.0, seed, 0.7, 2.2)
  n = fbm(wx * 2.8, wy * 2.2 + t * 4.0, seed + 5, 6)
  up = (1 - Y) / 2  # 0 bottom .. 1 top
  xw = np.abs(X + 0.35 * (fbm(X * 2, Y * 2 + t * 3, seed + 9, 3) - 0.5) * up)
  width = 0.62 * (1 - up) ** 0.55
  body = smooth(width, width * 0.4, xw)
  tongues = smooth((up - 0.3) * 1.1, (up - 0.3) * 1.1 + 0.3, n)
  bowl = smooth(1.0, 0.55, np.hypot(X * 1.1, np.maximum(Y - 0.1, 0) * 1.6))
  heat = body * tongues * bowl * (0.4 + 0.75 * n) * (1.0 - 0.45 * up)
  return np.clip(heat * 1.05, 0, 1.2)


def fire(k):
  """Hellish flame like PST's: dark smoky fringe, deep red, orange, a yellow-white heart."""
  h = flame_field(k, 400)
  r = smooth(0.05, 0.45, h)
  g = smooth(0.3, 0.85, h) * 0.85
  b = smooth(0.75, 1.15, h) * 0.6
  dark = smooth(0.02, 0.2, h) * (1 - smooth(0.15, 0.4, h))  # sooty edge
  rgb = np.dstack([r, g, b]) * (1 - 0.5 * dark[..., None])
  a = smooth(0.03, 0.3, h) * np.clip(grain(424 + k, 0.2), 0, 1.2)
  return np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])


def wisp(k):
  """Spectral flame (tinted by the shader): the flame field as light only, softer and taller."""
  h = flame_field(k * 2, 500)
  return light(smooth(0.05, 0.9, h) + speckle(510 + k, 0.015) * smooth(0.05, 0.4, h) * 0.6, 35 + k, 0.2)


def skull(k):
  """A black skull rendered as a lit relief: cranium, brow, cheekbones, maxilla, jaw, deep sockets with an
  ember inside; charred bone with a dull sheen, grain, a faint warm rim. k = 1 is turned a little."""
  turn = [0.0, 0.22][k]
  u = X * 1.08 + turn * 0.25 * (1 - Y)
  v = Y * 1.08 + 0.05

  def ell(cx, cy, rx, ry):
    return np.sqrt(((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2) - 1.0

  def smin(a, b, k=0.12):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)

  cran = ell(0.0 + turn * 0.08, -0.24, 0.56, 0.52)
  cheek = smin(ell(-0.3 + turn * 0.1, 0.12, 0.2, 0.17), ell(0.3 + turn * 0.1, 0.12, 0.2, 0.17), 0.1)
  maxl = ell(0.0 + turn * 0.12, 0.26, 0.27, 0.2)
  jaw = ell(0.0 + turn * 0.14, 0.5, 0.22, 0.14)
  shape = smin(smin(smin(cran, cheek, 0.15), maxl, 0.12), jaw, 0.1)
  sock_l = ell(-0.2 + turn * 0.12, 0.04, 0.14, 0.11)
  sock_r = ell(0.2 + turn * 0.12, 0.04, 0.14 * (1 - turn), 0.11)
  nose = np.maximum(np.abs(u - turn * 0.13) - 0.06 * smooth(0.18, 0.36, v), np.abs(v - 0.27) - 0.09)
  holes = np.minimum(np.minimum(sock_l, sock_r), nose)
  teeth = (np.abs(v - 0.43) < 0.035) * (np.abs(np.sin((u - turn * 0.14) * 30)) < 0.35) * (np.abs(u - turn * 0.14) < 0.2)
  # relief: height from the outline inward, sockets cut down
  hgt = smooth(0.0, -0.35, shape) * 1.0 - smooth(0.05, -0.12, holes) * 0.9 - teeth * 0.3
  hgt += 0.06 * (fbm(u * 6, v * 6, 610, 4) - 0.5)  # pitted bone
  gy, gx = np.gradient(hgt)
  nz = 1.0 / np.sqrt(1 + (gx * 40) ** 2 + (gy * 40) ** 2)
  nx, ny = -gx * 40 * nz, -gy * 40 * nz
  lit = np.clip(nx * -0.5 + ny * -0.6 + nz * 0.62, 0, 1)  # light from top-left, in front
  # black: charred obsidian bone, a dull sheen where the light catches it
  rgb = np.array([0.045, 0.04, 0.04])[None, None, :] + np.array([0.36, 0.34, 0.33])[None, None, :] * (lit ** 2.2)[..., None]
  rim = smooth(-0.08, 0.0, shape) * 0.35  # faint warm rim (the fire behind it)
  rgb = rgb + np.array([0.35, 0.16, 0.06])[None, None, :] * rim[..., None]
  in_hole = smooth(0.02, -0.06, holes)
  ember = 0.95 * gauss(np.minimum(np.hypot(u + 0.2 - turn * 0.12, v - 0.06), np.hypot(u - 0.2 - turn * 0.12, v - 0.06)), 0.025)
  rgb = rgb * (1 - 0.97 * in_hole[..., None]) + np.array([0.9, 0.55, 0.2])[None, None, :] * (ember * in_hole * 0.9)[..., None]
  rgb *= np.clip(grain(620 + k, 0.25), 0.6, 1.3)[..., None]
  a = smooth(0.02, -0.04, shape) * (0.92 - 0.25 * in_hole + 0.25 * ember * in_hole)
  return np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])


def glow():
  return light(gauss(R, 0.38) * (0.85 + 0.3 * fbm(X * 3, Y * 3, 700)), 34, 0.12)


def downsample(img):
  return img.reshape(CELL, SS, CELL, SS, 4).mean(axis=(1, 3))


def build():
  atlas = np.zeros((ROWS * CELL, COLS * CELL, 4), np.float32)

  def put(i, img):
    img = downsample(img.astype(np.float32))
    img[0, :, 3] = img[-1, :, 3] = img[:, 0, 3] = img[:, -1, 3] = 0  # no bleeding into a neighbour
    r, c = divmod(i, COLS)
    atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = img

  for k in range(4): put(GLINT + k, glint(k))
  put(DUST, dust())
  put(STREAK, streak())
  put(BURST, burst())
  put(SHARD, shard())
  for k in range(4): put(BOLT + k, bolt(k))
  for k in range(4): put(RING + k, ring(k))
  for k in range(8): put(SMOKE + k, smoke(k))
  for k in range(8): put(FIRE + k, fire(k))
  for k in range(2): put(SKULL + k, skull(k))
  put(GLOW, glow())
  for k in range(4): put(WISP + k, wisp(k))
  return (np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8)


def write_atlas(rgba, path):
  h, w = rgba.shape[:2]
  head = b"PFA1" + struct.pack("<4H", w, h, CELL, COLS)
  open(path, "wb").write(head + zlib.compress(rgba.tobytes(), 9))


def preview(rgba, path):
  """Light cells drawn additive in a few PST tints, colour cells over a dark floor."""
  from PIL import Image
  f = rgba.astype(np.float32) / 255
  bg = np.zeros_like(f[..., :3]) + np.array([0.1, 0.085, 0.075])
  tints = {0: (0.75, 0.9, 1.0), 1: (0.55, 0.85, 1.0), 2: (0.7, 1.0, 0.5), 3: None, 4: (0.6, 1.0, 0.85)}
  out = bg.copy()
  for r in range(ROWS):
    for c in range(COLS):
      sl = (slice(r * CELL, (r + 1) * CELL), slice(c * CELL, (c + 1) * CELL))
      cell = f[sl]
      i = r * COLS + c
      if i in (SKULL, SKULL + 1) or FIRE <= i < FIRE + 8:
        out[sl] = bg[sl] * (1 - cell[..., 3:4]) + cell[..., :3] * cell[..., 3:4]
      else:
        t = np.array(tints.get(r) or (1, 1, 1)) if i != SHARD else np.array((0.45, 0.6, 1.0))
        out[sl] = np.clip(bg[sl] + cell[..., :3] * cell[..., 3:4] * t, 0, 1)
  Image.fromarray((out * 255).astype(np.uint8)).save(path)


def write_blank_bam(path):
  """PFVBLANK.BAM: one 1x1 transparent frame. A blinding spell's pending sticky overlay (op 369) is pointed at
  it, so the engine plays nothing there (statusfx_logic.h kBlankBam)."""
  pal = bytes([0, 255, 0, 0]) + bytes(4 * 255)          # index 0 = the transparent green (BGRA)
  frames_off = 0x18
  cycles_off = frames_off + 12
  pal_off = cycles_off + 4
  lut_off = pal_off + 1024
  data_off = lut_off + 2
  hdr = b"BAM V1  " + struct.pack("<HBB3I", 1, 1, 0, frames_off, pal_off, lut_off)
  frame = struct.pack("<HHhhI", 1, 1, 0, 0, data_off | 0x80000000)  # 1x1, not RLE
  cycle = struct.pack("<HH", 1, 0)
  open(path, "wb").write(hdr + frame + cycle + pal + struct.pack("<H", 0) + bytes([0]))


def main(a):
  write_blank_bam(os.path.join(ROOT, "data", "override", "PFVBLANK.BAM"))
  rgba = build()
  out = os.path.join(ROOT, "data", "statusfx_atlas.bin")
  write_atlas(rgba, out)
  print(out, os.path.getsize(out))
  if "--preview" in a: preview(rgba, a[a.index("--preview") + 1])


if __name__ == "__main__":
  main(sys.argv[1:])
