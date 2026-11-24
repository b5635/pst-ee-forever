# SPDX-License-Identifier: GPL-2.0-or-later
"""BAMs for the dialogue ability roll (BG3 style roll panel; deployed into the game's override folder).

    python -I tools/make_dice_bams.py [--preview OUT.png]

All are BAMC (zlib) around BAM V1 with a per-BAM palette; palette alpha is used (EE UI: 00 = opaque,
01..FF = alpha), index 0 is transparent.

  PFD20.BAM    the green d20, 150x150 frames: 0..23 tumbling (motion blurred), then 24 + s*50 + (n+9)
               resting with n (-9..40) on the front face at scale s (0 = 1.0, 1 = 1.12, 2 = 1.25), 174 blank
               (the BAM is the fallback of the real-time die, src/modules/rolls/die3d*)
  data/d20_numbers.bin  the real-time die's number atlas (--only D20NUM)
  PFDICON.BAM  36x36 list icon: frames 0..7 glow pulse, 8 = dim
  PFDRPNL.BAM  the roll panel (arch frame, gold lines, dark/purple fill, bronze bar): 300x440, 1 frame
  PFDSPRK.BAM  gold spark burst, 220x220, 12 frames
  PFDSMOK.BAM  dark red smoke burst, 220x220, 12 frames
  PFDBANR.BAM  result banner 460x120: variant v*8 + k (v: 0 SUCCESS, 1 CRITICAL SUCCESS, 2 FAILURE,
               3 CRITICAL FAILURE; k: 0..7 the wings growing in)
  PFDCARD.BAM  bonus card 62x70: ability a*2 + lit (a: STR DEX CON INT WIS CHR)
  PFDBTN.BAM   button band 200x34: 0 normal, 1 hover
"""
import argparse
import math
import struct
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "override"
FONTS = Path(r"C:\Windows\Fonts")
SERIF_B = str(FONTS / "palab.ttf")
SERIF = str(FONTS / "pala.ttf")
GOLD = (196, 160, 98)
GOLD_HI = (240, 210, 140)
RNG = np.random.default_rng(20261123)


