# SPDX-License-Identifier: GPL-2.0-or-later
"""Per-frame depth (height above the billboard plane) for PST:EE character sprite BAMs.

Offline data for the `lighting` module (docs/VISUALS_PLAN.md 5.2, phase 3). Format: FORMAT.md here.

  python -I tools/sprite_depth/sprite_depth.py list            rewrite character_bams.txt from RESDATA.INI
  <venv python> -I tools/sprite_depth/sprite_depth.py run [--only REGEX] [--limit N] [--force] [--dome-only]
  <venv python> -I tools/sprite_depth/sprite_depth.py review BAM [BAM ...] [--out DIR]
  python -I tools/sprite_depth/sprite_depth.py stats           totals of the written .dep files
  python -I tools/sprite_depth/sprite_depth.py info BAM        decode one BAM / .dep, print sizes

`run` and `review` need torch + transformers (GemRB venv: D:\\gemrb\\tools\\upscale\\.venv\\Scripts\\python.exe).
Everything else (decode, dome, file format) is numpy/scipy only, so the tests run with plain python.

Height model per frame (all in screen pixels, 0 outside the surface):
  surface = palette index > 1 (0 transparent, 1 the shadow pixel)
  d       = distance to the silhouette edge minus 1 (edge pixels 0)
  r       = body radius of the BAM: median over its frames of max(d)
  dome    = r * sqrt(1 - (1 - min(d, r)/r)^2)
  model   = Depth-Anything disparity on the padded, upscaled frame, fitted to the dome by robust
            least squares (a*p + b, a >= 0) inside the surface: the dome sets the scale per frame,
            the model only adds shape, so frames cannot jump in scale
  detail  = model - dome, smoothed over the neighbours of the cycle (frames aligned by their
            centre point), so per-frame model noise does not flicker
  h       = (dome + MODEL_WEIGHT * detail) * edge taper, clipped to [0, 1.5 r]
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import sys
import time
import zlib
from dataclasses import dataclass, field

import numpy as np
from scipy.ndimage import binary_fill_holes, distance_transform_edt

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
if TOOLS not in sys.path:
  sys.path.append(TOOLS)  # append: tools/re.py must not shadow the stdlib re
import ieres  # noqa: E402

GAME = ieres.GAME
OUT_DIR = os.path.join(GAME, "pst_forever", "assets", "sprites", "depth")
LIST_FILE = os.path.join(HERE, "character_bams.txt")
HF_HOME = r"D:\gemrb\tools\upscale\models\hf"
MODEL = "xingyang1/Distill-Any-Depth-Large-hf"  # MIT licence (Depth-Anything-V2-Large is CC-BY-NC)
DEPTH_INPUT = 518  # model input side, multiple of 14
PAD_FRAC = 0.15
BG_GREY = 0.5
MODEL_WEIGHT = 0.6
EDGE_TAPER = 3.0  # px over which the model detail fades in from the silhouette
TEMPORAL = 1  # neighbours on each side in the detail smoothing
SMOOTH_TAPER = 6.0  # px over which a neighbour's weight fades in from its silhouette
NEIGHBOUR_WEIGHT = 0.5
MIN_MODEL_PIXELS = 24  # frames with fewer surface pixels get the dome only
VERSION = 1

# Static creature animations that have no RESDATA.INI section (animate.ids STAR_* entries;
# avatars.2da type 57 in GemRB). Gears (0x2000) are machinery, IGSPARK/POSSHAD/POSTANT effects or shadows.
STATIC_CREATURES = {"IGHEAD": "0xF101 STAR_Iron_Golem", "IGARM": "0xF101 STAR_Iron_Golem",
                    "IGARMB": "0xF101 STAR_Iron_Golem", "POSMAIN": "0xF100 STAR_Pillar_of_Skulls"}
SEQ_KEY = re.compile(r"^(attack|walk|run|stand|stance|die|getup|gethit|spell|talk|misc)")


def params_tag() -> int:
  s = f"{VERSION}|{MODEL}|{DEPTH_INPUT}|{PAD_FRAC}|{BG_GREY}|{MODEL_WEIGHT}|{EDGE_TAPER}|{TEMPORAL}|{SMOOTH_TAPER}|{NEIGHBOUR_WEIGHT}|{MIN_MODEL_PIXELS}"
  return zlib.crc32(s.encode())


# ---------------------------------------------------------------- resources

def read_res(name: str, ext: str) -> bytes | None:
  """ieres.read, plus resources whose KEY type has no name there (RESDATA is type 0x802, INI)."""
  d = ieres.read(name, ext)
  if d is not None or not ext.lower().startswith("0x"):
    return d
  key = ieres.load_key()
  if (name.upper(), ext.lower()) not in key:
    return None
  bif, idx = key[(name.upper(), ext.lower())]
  b = open(os.path.join(GAME, bif), "rb").read()
  n, _, oe = struct.unpack_from("<3I", b, 8)
  for i in range(n):
    loc, off, size, _t, _u = struct.unpack_from("<IIIHH", b, oe + i * 16)
    if loc & 0x3FFF == idx:
      return b[off:off + size]
  return None


def all_bam_names() -> set[str]:
  names = {n for n, e in ieres.load_key() if e == "BAM"}
  names |= {f.upper()[:-4] for f in os.listdir(os.path.join(GAME, "override")) if f.upper().endswith(".BAM")}
  return names


def character_bams() -> list[tuple[str, str]]:
  """(BAM, why) for every creature animation BAM the EE engine can load for a creature."""
  ini = read_res("RESDATA", "0x802").decode("latin1")
  names = all_bam_names()
  out: dict[str, str] = {}
  section = ""
  for raw in ini.splitlines():
    if raw.strip().startswith("["):
      section = raw.strip()
      continue
    line = raw.split("//")[0].strip()
    if "=" not in line:
      continue
    k, v = (s.strip() for s in line.split("=", 1))
    k = k.lower()
    if not SEQ_KEY.match(k) or re.search("sound|frame|scale|walk\\d|run\\d", k) or k.startswith("foot"):
      continue
    for x in v.split(","):
      x = x.strip().upper()
      if not x or x.isdigit():
        continue
      cands = [x + "B", x]
      cands += [("D" if c[0] == "C" else "C") + c[1:] for c in cands if c[0] in "CD"]
      hit = next((c for c in cands if c in names), None)
      if hit and hit not in out:
        out[hit] = f"RESDATA {section.split('//')[-1].strip() or section} {k}"
  for b, why in STATIC_CREATURES.items():
    if b in names:
      out.setdefault(b, why)
  return sorted(out.items())


def write_list() -> list[tuple[str, str]]:
  rows = character_bams()
  with open(LIST_FILE, "w", encoding="utf-8", newline="\n") as f:
    f.write("# SPDX-License-Identifier: GPL-2.0-or-later\n"
            "# Character sprite BAMs of PST:EE 3.2.2 (creature animations only), written by\n"
            "# `python -I tools/sprite_depth/sprite_depth.py list`.\n"
            "# How: EE maps a creature's ANIMATE.IDS value 0xF000+n to section [n] of RESDATA.INI\n"
            "# (resource type 0x802 in chitin.key). Every sequence key there (attack*, walk, run, stand*,\n"
            "# stance*, diebackward, getup, gethit, spell*, talk*, misc*) names a 7-char base; the file is\n"
            "# base + 'B' (the GemRB avatars.2da prefixes are the same CxxxB/DxxxB names). Where the C/D letter\n"
            "# in RESDATA is wrong (Baurier: CAT1BAU -> DAT1BAUB) the other letter is used. Plus the static\n"
            "# creatures without a section: Iron Golem (IGHEAD, IGARM, IGARMB) and Pillar of Skulls (POSMAIN).\n"
            "# Left out: spell effects, projectiles, area animations, GUI, the Foundry gears, shadow/effect\n"
            "# overlays (POSSHAD, POSTANT, IGSPARK) and 77 classic-only C*B leftovers no RESDATA entry loads\n"
            "# (e.g. CWLKNOFB; EE draws the D* versions).\n"
            f"# {len(rows)} BAMs. Columns: BAM <tab> source\n")
    for b, why in rows:
      f.write(f"{b}\t{why}\n")
  return rows


def read_list() -> list[str]:
  out = []
  for line in open(LIST_FILE, encoding="utf-8"):
    if line.strip() and not line.startswith("#"):
      out.append(line.split("\t")[0].strip())
  return out


# ---------------------------------------------------------------- BAM

@dataclass
class Frame:
  w: int
  h: int
  cx: int
  cy: int
  idx: np.ndarray  # uint8 [h, w] palette indices


@dataclass
class Bam:
  frames: list[Frame]
  cycles: list[tuple[int, int]]  # (count, first lookup index)
  lookup: list[int]
  palette: np.ndarray  # uint8 [256, 3] RGB

  def cycle_frames(self, c: int) -> list[int]:
    n, first = self.cycles[c]
    return [self.lookup[first + i] for i in range(n)]


def decode_bam(data: bytes) -> Bam:
  if data[:4] == b"BAMC":
    data = zlib.decompress(data[12:])
  if data[:8] != b"BAM V1  ":
    raise ValueError(f"not a BAM V1: {data[:8]!r}")
  nf, nc, cidx = struct.unpack_from("<HBB", data, 8)
  ofr, opal, olut = struct.unpack_from("<3I", data, 12)
  pal = np.frombuffer(data, np.uint8, 1024, opal).reshape(256, 4)[:, [2, 1, 0]].copy()
  frames = []
  for i in range(nf):
    w, h, cx, cy, off = struct.unpack_from("<HHhhI", data, ofr + i * 12)
    raw = off & 0x80000000
    off &= 0x7FFFFFFF
    n = w * h
    if raw:
      px = np.frombuffer(data, np.uint8, n, off).copy()
    else:
      px = np.empty(n, np.uint8)
      j, o = 0, off
      while j < n:
        b = data[o]
        o += 1
        if b == cidx:
          k = min(data[o] + 1, n - j)
          o += 1
          px[j:j + k] = cidx
          j += k
        else:
          px[j] = b
          j += 1
    frames.append(Frame(w, h, cx, cy, px.reshape(h, w)))
  oc = ofr + nf * 12
  cycles = [struct.unpack_from("<HH", data, oc + i * 4) for i in range(nc)]
  nl = max((c + f for c, f in cycles), default=0)
  lookup = list(struct.unpack_from(f"<{nl}H", data, olut)) if nl else []
  return Bam(frames, cycles, lookup, pal)


def load_bam(name: str) -> tuple[Bam, bytes]:
  d = read_res(name, "BAM")
  if d is None:
    raise FileNotFoundError(f"{name}.BAM")
  return decode_bam(d), d


# ---------------------------------------------------------------- height model (pure)

def surface_mask(idx: np.ndarray) -> np.ndarray:
  """Index 0 is transparent, index 1 the shadow pixel: neither is part of the body surface."""
  return idx > 1


def body_mask(idx: np.ndarray) -> np.ndarray:
  """Surface plus the shadow-index pixels it encloses. PST artists also used index 1 for dark
  spots inside the figure; those are drawn as shadow, but the body's shape runs through them."""
  m = surface_mask(idx)
  return binary_fill_holes(m) & (m | (idx == 1))


