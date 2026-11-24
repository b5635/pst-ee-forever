# SPDX-License-Identifier: GPL-2.0-or-later
"""Where Torment.exe (Windows, 3.2.2) touches the 6-slot party arrays of CInfGame (8-member party work).

  python -I tools/party_refs.py            summary: instructions and functions per kind
  python -I tools/party_refs.py --list     every instruction (VA, function, text)
  python -I tools/party_refs.py --bounds   functions that index the arrays and also compare with 5/6

CInfGame +0xA1E8 i32[6] fixed-order slots, +0xA200 i32[6] party order, +0xA218 i16 count
(lin CInfGame::AddCharacterToParty 375A70: this+10362.., this+10368.., this+20748 words).
Kinds: 'indexed' = [base+idx*4+disp] (retarget the disp32), 'fixed' = a fixed element (unrolled code:
the function needs rewriting for 8), 'count' = the i16 count (stays where it is).
Reads resources/re/win/dump/all_Torment.asm line by line (never load it whole).
"""
import collections
import re
import sys
from pathlib import Path

ASM = Path(__file__).resolve().parent.parent / "resources" / "re" / "win" / "dump" / "all_Torment.asm"
ARR = re.compile(r"\b0A(1E8|1EC|1F0|1F4|1F8|1FC|200|204|208|20C|210|214)h\b")
CNT = re.compile(r"\b0A218h\b")
IDX = re.compile(r"\*4\+0A(1E8|200)h\]")
BOUND = re.compile(r"\bcmp\s+[^,]+,\s*(5|6)\b")


def scan():
    fn = None
    rows = []
    bounds = collections.defaultdict(list)
    for line in open(ASM, encoding="latin-1"):
        if line.startswith("//@@"):
            fn = line[5:].strip()
            continue
        if BOUND.search(line):
            bounds[fn].append(line.strip()[:80])
        kind = None
        if ARR.search(line):
            kind = "indexed" if IDX.search(line) else ("fixed" if not re.search(r"\+0A(1E8|200)h\]", line) else "first")
        elif CNT.search(line):
            kind = "count"
        if kind:
            rows.append((kind, fn, line.strip()[:110]))
    return rows, bounds


def main(argv):
    rows, bounds = scan()
    if "--list" in argv:
        for k, f, l in rows:
            print(f"{k:8} {f} | {l}")
        return
    if "--bounds" in argv:
        indexed = {f for k, f, _ in rows if k in ("indexed", "first", "fixed")}
        for f in sorted(indexed):
            if bounds.get(f):
                print(f, bounds[f][:6])
        return
    per = collections.defaultdict(set)
    n = collections.Counter()
    for k, f, _ in rows:
        per[k].add(f)
        n[k] += 1
    for k in ("indexed", "first", "fixed", "count"):
        print(f"{k:8} {n[k]:5} instructions in {len(per[k]):4} functions")
    print("fixed (unrolled) functions:", sorted(per["fixed"]))


if __name__ == "__main__":
    main(sys.argv[1:])