# --- BAM writer --------------------------------------------------------------------------------------
def write_bam(path, frames):
  """frames: list of RGBA uint8 arrays (any sizes). One cycle with every frame, frame centre = 0,0.
  Palette: opaque pixels (alpha >= 235) and translucent ones get their own entries, so a gradient inside
  never maps to a see-through entry (speckles)."""
  allpx = np.concatenate([f.reshape(-1, 4) for f in frames]).astype(np.int32)
  opq = allpx[allpx[:, 3] >= 235]
  trn = allpx[(allpx[:, 3] > 8) & (allpx[:, 3] < 235)]
  n_t = 0 if len(trn) == 0 else (96 if len(opq) else 255)
  n_o = 255 - n_t

  def palette(px, n, rgba_mode):
    if len(px) == 0 or n == 0: return np.zeros((0, 4))
    sample = px[RNG.choice(len(px), min(len(px), 300000), replace=False)]
    side = int(math.ceil(math.sqrt(len(sample))))
    pad = np.zeros((side * side, 4), np.uint8)
    pad[:len(sample)] = sample
    pad[len(sample):] = sample[0]
    if rgba_mode:
      q = Image.fromarray(pad.reshape(side, side, 4), "RGBA").quantize(n, method=Image.Quantize.FASTOCTREE)
    else:
      q = Image.fromarray(pad[:, :3].reshape(side, side, 3), "RGB").quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    m = len(q.getpalette()) // 3
    pal = np.array(q.getpalette()[:min(n, m) * 3], np.float64).reshape(-1, 3)
    idx = np.array(q, np.int32).reshape(-1)[:len(sample)]
    alpha = np.full(len(pal), 255.0)
    if rgba_mode:
      for k in range(len(pal)):
        mk = idx == k
        if mk.any(): alpha[k] = sample[mk, 3].mean()
    return np.concatenate([pal, alpha[:, None]], 1)

  pal_o = palette(opq, n_o, False)
  pal_t = palette(trn, n_t, True)
  pal_rgba = np.concatenate([pal_o, pal_t], 0)
  pal_rgba = np.concatenate([pal_rgba, np.zeros((255 - len(pal_rgba), 4))], 0) if len(pal_rgba) < 255 else pal_rgba[:255]
  no = len(pal_o)

  def nearest(v, p, use_alpha):
    best = np.zeros(len(v), np.int32)
    bestd = np.full(len(v), 1 << 60, np.int64)
    for k0 in range(0, len(p), 32):
      q = p[k0:k0 + 32]
      d = ((v[:, None, :3] - q[None, :, :3]) ** 2).sum(2)
      if use_alpha: d = d + 2 * (v[:, None, 3] - q[None, :, 3]) ** 2
      a = d.argmin(1)
      dm = d[np.arange(len(v)), a]
      upd = dm < bestd
      best[upd] = a[upd] + k0
      bestd[upd] = dm[upd]
    return best

  def to_idx(f):
    h, w = f.shape[:2]
    px = f.reshape(-1, 4).astype(np.int64)
    out = np.zeros(len(px), np.uint8)
    o = px[:, 3] >= 235
    t = (px[:, 3] > 8) & ~o
    if o.any() and no: out[o] = nearest(px[o], pal_rgba[:no].astype(np.int64), False) + 1
    if t.any():
      if len(pal_t): out[t] = nearest(px[t], pal_rgba[no:no + len(pal_t)].astype(np.int64), True) + no + 1
      else: out[t] = nearest(px[t], pal_rgba[:no].astype(np.int64), False) + 1
    return out.reshape(h, w)

  idxs = [to_idx(f) for f in frames]
  n = len(frames)
  frames_off = 24
  pal_off = frames_off + 12 * n + 4
  lut_off = pal_off + 1024
  data_off = lut_off + 2 * n
  hdr = bytearray(b"BAM V1  " + struct.pack("<HBBIII", n, 1, 0, frames_off, pal_off, lut_off))
  data = bytearray()
  for i, ix in enumerate(idxs):
    h, w = ix.shape
    hdr += struct.pack("<HHhhI", w, h, w // 2, h // 2, (data_off + len(data)) | 0x80000000)
    data += ix.tobytes()
  hdr += struct.pack("<HH", n, 0)
  hdr += bytes((0, 255, 0, 0))
  for r, g, b, a in pal_rgba.astype(np.int32):
    a = int(round(a))
    hdr += bytes((b, g, r, 0 if a >= 250 else max(1, a)))
  hdr += b"".join(struct.pack("<H", i) for i in range(n))
  raw = bytes(hdr + data)
  path.write_bytes(b"BAMCV1  " + struct.pack("<I", len(raw)) + zlib.compress(raw, 9))
  return idxs, pal_rgba


def rgba(img):
  return np.array(img.convert("RGBA"), np.uint8)


def glow(img, radius, strength=1.0):
  """Soft glow behind an RGBA image (premultiplied blur of itself)."""
  a = np.array(img, np.float32) / 255.0
  pm = a.copy()
  pm[..., :3] *= pm[..., 3:4]
  pim = Image.fromarray((pm * 255).astype(np.uint8), "RGBA").filter(ImageFilter.GaussianBlur(radius))
  b = np.array(pim, np.float32) / 255.0 * strength
  b = np.clip(b, 0, 1)
  # under = blur, over = original
  out_a = a[..., 3:4] + b[..., 3:4] * (1 - a[..., 3:4])
  out_rgb = (a[..., :3] * a[..., 3:4] + b[..., :3] * (1 - a[..., 3:4])) / np.maximum(out_a, 1e-6)
  return Image.fromarray((np.concatenate([out_rgb, out_a], 2) * 255).clip(0, 255).astype(np.uint8), "RGBA")


# --- the d20 -----------------------------------------------------------------------------------------
PHI = (1 + 5 ** 0.5) / 2
VERTS = np.array([(0, 1, PHI), (0, -1, PHI), (0, 1, -PHI), (0, -1, -PHI), (1, PHI, 0), (-1, PHI, 0),
                  (1, -PHI, 0), (-1, -PHI, 0), (PHI, 0, 1), (-PHI, 0, 1), (PHI, 0, -1), (-PHI, 0, -1)], float)
VERTS /= np.linalg.norm(VERTS[0])


def _faces():
  fs = []
  n = len(VERTS)
  for i in range(n):
    for j in range(i + 1, n):
      for k in range(j + 1, n):
        if all(abs(np.linalg.norm(VERTS[a] - VERTS[b]) - 1.0515) < 0.01 for a, b in ((i, j), (j, k), (i, k))):
          c = VERTS[[i, j, k]].mean(0)
          nrm = np.cross(VERTS[j] - VERTS[i], VERTS[k] - VERTS[i])
          fs.append((i, j, k) if np.dot(nrm, c) > 0 else (i, k, j))
  return fs


FACES = _faces()
assert len(FACES) == 20


def _numbers():
  """Opposite faces sum to 21; numbers spread so 20 neighbours 2, 14, 8 like a real die."""
  cent = [VERTS[list(f)].mean(0) for f in FACES]
  num = [0] * 20
  order = sorted(range(20), key=lambda i: (-cent[i][2], cent[i][1], cent[i][0]))
  nxt = 1
  for i in order:
    if num[i]: continue
    opp = min(range(20), key=lambda j: np.linalg.norm(cent[j] + cent[i]))
    num[i] = nxt
    num[opp] = 21 - nxt
    nxt += 1
    if nxt > 10: break
  return num


NUMS = _numbers()


def rot_to_front(face):
  """Rotation that puts face `face` toward the viewer (+z) with one vertex straight up (-y screen)."""
  c = VERTS[list(FACES[face])].mean(0)
  z = c / np.linalg.norm(c)
  top = VERTS[FACES[face][0]] - c
  top -= np.dot(top, z) * z
  y = top / np.linalg.norm(top)  # screen up = +y in model before flip
  x = np.cross(y, z)
  return np.array([x, y, z])


def tilt(ax, ay):
  cx, sx, cy, sy = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay)
  rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
  ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
  return ry @ rx


