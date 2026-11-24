# SPDX-License-Identifier: GPL-2.0-or-later
"""Dialogue (DLG) stat checks: which replies a CheckStat/CheckStatGT on an ability hides.

    python -I tools/dlg_checks.py dump DMORTE [REGEX]   states whose replies match REGEX (default CheckStat)
    python -I tools/dlg_checks.py scan [--start]        every DLG with ability-checked replies
                                                         (--start: only in states that can open a dialogue,
                                                         i.e. that carry a state trigger)

Reads override first, then chitin.key/BIFs (tools/ieres.py). DLG V1.0 layout: IESDP file_formats/dlg_v1.htm.
"""
import os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ieres  # noqa: E402

ABIL = re.compile(r"CheckStat(GT)?\(\s*([^,]+),\s*(-?\d+)\s*,\s*(STR|INT|WIS|DEX|CON|CHR)\s*\)", re.I)


def parse(d):
  ns, os_, nt, ot, ost, nst, ott, ntt = struct.unpack_from("<8I", d, 8)

  def trig(i, off, cnt):
    if i >= cnt: return ""
    o, ln = struct.unpack_from("<II", d, off + i * 8)
    return d[o:o + ln].decode("latin1").strip()

  states = []
  for si in range(ns):
    sr, ft, ntr, st = struct.unpack_from("<iIIi", d, os_ + si * 16)
    replies = []
    for ti in range(ft, ft + ntr):
      fl, tx, _j, tr, _a, nd, nx = struct.unpack_from("<IiiII8sI", d, ot + ti * 32)
      replies.append({"i": ti - ft, "flags": fl, "text": tx if fl & 1 else -1,
                      "cond": trig(tr, ott, ntt) if fl & 2 else "",
                      "next": nd.rstrip(b"\0").decode("latin1"), "state": nx})
    states.append({"i": si, "text": sr, "trigger": trig(st, ost, nst) if st >= 0 else None, "replies": replies})
  return states


def dump(name, rx):
  d = ieres.read(name, "DLG")
  if not d: sys.exit("not found")
  p = re.compile(rx, re.I)
  for s in parse(d):
    if not any(p.search(r["cond"]) for r in s["replies"]): continue
    trig = (" [start: " + s["trigger"].replace("\n", " ; ") + "]") if s["trigger"] is not None else ""
    print(f"== state {s['i']}{trig}: {ieres.tlk(s['text'])[:200]}")
    for r in s["replies"]:
      t = ieres.tlk(r["text"])[:120] if r["text"] >= 0 else "[no text]"
      c = r["cond"].replace("\n", " ; ") or "(always)"
      nx = " -> end" if r["flags"] & 0x8 else f" -> {r['next'] or name}:{r['state']}"
      print(f"  {r['i']}: {t}{nx}\n       IF {c}")


def scan(start_only):
  ieres._key = ieres._key or ieres.load_key()
  names = sorted({n for n, e in ieres._key if e == "DLG"})
  for n in names:
    d = ieres.read(n, "DLG")
    if not d or d[:8] != b"DLG V1.0": continue
    hits = []
    for s in parse(d):
      if start_only and s["trigger"] is None: continue
      for r in s["replies"]:
        for m in ABIL.finditer(r["cond"]):
          hits.append(f"s{s['i']}r{r['i']} {m.group(0)}")
    if hits: print(f"{n}: {len(hits)}  " + "  ".join(hits[:4]))


if __name__ == "__main__":
  a = sys.argv[1:]
  if not a: sys.exit(__doc__)
  if a[0] == "dump": dump(a[1], a[2] if len(a) > 2 else "CheckStat")
  elif a[0] == "scan": scan("--start" in a)
  else: sys.exit(__doc__)
