# SPDX-License-Identifier: GPL-2.0-or-later
"""8-member party: look for other 6-slot arrays in the functions that touch the party arrays.

  python -I tools/party_scan.py            flagged functions only
  python -I tools/party_scan.py --all      every function, flagged or not

The functions are those holding a retargeted site or a bound patch (src/engine/party_sites.h,
tools/party_patches.tsv). A loop that now runs to 8 may also index something else that still has 6
entries (CInfGame::Marshal's stack array of CGameSaveCharacter, the PORTRT0..5 resources). Per function,
from its Linux twin's pseudocode (named; the Windows body when there is no twin), it lists:
  array    local arrays whose length is a multiple of 6 (or of 6 x a common record size)
  fmt      "...%d..." format strings (per-slot resource or file names: PORTRT%d)
  stride   constant multiples of 6 records (6*N byte memsets, frame offsets)
Review every hit by hand; the scan only narrows the reading.
"""
import re
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DB = ROOT / "resources" / "re" / "index.sqlite"
SITES = ROOT / "src" / "engine" / "party_sites.h"
PATCHES = ROOT / "tools" / "party_patches.tsv"
BASE = 0x140000000

DECL = re.compile(r"^\s*([\w :*]+?)\s+\**(\w+)\[(\d+)\];", re.M)
FMT = re.compile(r'"([^"\n]*%d[^"\n]*)"')
NUM = re.compile(r"\b(0x[0-9A-Fa-f]+|\d+)(?:u?LL|u)?\b")


def function_eas(db):
    vas = set()
    for m in re.finditer(r"\{0x([0-9A-F]+), \d+, \d+, 0x", SITES.read_text()):
        vas.add(BASE + int(m.group(1), 16))
    for line in PATCHES.read_text().splitlines():
        if line and not line.startswith("#"):
            vas.add(int(line.split("\t")[0], 16))
    funcs = db.execute("select ea, size, name from func where bin='win' order by ea").fetchall()
    starts = [f[0] for f in funcs]
    import bisect
    out = {}
    for va in vas:
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and va < funcs[i][0] + max(funcs[i][1], 1) + 16:
            out[funcs[i][0]] = funcs[i][2]
    return out


def flags(body):
    hits = []
    for m in DECL.finditer(body):
        n = int(m.group(3))
        if n >= 6 and n % 6 == 0 and n <= 0x10000:
            hits.append("array %s %s[%d]" % (m.group(1).strip(), m.group(2), n))
    for m in FMT.finditer(body):
        hits.append('fmt "%s"' % m.group(1))
    for m in re.finditer(r"memset\([^;]*?,\s*(0x[0-9A-Fa-f]+|\d+)(?:u?LL)?\)", body):
        n = int(m.group(1), 0)
        if n >= 0x30 and n % 6 == 0:
            hits.append("stride memset %s (= 6 x 0x%X)" % (m.group(1), n // 6))
    return hits


def main():
    db = sqlite3.connect(DB)
    shown = 0
    for ea, name in sorted(function_eas(db).items()):
        row = db.execute("select lin from xmap where win=?", (ea,)).fetchone()
        body, src = None, "win"
        if row:
            r = db.execute("select name, body from code where bin='lin' and ea=?", (row[0],)).fetchone()
            if r:
                body, src = r[1], "lin %X %s" % (row[0], r[0])
        if body is None:
            r = db.execute("select body from code where bin='win' and ea=?", (ea,)).fetchone()
            body = r[0] if r else ""
        hits = flags(body)
        if hits or "--all" in sys.argv:
            shown += 1
            print("%X %s  [%s]" % (ea, name, src))
            for h in sorted(set(hits)):
                print("    " + h)
    print("%d functions shown" % shown)


if __name__ == "__main__":
    main()