def random_rot():
  q = RNG.normal(size=4)
  q /= np.linalg.norm(q)
  w, x, y, z = q
  return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                   [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                   [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


_tex_cache = {}


def face_texture(text, size=256, big=False):
  key = (text, big)
  if key in _tex_cache: return _tex_cache[key]
  im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
  d = ImageDraw.Draw(im)
  fs = int(size * (0.46 if len(text) < 2 else 0.38)) if big else int(size * 0.26)
  font = ImageFont.truetype(SERIF_B, fs)
  # equilateral triangle: apex (size/2, 0), base at y = size*0.866; centroid at 2/3 height
  cy = size * 0.866 * 2 / 3
  bb = d.textbbox((0, 0), text, font=font)
  tx = size / 2 - (bb[0] + bb[2]) / 2
  ty = cy - (bb[1] + bb[3]) / 2 + size * 0.02
  outline = (8, 30, 10, 255)
  for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, 1), (-1, 1), (1, -1)):
    d.text((tx + dx, ty + dy), text, font=font, fill=outline)
  d.text((tx, ty), text, font=font, fill=(226, 255, 214, 255))
  if text in ("6", "9"):  # underline marker like real dice
    w = (bb[2] - bb[0]) * 0.8
    d.line([(size / 2 - w / 2, ty + bb[3] + 6), (size / 2 + w / 2, ty + bb[3] + 6)], fill=(226, 255, 214, 255), width=5)
  _tex_cache[key] = im
  return im


def affine_from(src, dst):
  """Matrix for Image.transform(AFFINE): maps output (dst) coords to input (src) coords."""
  A = []
  B = []
  for (sx, sy), (dx, dy) in zip(src, dst):
    A.append([dx, dy, 1, 0, 0, 0]); B.append(sx)
    A.append([0, 0, 0, dx, dy, 1]); B.append(sy)
  return np.linalg.solve(np.array(A, float), np.array(B, float))


def render_die(R, size=150, radius=52, front_text=None, ss=4):
  """R: 3x3 rotation (rows = screen x, y, z in model space). Screen y up = model +y; viewer at +z."""
  S = size * ss
  im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
  P = VERTS @ R.T  # rows: vertices in view space
  scr = [(S / 2 + p[0] * radius * ss, S / 2 - p[1] * radius * ss) for p in P]
  light = np.array([-0.45, 0.6, 0.66])
  light /= np.linalg.norm(light)
  order = sorted(range(20), key=lambda f: P[list(FACES[f])].mean(0)[2])
  front = order[-1]
  d = ImageDraw.Draw(im)
  for f in order:
    a, b, c = FACES[f]
    nrm = np.cross(P[b] - P[a], P[c] - P[a])
    nrm /= np.linalg.norm(nrm)
    if nrm[2] <= 0.02: continue
    lam = max(0.0, float(np.dot(nrm, light)))
    spec = lam ** 18
    base = np.array([22, 92, 34], float)
    col = base * (0.45 + 0.75 * lam) + np.array([150, 255, 150]) * spec * 0.55
    col = tuple(int(v) for v in np.clip(col, 0, 255))
    poly = [scr[a], scr[b], scr[c]]
    d.polygon(poly, fill=col + (255,))
    # number texture mapped onto the face (orthographic = affine, exact)
    text = str(NUMS[f])
    if f == front and front_text is not None: text = front_text
    tex = face_texture(text, big=(f == front))
    T = 256
    src = [(T / 2, 0), (T, T * 0.866), (0, T * 0.866)]  # apex, base right, base left (clockwise, y down)
    area = (poly[1][0] - poly[0][0]) * (poly[2][1] - poly[0][1]) - (poly[1][1] - poly[0][1]) * (poly[2][0] - poly[0][0])
    m = affine_from(src, poly if area > 0 else [poly[0], poly[2], poly[1]])  # keep the text unmirrored
    warped = tex.transform((S, S), Image.AFFINE, tuple(m), resample=Image.BICUBIC)
    shade = 0.55 + 0.45 * nrm[2]
    wa = np.array(warped, np.float32)
    wa[..., 3] *= shade
    im.alpha_composite(Image.fromarray(wa.astype(np.uint8), "RGBA"))
  # edges: bright green on the visible faces
  d = ImageDraw.Draw(im)
  for f in order:
    a, b, c = FACES[f]
    nrm = np.cross(P[b] - P[a], P[c] - P[a])
    if nrm[2] / np.linalg.norm(nrm) <= 0.02: continue
    for u, v in ((a, b), (b, c), (c, a)):
      d.line([scr[u], scr[v]], fill=(120, 236, 112, 255), width=int(2.2 * ss))
  # silhouette darker rim
  out = im.resize((size, size), Image.LANCZOS)
  return out