def edge_distance(mask: np.ndarray, body: np.ndarray | None = None) -> np.ndarray:
  """Distance (inside `body`, default `mask`) to the nearest outside pixel minus 1: the outermost
  ring is 0. Zero off `mask`."""
  body = mask if body is None else body
  if not mask.any():
    return np.zeros(mask.shape, np.float32)
  d = distance_transform_edt(np.pad(body, 1)).astype(np.float32)[1:-1, 1:-1]
  return np.where(mask, np.maximum(d - 1.0, 0.0), 0.0).astype(np.float32)


def dome(d: np.ndarray, r: float) -> np.ndarray:
  """h = r * sqrt(1 - (1 - d/r)^2), d capped at r (VISUALS_PLAN 5.2)."""
  if r <= 0:
    return np.zeros_like(d)
  t = np.minimum(d, r) / r
  return (r * np.sqrt(np.maximum(1.0 - (1.0 - t) ** 2, 0.0))).astype(np.float32)


def body_radius(dists: list[np.ndarray]) -> float:
  m = [float(d.max()) for d in dists if d.size and d.max() > 0]
  return max(float(np.median(m)), 1.0) if m else 1.0


def fit_to_dome(p: np.ndarray, dm: np.ndarray, mask: np.ndarray, rounds: int = 3) -> np.ndarray:
  """Robust (Huber-IRLS) a*p + b ~ dome inside the mask, a >= 0. Returns the fitted model height."""
  x = p[mask].astype(np.float64)
  y = dm[mask].astype(np.float64)
  if x.size < 3 or np.ptp(x) < 1e-6:
    return dm.copy()
  w = np.ones_like(x)
  a, b = 0.0, float(np.mean(y))
  for _ in range(rounds):
    sw = w.sum()
    mx, my = (w * x).sum() / sw, (w * y).sum() / sw
    vx = (w * (x - mx) ** 2).sum()
    a = max(((w * (x - mx) * (y - my)).sum() / vx) if vx > 0 else 0.0, 0.0)
    b = my - a * mx
    res = np.abs(a * x + b - y)
    delta = max(float(np.median(res)) * 1.5, 0.5)
    w = np.where(res <= delta, 1.0, delta / np.maximum(res, 1e-9))
  return (a * p + b).astype(np.float32)


