# SPDX-License-Identifier: GPL-2.0-or-later
r"""Everything needed to vet one spell by hand, on one screen.

  python -I tools/spell_vet.py SPWI105 [SPPR201 ...]
  python -I tools/spell_vet.py --log [LOG]   per spell of the last spell_suite run: the cast line, the effects on the
                                              measured creature before and after (op:type:ticks left), rolls, result

Per spell: its description (the rules paragraphs), the ability header the suite caster uses (mage level 20,
priest 14) and the global effects, its row in docs/SPELL_TEST_PLAN.md, its last measurement in
tests/unit/spell_results.h, and whether tests/unit/spell_expect.h has it (vetted).
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ieres  # noqa: E402

CASTER_LEVEL = {"SPWI": 20, "SPPR": 14, "SPIN": 20}


def header_for(d, level):
  """Index of the last ability header whose level the caster has (the engine's pick)."""
  oext, next_ = struct.unpack_from("<IH", d, 0x64)
  best = 0
  for h in range(next_):
    if struct.unpack_from("<H", d, oext + h * 0x28 + 0x10)[0] <= level: best = h
  return best, next_


def effects(d, h):
  oext, next_, ofeat, ocast, ncast = struct.unpack_from("<IHIHH", d, 0x64)
  out = ["global: " + ieres.effect(d, ofeat + (ocast + i) * 0x30) for i in range(ncast)]
  o = oext + h * 0x28
  n, first = struct.unpack_from("<HH", d, o + 0x1E)
  proj, tgt, rng = struct.unpack_from("<H", d, o + 0x26)[0], d[o + 0x0C], struct.unpack_from("<H", d, o + 0x0E)[0]
  speed = struct.unpack_from("<H", d, o + 0x12)[0]
  out.insert(0, f"header {h}: target {tgt} range {rng} speed {speed} projectile {proj}")
  out += ["  " + ieres.effect(d, ofeat + (first + i) * 0x30) for i in range(n)]
  return out


def grep(path, res):
  if not os.path.exists(path): return []
  return [l.rstrip() for l in open(path, encoding="utf-8") if re.search(r'\b' + res + r'\b', l)]


LOG = r"C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs\pst_forever.log"


def from_log(path):
  lines = open(path, encoding="latin1").read().splitlines()
  cur = None
  for l in lines:
    m = re.search(r"tbc_spells_all: (SP\w+) cast by", l)
    if m:
      cur = m.group(1)
      print(f"== {cur}: " + l.split("tbc_spells_all: ")[1][:160])
      continue
    if not cur: continue
    t = l[13:] if len(l) > 13 else l
    if re.search(r"PSTF_Effects|spell being cast|roll: .*saves|turn waits|ends turn \(End|first pulse|onset in|WARNING|faulted", t):
      print("   " + re.sub(r" hp:.*", "", t)[:300])
    m = re.search(r"tbc_spells_all: (SP\w+) (ok|fizzle|stall) (.*)", l)
    if m:
      print("   RESULT " + m.group(2) + " " + m.group(3)[:300])
      cur = None


def main(a):
  if not a: sys.exit(__doc__)
  if a[0] == "--log":
    from_log(a[1] if len(a) > 1 else LOG)
    return
  for res in a:
    res = res.upper()
    d = ieres.read(res, "SPL")
    if d is None:
      print(f"== {res}: no SPL"); continue
    name = ieres.tlk(struct.unpack_from("<I", d, 0x08)[0])
    desc = ieres.tlk(struct.unpack_from("<I", d, 0x50)[0]).strip()
    h, nh = header_for(d, CASTER_LEVEL.get(res[:4], 20))
    print(f"== {res} {name}  (header {h} of {nh})")
    paras = [p.strip() for p in desc.split("\n\n") if p.strip()]
    for p in paras[:1] + paras[2:] if len(paras) > 2 else paras:  # stat block + rules, not the flavour text
      print("   " + p.replace("\n", " | "))
    for line in effects(d, h): print("   " + line)
    for l in grep(os.path.join(ROOT, "docs", "SPELL_TEST_PLAN.md"), res): print("   plan: " + l[:400])
    for l in grep(os.path.join(ROOT, "tests", "unit", "spell_results.h"), res): print("   measured: " + l.strip())
    print("   vetted: " + ("yes" if grep(os.path.join(ROOT, "tests", "unit", "spell_expect.h"), res) else "no"))


if __name__ == "__main__":
  main(sys.argv[1:])