def make_d20():
  frames = []
  # tumble: blurred random orientations
  for i in range(24):
    R0 = random_rot()
    acc = np.zeros((150, 150, 4), np.float32)
    ax = RNG.normal(size=3)
    ax /= np.linalg.norm(ax)
    for k in range(4):
      ang = (k - 1.5) * 0.18
      K = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
      Rk = np.eye(3) + math.sin(ang) * K + (1 - math.cos(ang)) * K @ K
      a = np.array(render_die(R0 @ Rk, ss=2), np.float32)
      a[..., :3] *= a[..., 3:4] / 255.0
      acc += a
    acc /= 4
    rgb = acc[..., :3] / np.maximum(acc[..., 3:4] / 255.0, 1e-3)
    frames.append(np.concatenate([rgb, acc[..., 3:4]], 2).clip(0, 255).astype(np.uint8))
  for s, sc in enumerate((1.0, 1.12, 1.25)):
    for n in range(-9, 41):
      # 1..20: the die's own face, neighbours as on a real d20; other totals on the 20 face's place
      face = NUMS.index(n) if 1 <= n <= 20 else NUMS.index(20)
      R = tilt(0.12, -0.08) @ rot_to_front(face)
      img = render_die(R, radius=52 * sc, front_text=str(n))
      frames.append(rgba(img))
  frames.append(np.zeros((150, 150, 4), np.uint8))  # 174: blank (the real-time die is drawn instead)
  return frames


def number_cell(text, big, T=128):
  """Two coverage planes (fill, outline) of one face's number, face_texture's layout at T px."""
  fill = Image.new("L", (T, T), 0)
  line = Image.new("L", (T, T), 0)
  df, dl = ImageDraw.Draw(fill), ImageDraw.Draw(line)
  fs = int(T * (0.46 if len(text) < 2 else 0.38)) if big else int(T * 0.26)
  font = ImageFont.truetype(SERIF_B, fs)
  cy = T * 0.866 * 2 / 3
  bb = df.textbbox((0, 0), text, font=font)
  tx = T / 2 - (bb[0] + bb[2]) / 2
  ty = cy - (bb[1] + bb[3]) / 2 + T * 0.02
  o = max(1, round(T / 128))
  for dx, dy in ((-o, 0), (o, 0), (0, -o), (0, o), (-o, -o), (o, o), (-o, o), (o, -o)):
    dl.text((tx + dx, ty + dy), text, font=font, fill=255)
  df.text((tx, ty), text, font=font, fill=255)
  if text in ("6", "9"):
    w = (bb[2] - bb[0]) * 0.8
    df.line([(T / 2 - w / 2, ty + bb[3] + 3 * o), (T / 2 + w / 2, ty + bb[3] + 3 * o)], fill=255, width=3 * o)
  return np.array(fill, np.uint8), np.array(line, np.uint8)