def taper(d: np.ndarray) -> np.ndarray:
  t = np.clip(d / EDGE_TAPER, 0.0, 1.0)
  return (t * t * (3 - 2 * t)).astype(np.float32)


def combine(dm: np.ndarray, detail: np.ndarray, d: np.ndarray, mask: np.ndarray, r: float,
            weight: float = MODEL_WEIGHT) -> np.ndarray:
  h = (dm + weight * detail * taper(d))
  h = np.clip(h, 0.0, 1.5 * r)
  return np.where(mask, h, 0.0).astype(np.float32)


def smooth_detail(details: list[np.ndarray | None], masks: list[np.ndarray], dists: list[np.ndarray],
                  frames: list[Frame], order: list[int], k: int | None = None) -> list[np.ndarray | None]:
  """Average each frame's model detail with its k neighbours in cycle order (canvas aligned by the
  centre points). A neighbour's weight fades in over SMOOTH_TAPER px from its silhouette, so its
  edge leaves no seam."""
  k = TEMPORAL if k is None else k
  if k <= 0:
    return list(details)
  out = list(details)
  for pos, fi in enumerate(order):
    if details[fi] is None:
      continue
    acc = details[fi].astype(np.float64)
    wsum = np.ones_like(acc)
    for dp in range(-k, k + 1):
      if dp == 0 or not (0 <= pos + dp < len(order)):
        continue
      gi = order[pos + dp]
      if gi == fi or details[gi] is None:
        continue
      ov = _overlap(frames[fi], frames[gi])
      if ov is None:
        continue
      sf, sg = ov
      w = np.clip(dists[gi][sg] / SMOOTH_TAPER, 0, 1) * masks[gi][sg] * NEIGHBOUR_WEIGHT
      acc[sf] += w * details[gi][sg]
      wsum[sf] += w
    out[fi] = np.where(masks[fi], acc / wsum, 0.0).astype(np.float32)
  return out


