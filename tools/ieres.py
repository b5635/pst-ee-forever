# SPDX-License-Identifier: GPL-2.0-or-later
"""Read PST:EE game resources (override first, then chitin.key/BIFs) and dump SPL/ITM effects.

  python -I tools/ieres.py spl SPWI105        effects of a spell, per ability header
  python -I tools/ieres.py itm WHISPFLK       same for an item
  python -I tools/ieres.py eff NAME | raw NAME.EXT [OUT]
  python -I tools/ieres.py find REGEX [EXT]   resource names matching
  python -I tools/ieres.py name SPWI105       TLK name of a spell/item
  python -I tools/ieres.py desc SPWI105       TLK description of a spell/item (what it should do)
"""
import os, re, struct, sys

GAME = r"C:\GOG Games\Planescape Torment - Enhanced Edition"
# KEY resource type codes (IESDP file_formats/general.htm).
TYPES = {0x001: "BMP", 0x004: "WAV", 0x3e8: "BAM", 0x3e9: "WED", 0x3ea: "CHU", 0x3eb: "TIS", 0x3ec: "MOS",
         0x3ed: "ITM", 0x3ee: "SPL", 0x3ef: "BCS", 0x3f0: "IDS", 0x3f1: "CRE", 0x3f2: "ARE", 0x3f3: "DLG",
         0x3f4: "2DA", 0x3f5: "GAM", 0x3f6: "STO", 0x3f7: "WMP", 0x3f8: "EFF", 0x3fb: "VVC", 0x3fc: "VEF",
         0x3fd: "PRO", 0x404: "PVRZ", 0x405: "GLSL", 0x408: "MENU", 0x409: "LUA", 0x40a: "TTF"}
TIMING = {0: "dur", 1: "perm", 2: "equip", 3: "delay", 4: "delay->perm", 5: "delay->equip", 6: "delay2",
          7: "delay2->perm", 8: "delay2->equip", 9: "permAfterDeath", 10: "ticks"}


def load_key():
  d = open(os.path.join(GAME, "chitin.key"), "rb").read()
  nbif, nres, obif, ores = struct.unpack_from("<4I", d, 8)
  bifs = []
  for i in range(nbif):
    ln, on, nl, loc = struct.unpack_from("<IIHH", d, obif + i * 12)
    bifs.append(d[on:on + nl].rstrip(b"\0").decode().replace("\\", "/"))
  res = {}
  for i in range(nres):
    o = ores + i * 14
    name = d[o:o + 8].rstrip(b"\0").decode("latin1").upper()
    t, loc = struct.unpack_from("<HI", d, o + 8)
    res[(name, TYPES.get(t, hex(t)))] = (bifs[loc >> 20], loc & 0x3FFF)
  return res


_key = None
_bifs = {}
_override = None


def read(name, ext):
  name, ext = name.upper(), ext.upper()
  global _key, _override
  if _override is None: _override = {f.upper(): f for f in os.listdir(os.path.join(GAME, "override"))}
  f = _override.get(f"{name}.{ext}")
  if f: return open(os.path.join(GAME, "override", f), "rb").read()
  _key = _key or load_key()
  if (name, ext) not in _key: return None
  bif, idx = _key[(name, ext)]
  if bif not in _bifs: _bifs[bif] = open(os.path.join(GAME, bif), "rb").read()
  b = _bifs[bif]
  n, _, oe = struct.unpack_from("<3I", b, 8)
  for i in range(n):
    loc, off, size, _t, _u = struct.unpack_from("<IIIHH", b, oe + i * 16)
    if loc & 0x3FFF == idx: return b[off:off + size]
  return None


_tlk = None


def tlk(ref):
  global _tlk
  if _tlk is None: _tlk = open(os.path.join(GAME, "lang", "en_US", "dialog.tlk"), "rb").read()
  if ref < 0 or ref == 0xFFFFFFFF: return ""
  n, so = struct.unpack_from("<IxxI", _tlk, 10)[0], struct.unpack_from("<I", _tlk, 14)[0]
  if ref >= n: return ""
  e = 18 + ref * 26
  off, ln = struct.unpack_from("<II", _tlk, e + 18)
  return _tlk[so + off:so + off + ln].decode("latin1")


def effect(d, o):
  (op, tgt, pw, p1, p2, tim, res_, dur, pr1, pr2) = struct.unpack_from("<HBBIIBBIBB", d, o)
  r = d[o + 20:o + 28].rstrip(b"\0").decode("latin1")
  dn, ds, st, sb, sp = struct.unpack_from("<IIIiI", d, o + 28)
  s = f"op{op:<3} tgt{tgt} p1={p1} p2={p2} {TIMING.get(tim, tim)} dur={dur}"
  if r: s += f" res={r}"
  if dn or ds: s += f" dice={dn}d{ds}"
  if st: s += f" save=0x{st:x}{sb:+d}"
  if pr1 != 100 or pr2: s += f" prob={pr2}-{pr1}"
  if sp: s += f" special={sp}"
  return s