def make_numbers(path):
  """data/d20_numbers.bin for the real-time die (src/modules/rolls/die3d.h AtlasCell): "PFDN", u16 width,
  height, cell, columns, then zlib of width*height*2 bytes (fill, outline coverage per pixel)."""
  T, cols, rows = 128, 10, 7
  W, H = T * cols, T * rows
  px = np.zeros((H, W, 2), np.uint8)
  cells = [(str(n), True) for n in range(-9, 41)] + [(str(n), False) for n in range(1, 21)]
  for i, (text, big) in enumerate(cells):
    f, l = number_cell(text, big, T)
    y, x = (i // cols) * T, (i % cols) * T
    px[y:y + T, x:x + T, 0] = f
    px[y:y + T, x:x + T, 1] = l
  path.write_bytes(b"PFDN" + struct.pack("<HHHH", W, H, T, cols) + zlib.compress(px.tobytes(), 9))
  return px


def make_icon():
  R = tilt(0.12, -0.08) @ rot_to_front(NUMS.index(20))
  die = render_die(R, size=36, radius=12.5, front_text="20")
  frames = []
  for k in range(8):
    s = 0.55 + 0.45 * math.sin(k / 8 * 2 * math.pi)
    base = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
    halo = Image.new("RGBA", (36, 36), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((5, 5, 31, 31), fill=(255, 214, 120, int(170 * s)))
    halo = halo.filter(ImageFilter.GaussianBlur(4))
    base.alpha_composite(halo)
    base.alpha_composite(die)
    frames.append(rgba(base))
  dim = np.array(die, np.float32)
  dim[..., :3] *= 0.6
  frames.append(dim.astype(np.uint8))
  return frames


# --- panel -------------------------------------------------------------------------------------------
def arch_path(cx, top, bottom, half_w, r_top, r_bot, steps=40):
  pts = []
  # top arch from left to right
  for i in range(steps + 1):
    t = math.pi + i / steps * math.pi
    pts.append((cx + half_w * math.cos(t) * 1.0, top + r_top + r_top * math.sin(t)))
  # right side down
  pts.append((cx + half_w, bottom - r_bot))
  for i in range(steps + 1):
    t = i / steps * math.pi
    pts.append((cx + half_w * math.cos(t), bottom - r_bot + r_bot * math.sin(t)))
  pts.append((cx - half_w, top + r_top))
  return pts


def make_panel(W=300, H=440, ss=3):
  S = (W * ss, H * ss)
  im = Image.new("RGBA", S, (0, 0, 0, 0))
  cx = W * ss / 2
  bar_y = int(H * 0.43 * ss)
  inner = arch_path(cx, 24 * ss, (H - 24) * ss, 118 * ss, 70 * ss, 70 * ss)
  # fill: gradient, top charcoal, bottom purple
  mask = Image.new("L", S, 0)
  ImageDraw.Draw(mask).polygon(inner, fill=255)
  yy = np.arange(S[1])[:, None].astype(np.float32)
  top = np.array([22, 22, 26], np.float32)
  mid = np.array([30, 26, 38], np.float32)
  bot = np.array([44, 32, 62], np.float32)
  t1 = np.clip(yy / bar_y, 0, 1)
  t2 = np.clip((yy - bar_y) / (S[1] - bar_y), 0, 1)
  col = np.where(yy < bar_y, top + (mid - top) * t1, bot + (np.array([26, 20, 36]) - bot) * t2 ** 1.4)
  col = np.repeat(col[:, None, :], S[0], 1)
  noise = RNG.normal(0, 3.0, (S[1], S[0], 1))
  col = np.clip(col + noise, 0, 255)
  # specks
  for _ in range(90):
    x, y = RNG.integers(0, S[0]), RNG.integers(0, S[1])
    col[max(0, y - 1):y + 2, max(0, x - 1):x + 2] += RNG.uniform(20, 50)
  fill = np.concatenate([np.clip(col, 0, 255), np.array(mask, np.float32)[..., None] * 0.97], 2).astype(np.uint8)
  im.alpha_composite(Image.fromarray(fill, "RGBA"))
  d = ImageDraw.Draw(im)
  lw = int(1.6 * ss)
  d.line(inner + [inner[0]], fill=GOLD + (255,), width=lw, joint="curve")
  outer = arch_path(cx, 8 * ss, (H - 8) * ss, 134 * ss, 82 * ss, 82 * ss)
  d.line(outer + [outer[0]], fill=(150, 116, 70, 230), width=int(1.1 * ss), joint="curve")
  # corner horns: diagonal strokes with a small arrow at the top corners and bottom corners
  for sx in (-1, 1):
    for (y0, y1, dy) in ((60, 28, -1), (H - 60, H - 28, 1)):
      x0 = cx + sx * 118 * ss
      x1 = cx + sx * 146 * ss
      d.line([(x0, y0 * ss), (x1, y1 * ss)], fill=GOLD + (255,), width=lw)
      ax, ay = x1, y1 * ss
      d.polygon([(ax, ay), (ax - sx * 9 * ss, ay - dy * 2 * ss), (ax - sx * 3 * ss, ay - dy * 8 * ss)], fill=GOLD_HI + (255,))
      # little curl along the outer line
      d.arc([x0 - 14 * ss, (y0 - 14) * ss, x0 + 14 * ss, (y0 + 14) * ss],
            0 if sx > 0 else 180, 90 if sx > 0 else 270, fill=GOLD + (220,), width=lw)
  # brackets at bar level on both sides
  for sx in (-1, 1):
    x = cx + sx * 134 * ss
    d.line([(x, bar_y - 26 * ss), (x + sx * 8 * ss, bar_y - 8 * ss), (x + sx * 8 * ss, bar_y + 8 * ss), (x, bar_y + 26 * ss)],
           fill=GOLD + (255,), width=lw)
  # bronze braided bar
  bx0, bx1, bh = cx - 128 * ss, cx + 128 * ss, 6 * ss
  bar = Image.new("RGBA", S, (0, 0, 0, 0))
  bd = ImageDraw.Draw(bar)
  bd.rectangle([bx0, bar_y - bh, bx1, bar_y + bh], fill=(116, 72, 34, 255))
  step = 7 * ss
  x = bx0
  while x < bx1:
    bd.line([(x, bar_y + bh), (x + step * 0.8, bar_y - bh)], fill=(178, 120, 62, 255), width=int(1.4 * ss))
    bd.line([(x + step * 0.4, bar_y + bh), (x + step * 1.2, bar_y - bh)], fill=(70, 42, 20, 255), width=int(0.8 * ss))
    x += step
  bd.line([(bx0, bar_y - bh), (bx1, bar_y - bh)], fill=(214, 168, 100, 255), width=int(1.1 * ss))
  bd.line([(bx0, bar_y + bh), (bx1, bar_y + bh)], fill=(58, 34, 16, 255), width=int(1.1 * ss))
  for x in (bx0, bx1):
    bd.ellipse([x - 7 * ss, bar_y - 9 * ss, x + 7 * ss, bar_y + 9 * ss], fill=(150, 104, 54, 255), outline=GOLD_HI + (255,), width=ss)
  im.alpha_composite(bar)
  out = im.resize((W, H), Image.LANCZOS)
  return rgba(out)


# --- particles ---------------------------------------------------------------------------------------
def make_burst(colors, n=70, size=220, frames=12, smoke=False, seed=1):
  rng = np.random.default_rng(seed)
  ang = rng.uniform(0, 2 * math.pi, n)
  spd = rng.uniform(0.35, 1.0, n)
  curl = rng.normal(0, 1.4, n)
  out = []
  for k in range(frames):
    t = (k + 1) / frames
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for i in range(n):
      def pos(tt):
        r = spd[i] * (size * 0.46) * (1 - (1 - tt) ** 2.2)
        a = ang[i] + curl[i] * tt * 0.9
        return size / 2 + r * math.cos(a), size / 2 + r * math.sin(a) * 0.8
      fade = (1 - t) ** 1.3
      c = colors[i % len(colors)]
      if smoke:
        x, y = pos(t)
        rad = 6 + 26 * t * spd[i]
        d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=c + (int(110 * fade),))
      else:
        p0, p1 = pos(max(0, t - 0.14)), pos(t)
        d.line([p0, p1], fill=c + (int(255 * fade),), width=2)
        d.ellipse([p1[0] - 1.5, p1[1] - 1.5, p1[0] + 1.5, p1[1] + 1.5], fill=(255, 246, 210, int(255 * fade)))
    if smoke:
      im = im.filter(ImageFilter.GaussianBlur(6))
    else:
      im = glow(im, 3, 0.9)
    out.append(rgba(im))
  return out


# --- banner ------------------------------------------------------------------------------------------
def wings(W, H, color, seed, dark=False):
  """Smoky wings behind the result word: thin horizontal streaks (noise stretched along x) that fan out
  and thin towards the ends, plus a few bright embers (the BG3 banner)."""
  rng = np.random.default_rng(seed)
  field = np.zeros((H, W), np.float32)
  for (fy, fx), amp in (((10, 6), 1.0), ((26, 14), 0.7), ((48, 30), 0.45)):
    nz = rng.normal(size=(fy, fx)).astype(np.float32)
    im = Image.fromarray(((nz - nz.min()) / (np.ptp(nz) + 1e-6) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
    field += amp * (np.array(im, np.float32) / 255.0)
  field /= field.max()
  strands = np.clip((field - 0.45) * 3.2, 0, 1) ** 1.5
  x = (np.arange(W) - W / 2) / (W / 2)
  y = (np.arange(H) - H / 2) / (H / 2)
  env_x = np.clip(1 - np.abs(x) ** 2.2, 0, 1)[None, :]
  spread = 0.22 + 0.38 * np.abs(x)[None, :]
  env_y = np.exp(-(y[:, None] / spread) ** 2)
  core = np.exp(-(y[:, None] / 0.18) ** 2) * np.exp(-(x[None, :] / 0.55) ** 2) * 0.55
  a = np.clip(strands * env_x * env_y * 0.95 + core, 0, 1)
  rgb = np.zeros((H, W, 3), np.float32) + np.array(color, np.float32)
  rgb *= (0.65 + 0.6 * field[..., None])
  out = np.concatenate([np.clip(rgb, 0, 255), (a * 220)[..., None]], 2)
  for _ in range(40):  # embers
    ex = int(W / 2 + rng.normal(0, W * 0.25))
    ey = int(H / 2 + rng.normal(0, H * 0.18))
    if 1 <= ex < W - 1 and 1 <= ey < H - 1:
      out[ey, ex] = (255, 240, 200, 255)
      out[ey, ex - 1, 3] = max(out[ey, ex - 1, 3], 150)
      out[ey, ex + 1, 3] = max(out[ey, ex + 1, 3], 150)
  return out


def make_banner(W=460, H=120):
  variants = [("SUCCESS", (232, 196, 110), (242, 230, 200), 46),
              ("CRITICAL SUCCESS", (240, 206, 110), (250, 238, 206), 34),
              ("FAILURE", (150, 46, 40), (226, 160, 150), 46),
              ("CRITICAL FAILURE", (140, 36, 30), (230, 150, 140), 34)]
  frames = []
  for v, (text, wing_col, text_col, pt) in enumerate(variants):
    wg = wings(W, H, wing_col, seed=7 + v)
    font = ImageFont.truetype(SERIF_B, pt)
    for k in range(8):
      t = (k + 1) / 8
      reach = 0.15 + 0.85 * (1 - (1 - t) ** 2)
      x = np.abs((np.arange(W) - W / 2) / (W / 2))
      m = np.clip((reach - x) / 0.12, 0, 1)[None, :]
      w = wg.copy()
      w[..., 3] *= m
      im = Image.fromarray(w.astype(np.uint8), "RGBA")
      if k >= 1:
        txt = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(txt)
        bb = d.textbbox((0, 0), text, font=font)
        tx, ty = W / 2 - (bb[0] + bb[2]) / 2, H / 2 - (bb[1] + bb[3]) / 2
        ta = int(255 * min(1.0, (k) / 4))
        for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2), (0, 3)):
          d.text((tx + dx, ty + dy), text, font=font, fill=(20, 12, 8, int(ta * 0.8)))
        d.text((tx, ty), text, font=font, fill=text_col + (ta,))
        txt = glow(txt, 5, 0.6)
        im.alpha_composite(txt)
      frames.append(rgba(im))
  return frames


# --- cards and button --------------------------------------------------------------------------------
ABILS = ["STR", "DEX", "CON", "INT", "WIS", "CHA"]


def emblem(d, a, cx, cy, col, k=1):
  """Simple engraved emblem per ability (k = supersampling factor)."""
  w = 3 * k
  _d = d

  class _S:  # scales every coordinate around (cx, cy) by k
    def __getattr__(self, name):
      f = getattr(_d, name)

      def call(xy, *args, **kw):
        def sc(p): return (cx + (p[0] - cx) * k, cy + (p[1] - cy) * k)
        if isinstance(xy, list) and xy and isinstance(xy[0], tuple): xy = [sc(p) for p in xy]
        elif isinstance(xy, list): xy = [cx + (xy[0] - cx) * k, cy + (xy[1] - cy) * k, cx + (xy[2] - cx) * k, cy + (xy[3] - cy) * k]
        return f(xy, *args, **kw)
      return call
  d = _S()
  if a == 0:  # STR: a fist-like block (anvil)
    d.polygon([(cx - 12, cy - 4), (cx + 12, cy - 4), (cx + 7, cy + 3), (cx - 7, cy + 3)], outline=col, width=w)
    d.rectangle([cx - 4, cy + 3, cx + 4, cy + 10], outline=col, width=w)
  elif a == 1:  # DEX: feather / arrow
    d.line([(cx - 11, cy + 11), (cx + 11, cy - 11)], fill=col, width=w)
    d.polygon([(cx + 11, cy - 11), (cx + 3, cy - 9), (cx + 9, cy - 3)], fill=col)
  elif a == 2:  # CON: heart
    d.polygon([(cx, cy + 11), (cx - 12, cy - 1), (cx - 6, cy - 9), (cx, cy - 4), (cx + 6, cy - 9), (cx + 12, cy - 1)], outline=col, width=w)
  elif a == 3:  # INT: open book
    d.line([(cx, cy - 8), (cx, cy + 9)], fill=col, width=w)
    d.polygon([(cx, cy - 8), (cx - 13, cy - 10), (cx - 13, cy + 7), (cx, cy + 9)], outline=col, width=w)
    d.polygon([(cx, cy - 8), (cx + 13, cy - 10), (cx + 13, cy + 7), (cx, cy + 9)], outline=col, width=w)
  elif a == 4:  # WIS: eye
    d.ellipse([cx - 13, cy - 7, cx + 13, cy + 7], outline=col, width=w)
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=col)
  else:  # CHA: crown
    d.polygon([(cx - 12, cy + 7), (cx - 12, cy - 7), (cx - 6, cy), (cx, cy - 10), (cx + 6, cy), (cx + 12, cy - 7), (cx + 12, cy + 7)], outline=col, width=w)