def _overlap(fa: Frame, fb: Frame):
  """Slices of fa and fb that cover the same canvas points (frames aligned by their centre points)."""
  ox, oy = fb.cx - fa.cx, fb.cy - fa.cy
  ys0, ys1 = max(0, -oy), min(fa.h, fb.h - oy)
  xs0, xs1 = max(0, -ox), min(fa.w, fb.w - ox)
  if ys0 >= ys1 or xs0 >= xs1:
    return None
  return (slice(ys0, ys1), slice(xs0, xs1)), (slice(ys0 + oy, ys1 + oy), slice(xs0 + ox, xs1 + ox))


def flicker(heights: list[np.ndarray], masks: list[np.ndarray], frames: list[Frame], order: list[int],
            static: bool = True) -> tuple[float, int]:
  """Sum and count of mean |h(t+1) - h(t)| over consecutive frames of a cycle. static=True counts only
  pixels whose palette index is the same in both frames (the surface did not move there, so any height
  change is flicker); static=False counts every pixel that is surface in both (includes real motion)."""
  tot, n = 0.0, 0
  for a, b in zip(order, order[1:]):
    if a == b:
      continue
    ov = _overlap(frames[a], frames[b])
    if ov is None:
      continue
    sa, sb = ov
    both = masks[a][sa] & masks[b][sb]
    if static:
      both &= frames[a].idx[sa] == frames[b].idx[sb]
    if both.sum() < 8:
      continue
    tot += float(np.abs(heights[a][sa] - heights[b][sb])[both].mean())
    n += 1
  return tot, n


# ---------------------------------------------------------------- .dep file