def dump(name, ext):
  d = read(name, ext)
  if d is None: sys.exit(f"{name}.{ext} not found")
  print(f"{name}.{ext} {d[:8]!r} name: {tlk(struct.unpack_from('<I', d, 0x0C if ext == 'ITM' else 0x08)[0])}")
  oext, next_, ofeat, ocast, ncast = struct.unpack_from("<IHIHH", d, 0x64)
  ehs = 0x38 if ext == "ITM" else 0x28
  for i in range(ncast):
    print("  global:", effect(d, ofeat + (ocast + i) * 0x30))
  for h in range(next_):
    o = oext + h * ehs
    n, first = struct.unpack_from("<HH", d, o + 0x1E)
    proj = struct.unpack_from("<H", d, o + (0x2A if ext == "ITM" else 0x26))[0]
    lvl = struct.unpack_from("<H", d, o + 0x10)[0] if ext == "SPL" else 0
    rng = struct.unpack_from("<H", d, o + 0x0E)[0]
    tgt = d[o + 0x0C]
    ct = struct.unpack_from("<H", d, o + 0x12)[0] if ext == "SPL" else d[o + 0x12 - 0]  # casting time / speed
    print(f" header {h}: type {d[o]} target {tgt} range {rng} level {lvl} speed {ct} projectile {proj}, {n} effects")
    for i in range(n):
      print("   ", effect(d, ofeat + (first + i) * 0x30))


def main(a):
  global _key
  if not a: sys.exit(__doc__)
  c = a[0]
  if c in ("spl", "itm"): dump(a[1], c.upper())
  elif c == "eff":
    d = read(a[1], "EFF")
    if d is None: sys.exit("not found")
    op, tgt, pw, p1, p2 = struct.unpack_from("<IIIII", d, 0x10)
    tim, dur = struct.unpack_from("<HI", d, 0x24)[0], struct.unpack_from("<I", d, 0x28)[0]
    r = d[0x94:0x9C].rstrip(b"\0").decode("latin1")
    dn, ds, st, sb = struct.unpack_from("<IIIi", d, 0x40)
    print(f"{a[1]}.EFF op{op} tgt{tgt} p1={p1} p2={p2} {TIMING.get(tim, tim)} dur={dur} res={r} dice={dn}d{ds} save=0x{st:x}{sb:+d}")
  elif c == "raw":
    n, e = a[1].split(".")
    d = read(n, e)
    if d is None: sys.exit(f"{a[1]} not found")
    if len(a) > 2: open(a[2], "wb").write(d)
    else: print(len(d) if d else "not found")
  elif c == "find":

    _key = _key or load_key()
    names = set(_key) | {tuple(f.upper().rsplit(".", 1)) for f in os.listdir(os.path.join(GAME, "override")) if "." in f}
    rx = re.compile(a[1], re.I)
    for n, e in sorted(names):
      if rx.search(n) and (len(a) < 3 or e == a[2].upper()): print(f"{n}.{e}")
  elif c == "named":  # named REGEX: SPL/ITM whose TLK name matches
    _key = _key or load_key()
    rx = re.compile(a[1], re.I)
    names = {k for k in _key if k[1] in ("SPL", "ITM")}
    names |= {tuple(f.upper().rsplit(".", 1)) for f in os.listdir(os.path.join(GAME, "override"))
              if f.upper().endswith((".SPL", ".ITM"))}
    for n, e in sorted(names):
      d = read(n, e)
      if not d or len(d) < 0x72: continue
      nm = tlk(struct.unpack_from("<I", d, 0x0C if e == "ITM" else 0x08)[0])
      if rx.search(nm): print(f"{n}.{e}  {nm}")
  elif c == "name":
    for ext in ("SPL", "ITM"):
      d = read(a[1], ext)
      if d: print(ext, tlk(struct.unpack_from("<I", d, 0x0C if ext == "ITM" else 0x08)[0]))
  elif c == "desc":  # SPL description +0x50; ITM identified description +0x54
    for ext in ("SPL", "ITM"):
      d = read(a[1], ext)
      if d: print(ext, tlk(struct.unpack_from("<I", d, 0x54 if ext == "ITM" else 0x50)[0]))


if __name__ == "__main__":
  main(sys.argv[1:])