def make_cards(W=62, H=70, ss=3):
  frames = []
  for a in range(6):
    for lit in (0, 1):
      S = (W * ss, H * ss)
      im = Image.new("RGBA", S, (0, 0, 0, 0))
      d = ImageDraw.Draw(im)
      top, bot = (44, 34, 64), (30, 22, 44)
      for y in range(S[1]):
        t = y / S[1]
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        d.line([(3 * ss, y), (S[0] - 3 * ss, y)], fill=c + (240,))
      border = GOLD_HI if lit else (150, 120, 80)
      d.rounded_rectangle([2 * ss, 2 * ss, S[0] - 2 * ss, S[1] - 2 * ss], radius=4 * ss, outline=border + (255,), width=int((2.2 if lit else 1.4) * ss))
      emblem(d, a, S[0] // 2, int(S[1] * 0.60), (226, 196, 140, 255) if lit else (180, 160, 130, 255), ss)
      img = im.resize((W, H), Image.LANCZOS)
      if lit: img = glow(img, 4, 0.8)
      frames.append(rgba(img))
  return frames


def make_button(W=200, H=34):
  frames = []
  for hov in (0, 1):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    a = np.zeros((H, W, 4), np.float32)
    x = np.abs((np.arange(W) - W / 2) / (W / 2))
    env = np.clip(1 - x ** 3, 0, 1)
    a[..., 0], a[..., 1], a[..., 2] = (52, 40, 66) if hov else (34, 28, 44)
    a[..., 3] = env[None, :] * (215 if hov else 170)
    im = Image.fromarray(a.astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(im)
    for y in (1, H - 2):
      for xx in range(W):
        e = env[xx]
        if e > 0.02: d.point((xx, y), fill=GOLD + (int(255 * e * (1.0 if hov else 0.75)),))
    frames.append(rgba(im))
  return frames


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument("--preview")
  ap.add_argument("--only")
  a = ap.parse_args()
  OUT.mkdir(parents=True, exist_ok=True)
  jobs = {"PFD20": make_d20, "PFDICON": make_icon, "PFDRPNL": lambda: [make_panel()],
          "PFDSPRK": lambda: make_burst([(255, 214, 120), (255, 184, 80), (255, 236, 170)], seed=3),
          "PFDSMOK": lambda: make_burst([(70, 20, 18), (40, 30, 30), (96, 30, 24)], n=40, smoke=True, seed=5),
          "PFDBANR": make_banner, "PFDCARD": make_cards, "PFDBTN": make_button}
  made = {}
  if not a.only or "D20NUM" in a.only.split(","):
    p = ROOT / "data" / "d20_numbers.bin"
    make_numbers(p)
    print(f"{p.name} {p.stat().st_size} bytes")
  for name, fn in jobs.items():
    if a.only and name not in a.only.split(","): continue
    frames = fn()
    write_bam(OUT / f"{name}.BAM", frames)
    made[name] = frames
    print(f"{name}.BAM {len(frames)} frames {frames[0].shape[1]}x{frames[0].shape[0]} "
          f"{(OUT / f'{name}.BAM').stat().st_size} bytes")
  if a.preview:
    picks = []
    for name, fr in made.items():
      sel = fr if len(fr) <= 8 else [fr[i] for i in np.linspace(0, len(fr) - 1, 8).astype(int)]
      picks.append(sel)
    W = max(sum(f.shape[1] + 6 for f in row) for row in picks)
    H = sum(max(f.shape[0] for f in row) + 6 for row in picks)
    sheet = Image.new("RGBA", (W, H), (60, 60, 60, 255))
    y = 0
    for row in picks:
      x = 0
      for f in row:
        sheet.alpha_composite(Image.fromarray(f, "RGBA"), (x, y))
        x += f.shape[1] + 6
      y += max(f.shape[0] for f in row) + 6
    sheet.save(a.preview)


if __name__ == "__main__":
  main()