HDR = struct.Struct("<4sHHHHIIIII")  # magic ver flags nframes ncycles ofr ocyc olut bamcrc params = 32 bytes
FRM = struct.Struct("<HHhhfII")      # w h cx cy scale offset csize = 20 bytes
FLAG_U16 = 1


def encode_dep(frames: list[Frame], heights: list[np.ndarray], cycles, lookup, bam_crc: int,
               params: int, u16: bool = True) -> bytes:
  nf, nc = len(frames), len(cycles)
  ofr = HDR.size
  ocyc = ofr + nf * FRM.size
  olut = ocyc + nc * 4
  odata = olut + len(lookup) * 2
  table, blobs, pos = [], [], odata
  vmax = 65535 if u16 else 255
  for f, h in zip(frames, heights):
    hm = float(h.max()) if h.size else 0.0
    if hm <= 0:
      table.append(FRM.pack(f.w, f.h, f.cx, f.cy, 0.0, 0, 0))
      continue
    scale = hm / vmax
    q = np.clip(np.rint(h / scale), 0, vmax).astype("<u2" if u16 else np.uint8)
    q[h <= 0] = 0
    blob = zlib.compress(q.tobytes(), 9)
    table.append(FRM.pack(f.w, f.h, f.cx, f.cy, scale, pos, len(blob)))
    blobs.append(blob)
    pos += len(blob)
  out = HDR.pack(b"SPD1", VERSION, FLAG_U16 if u16 else 0, nf, nc, ofr, ocyc, olut, bam_crc, params)
  out += b"".join(table)
  out += b"".join(struct.pack("<HH", c, i) for c, i in cycles)
  out += struct.pack(f"<{len(lookup)}H", *lookup) if lookup else b""
  return out + b"".join(blobs)


@dataclass
class Dep:
  version: int
  u16: bool
  bam_crc: int
  params: int
  frames: list[tuple[int, int, int, int, float]]
  heights: list[np.ndarray]  # float32 px
  cycles: list[tuple[int, int]]
  lookup: list[int]


def decode_dep(b: bytes) -> Dep:
  magic, ver, flags, nf, nc, ofr, ocyc, olut, bcrc, params = HDR.unpack_from(b, 0)
  if magic != b"SPD1":
    raise ValueError("not SPD1")
  u16 = bool(flags & FLAG_U16)
  frames, heights = [], []
  for i in range(nf):
    w, h, cx, cy, scale, off, cs = FRM.unpack_from(b, ofr + i * FRM.size)
    frames.append((w, h, cx, cy, scale))
    if cs == 0:
      heights.append(np.zeros((h, w), np.float32))
    else:
      q = np.frombuffer(zlib.decompress(b[off:off + cs]), "<u2" if u16 else np.uint8).reshape(h, w)
      heights.append(q.astype(np.float32) * np.float32(scale))
  cycles = [struct.unpack_from("<HH", b, ocyc + i * 4) for i in range(nc)]
  nl = max((c + f for c, f in cycles), default=0)
  lookup = list(struct.unpack_from(f"<{nl}H", b, olut)) if nl else []
  return Dep(ver, u16, bcrc, params, frames, heights, cycles, lookup)


# ---------------------------------------------------------------- model

class DepthModel:
  def __init__(self, name: str = MODEL, batch: int = 16):
    os.environ["HF_HOME"] = HF_HOME
    os.environ.setdefault("HF_HUB_OFFLINE", "1")
    os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")
    import torch
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation
    self.torch = torch
    dev = next((i for i in range(torch.cuda.device_count()) if "4060" in torch.cuda.get_device_name(i)), None)
    if dev is None:
      raise RuntimeError("no RTX 4060 Ti among the CUDA devices")
    self.device = torch.device(f"cuda:{dev}")
    proc = AutoImageProcessor.from_pretrained(name)
    mean = np.array(getattr(proc, "image_mean", [0.485, 0.456, 0.406]), np.float32)
    std = np.array(getattr(proc, "image_std", [0.229, 0.224, 0.225]), np.float32)
    self.mean = torch.tensor(mean, device=self.device).view(1, 3, 1, 1).half()
    self.std = torch.tensor(std, device=self.device).view(1, 3, 1, 1).half()
    self.model = AutoModelForDepthEstimation.from_pretrained(name, use_safetensors=True).to(self.device).eval().half()
    self.batch = batch
    self.name = name

  def prepare(self, f: Frame, pal: np.ndarray) -> tuple[np.ndarray, tuple[int, int, int]]:
    """RGB float [S, S, 3] with the frame centred on grey padding; (S, y0, x0)."""
    m = surface_mask(f.idx)
    side = max(f.w, f.h)
    pad = max(8, int(round(side * PAD_FRAC)))
    S = side + 2 * pad
    img = np.full((S, S, 3), BG_GREY, np.float32)
    y0, x0 = (S - f.h) // 2, (S - f.w) // 2
    rgb = pal[f.idx].astype(np.float32) / 255.0
    img[y0:y0 + f.h, x0:x0 + f.w] = np.where(m[..., None], rgb, BG_GREY)
    return img, (S, y0, x0)

  def infer(self, items: list[tuple[np.ndarray, tuple[int, int, int], Frame]]) -> list[np.ndarray]:
    """Disparity (larger = closer) at frame resolution for each prepared frame."""
    torch = self.torch
    F = torch.nn.functional
    out = []
    for i in range(0, len(items), self.batch):
      chunk = items[i:i + self.batch]
      ts = []
      for img, _, _ in chunk:
        t = torch.from_numpy(img).to(self.device).permute(2, 0, 1)[None].half()
        ts.append(F.interpolate(t, size=(DEPTH_INPUT, DEPTH_INPUT), mode="bicubic", align_corners=False))
      x = (torch.cat(ts).clamp(0, 1) - self.mean) / self.std
      with torch.no_grad():
        pred = self.model(pixel_values=x).predicted_depth
      if pred.ndim == 3:
        pred = pred[:, None]
      pred = pred.float()
      for j, (_, (S, y0, x0), f) in enumerate(chunk):
        p = F.interpolate(pred[j:j + 1], size=(S, S), mode="bilinear", align_corners=False)[0, 0]
        out.append(p[y0:y0 + f.h, x0:x0 + f.w].cpu().numpy())
    return out


# ---------------------------------------------------------------- per BAM

@dataclass
class BamResult:
  heights: list[np.ndarray]
  masks: list[np.ndarray]
  r: float
  flick: dict = field(default_factory=dict)  # name -> (sum, n)
  raw_model: list = field(default_factory=list)


def bam_heights(bam: Bam, model: DepthModel | None, weight: float = MODEL_WEIGHT,
                keep_raw: bool = False) -> BamResult:
  frames = bam.frames
  masks = [surface_mask(f.idx) for f in frames]
  dists = [edge_distance(m, body_mask(f.idx)) for m, f in zip(masks, frames)]
  r = body_radius(dists)
  domes = [dome(d, r) for d in dists]
  details: list[np.ndarray | None] = [None] * len(frames)
  fitted: list[np.ndarray | None] = [None] * len(frames)
  if model is not None:
    todo = [i for i, m in enumerate(masks) if m.sum() >= MIN_MODEL_PIXELS]
    preps = [(*model.prepare(frames[i], bam.palette), frames[i]) for i in todo]
    preds = model.infer(preps)
    for i, p in zip(todo, preds):
      fm = fit_to_dome(p, domes[i], masks[i])
      fitted[i] = fm
      details[i] = np.where(masks[i], fm - domes[i], 0.0).astype(np.float32)
  orders = [bam.cycle_frames(c) for c in range(len(bam.cycles))]
  sm = list(details)
  done: set[int] = set()
  for order in orders:  # each frame smoothed within the first cycle that uses it
    sm2 = smooth_detail(details, masks, dists, frames, order)
    for fi in order:
      if fi not in done:
        sm[fi] = sm2[fi]
        done.add(fi)
  heights = []
  for i in range(len(frames)):
    det = sm[i] if sm[i] is not None else np.zeros_like(domes[i])
    heights.append(combine(domes[i], det, dists[i], masks[i], r, weight))
  res = BamResult(heights, masks, r)
  raw = [combine(domes[i], details[i] if details[i] is not None else np.zeros_like(domes[i]), dists[i], masks[i], r, 1.0)
         for i in range(len(frames))]
  for name, hs, st in (("final", heights, True), ("dome", domes, True), ("model_raw", raw, True),
                        ("final_all", heights, False)):
    s, n = 0.0, 0
    for order in orders:
      a, b = flicker(hs, masks, frames, order, st)
      s, n = s + a, n + b
    res.flick[name] = (s, n)
  if keep_raw:
    res.raw_model = raw
  return res


def out_path(name: str) -> str:
  return os.path.join(OUT_DIR, f"{name}.DEP")


def up_to_date(name: str, crc: int) -> bool:
  p = out_path(name)
  if not os.path.exists(p):
    return False
  try:
    hdr = HDR.unpack_from(open(p, "rb").read(HDR.size), 0)
  except struct.error:
    return False
  return hdr[0] == b"SPD1" and hdr[8] == crc and hdr[9] == params_tag()


def run(args) -> None:
  names = read_list()
  if args.only:
    rx = re.compile(args.only, re.I)
    names = [n for n in names if rx.search(n)]
  if args.limit:
    names = names[:args.limit]
  os.makedirs(OUT_DIR, exist_ok=True)
  model = None if args.dome_only else DepthModel(batch=args.batch)
  t0 = time.time()
  tot = {"final": [0.0, 0], "dome": [0.0, 0], "model_raw": [0.0, 0], "final_all": [0.0, 0]}
  nb = nfr = size = skipped = 0
  log = open(os.path.join(OUT_DIR, "run.log"), "a", encoding="utf-8")
  log.write(f"--- {time.strftime('%Y-%m-%d %H:%M:%S')} {len(names)} BAMs model={'dome' if model is None else MODEL}\n")
  for k, name in enumerate(names):
    bam, raw = load_bam(name)
    crc = zlib.crc32(raw)
    if not args.force and up_to_date(name, crc):
      skipped += 1
      continue
    t1 = time.time()
    res = bam_heights(bam, model)
    blob = encode_dep(bam.frames, res.heights, bam.cycles, bam.lookup, crc, params_tag() if model else 0)
    tmp = out_path(name) + ".tmp"
    open(tmp, "wb").write(blob)
    os.replace(tmp, out_path(name))
    nb += 1
    nfr += len(bam.frames)
    size += len(blob)
    for key, (s, n) in res.flick.items():
      tot[key][0] += s
      tot[key][1] += n
    fl = {k: (s / n / res.r if n else 0.0) for k, (s, n) in res.flick.items()}
    line = (f"{name} frames={len(bam.frames)} cycles={len(bam.cycles)} r={res.r:.1f} bytes={len(blob)} "
            f"t={time.time() - t1:.1f}s flicker/r static: final={fl['final']:.4f} dome={fl['dome']:.4f} raw={fl['model_raw']:.4f} "
            f"all pixels: final={fl['final_all']:.4f}")
    log.write(line + "\n")
    log.flush()
    print(f"[{k + 1}/{len(names)}] {line}", flush=True)
  dt = time.time() - t0
  summ = (f"done: {nb} BAMs written ({skipped} up to date), {nfr} frames, {size / 1e6:.1f} MB, {dt / 60:.1f} min; "
          + " ".join(f"flicker_{k}={s / n:.3f}px" for k, (s, n) in tot.items() if n))
  log.write(summ + "\n")
  print(summ)


# ---------------------------------------------------------------- review

def shade(rgb: np.ndarray, h: np.ndarray, mask: np.ndarray) -> np.ndarray:
  """Lambert from the height's normals (x right, y down the screen, z toward the camera), light from
  the upper left, as a ratio against a flat sprite (the plan's art-style guard)."""
  gy, gx = np.gradient(h.astype(np.float64))
  n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
  n /= np.linalg.norm(n, axis=-1, keepdims=True)
  L = np.array([-0.55, -0.55, 0.63])
  L /= np.linalg.norm(L)
  lam = np.clip((n * L).sum(-1), 0, 1) / L[2]
  lit = np.clip(rgb.astype(np.float64) * (0.35 + 0.65 * lam)[..., None], 0, 255)
  return np.where(mask[..., None], lit, 0).astype(np.uint8)


def review(names: list[str], out: str, cycle: int | None = None, debug: bool = False) -> list[str]:
  from PIL import Image
  model = DepthModel()
  os.makedirs(out, exist_ok=True)
  paths = []
  for name in names:
    bam, _ = load_bam(name)
    res = bam_heights(bam, model, keep_raw=debug)
    c = cycle if cycle is not None else 0
    order = bam.cycle_frames(c)[:8]
    rows = []
    for fi in order:
      f = bam.frames[fi]
      m = res.masks[fi]
      rgb = bam.palette[f.idx]
      rgb = np.where((f.idx == 0)[..., None], 0, rgb)
      h = res.heights[fi]
      dv = (np.clip(h / max(res.r * 1.2, 1e-6), 0, 1) * 255).astype(np.uint8)
      dimg = np.where(m[..., None], np.stack([dv] * 3, -1), np.array([40, 0, 40], np.uint8))
      sh = shade(bam.palette[f.idx], h, m)
      sep = np.full((f.h, 4, 3), 255, np.uint8)
      panels = [rgb.astype(np.uint8), sep, dimg, sep, sh]
      if debug:  # model fitted to the dome, full weight, no temporal smoothing
        rv = (np.clip(res.raw_model[fi] / max(res.r * 1.2, 1e-6), 0, 1) * 255).astype(np.uint8)
        panels += [sep, np.where(m[..., None], np.stack([rv] * 3, -1), np.array([40, 0, 40], np.uint8)),
                   sep, shade(bam.palette[f.idx], res.raw_model[fi], m)]
      rows.append(np.concatenate(panels, 1))
    W = max(r.shape[1] for r in rows)
    sheet = np.concatenate([np.pad(r, ((0, 4), (0, W - r.shape[1]), (0, 0))) for r in rows], 0)
    img = Image.fromarray(sheet).resize((sheet.shape[1] * 2, sheet.shape[0] * 2), Image.NEAREST)
    p = os.path.join(out, f"{name}_c{c}.png")
    img.save(p)
    paths.append(p)
    fl = {k: (s / n / res.r if n else 0.0) for k, (s, n) in res.flick.items()}
    print(f"{p}  r={res.r:.1f} flicker/r " + " ".join(f"{k}={v:.4f}" for k, v in fl.items()))
  return paths


# ---------------------------------------------------------------- cli

def main(argv: list[str]) -> None:
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  sub = ap.add_subparsers(dest="cmd", required=True)
  sub.add_parser("list")
  r = sub.add_parser("run")
  r.add_argument("--only")
  r.add_argument("--limit", type=int, default=0)
  r.add_argument("--force", action="store_true")
  r.add_argument("--dome-only", action="store_true")
  r.add_argument("--batch", type=int, default=16)
  v = sub.add_parser("review")
  v.add_argument("bams", nargs="+")
  v.add_argument("--out", default=os.path.join(OUT_DIR, "review"))
  v.add_argument("--cycle", type=int)
  v.add_argument("--debug", action="store_true", help="add the raw (fitted, unsmoothed) model panels")
  sub.add_parser("stats")
  i = sub.add_parser("info")
  i.add_argument("bam")
  a = ap.parse_args(argv)
  if a.cmd == "list":
    rows = write_list()
    print(f"{len(rows)} BAMs -> {LIST_FILE}")
  elif a.cmd == "run":
    run(a)
  elif a.cmd == "review":
    review([b.upper() for b in a.bams], a.out, a.cycle, a.debug)
  elif a.cmd == "stats":
    files = [f for f in os.listdir(OUT_DIR) if f.upper().endswith(".DEP")] if os.path.isdir(OUT_DIR) else []
    nf = sz = 0
    for f in files:
      b = open(os.path.join(OUT_DIR, f), "rb").read()
      nf += HDR.unpack_from(b, 0)[3]
      sz += len(b)
    print(f"{len(files)} .DEP files of {len(read_list())} listed, {nf} frames, {sz / 1e6:.1f} MB")
  elif a.cmd == "info":
    bam, raw = load_bam(a.bam)
    print(f"{a.bam}.BAM {len(raw)} bytes, {len(bam.frames)} frames, {len(bam.cycles)} cycles "
          f"{[c for c, _ in bam.cycles]}, sizes {sorted({(f.w, f.h) for f in bam.frames})[:6]}...")
    if os.path.exists(out_path(a.bam)):
      d = decode_dep(open(out_path(a.bam), "rb").read())
      print(f".DEP v{d.version} u16={d.u16} frames={len(d.frames)} max h={max(float(h.max()) for h in d.heights):.1f}px "
            f"up_to_date={up_to_date(a.bam, zlib.crc32(raw))}")


if __name__ == "__main__":
  main(sys.argv[1:])
