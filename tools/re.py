# SPDX-License-Identifier: GPL-2.0-or-later
"""Search the PST:EE reverse-engineering dumps (3.2.2: Linux Torment = named reference, Windows Torment.exe x64 = hook target).

  python tools/re.py build                      build resources/re/index.sqlite from the dumps (once, ~1 min)
  python tools/re.py find  SetActiveAI          functions by name (regex), both binaries + Linux<->Windows map
  python tools/re.py code  CGameSprite::Swing   pseudocode (Linux, named); --win for the Windows twin; --lines 40-80
  python tools/re.py xref  CGameSprite::Swing   callers / callees (--win for the Windows call graph)
  python tools/re.py str   "SPWI912"            strings containing text and the functions using them
  python tools/re.py grep  "m_nAttacks\\b"      regex over all pseudocode (--win, --limit N)
  python tools/re.py asm   CGameSprite::Swing   disassembly (Windows by default: hook prologues; --lin for Linux)
  python tools/re.py data  g_pBaldurChitin      who reads/writes a global (regex); or `data FUNC --uses`: globals a function touches
  python tools/re.py addr  CGameSprite::Swing   Windows VA/RVA, first bytes, signature uniqueness (hook manifest)
  python tools/re.py stats
Name arguments accept a demangled name (prefix before "("), a regex (find), or a hex EA (0x... or bare).
"""
import argparse, os, re, sqlite3, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RE = os.path.join(ROOT, "resources", "re")
DB = os.path.join(RE, "index.sqlite")
LIN, WIN = os.path.join(RE, "lin", "dump"), os.path.join(RE, "win", "dump")
EXE = os.path.join(RE, "win", "Torment.exe")


def first(*paths):
    return next((p for p in paths if os.path.exists(p)), None)


def rows(path, n):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) >= n:
                yield p


def code_chunks(path):
    ea = name = None; buf = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("//@@ "):
                if ea is not None:
                    yield ea, name, "".join(buf)
                e, _, nm = line[5:].rstrip("\n").partition(" ")
                ea, name, buf = int(e, 16), nm, []
            else:
                buf.append(line)
    if ea is not None:
        yield ea, name, "".join(buf)


def build():
    if os.path.exists(DB):
        os.remove(DB)
    c = sqlite3.connect(DB)
    c.executescript("""
      create table func(bin text, ea int, size int, name text, base text);
      create table xmap(win int, lin int, method text, score real);
      create table calls(bin text, caller int, callee int, kind text);
      create table str(bin text, ea int, text text, refs text);
      create table code(bin text, ea int, name text, body text);
      create table vt(bin text, ea int, cls text, slots text);
      create table asm(bin text, ea int, name text, body text);
      create table dref(bin text, func int, target int, tname text, kind text);
      create table proto(bin text, ea int, proto text);
    """)
    for b, d in (("lin", LIN), ("win", WIN)):
        dp = os.path.join(d, "drefs.tsv")
        if os.path.exists(dp):
            c.executemany("insert into dref values(?,?,?,?,?)",
                          ((b, int(p[0], 16), int(p[2], 16), p[3], p[4]) for p in rows(dp, 5) if p[0] != "func_ea" and int(p[2], 16) < (1 << 63)))
        pp = os.path.join(d, "protos.tsv")
        if os.path.exists(pp):
            c.executemany("insert into proto values(?,?,?)", ((b, int(p[0], 16), p[1]) for p in rows(pp, 2)))
        ap = os.path.join(d, "all_Torment.asm")
        if os.path.exists(ap):
            c.executemany("insert into asm values(?,?,?,?)", ((b, ea, nm, body) for ea, nm, body in code_chunks(ap)))
        fp = os.path.join(d, "funcs.txt")
        if os.path.exists(fp):
            c.executemany("insert into func values(?,?,?,?,?)",
                          ((b, int(p[0], 16), int(p[1]), p[2], p[2].split("(")[0]) for p in rows(fp, 3)))
        sp = os.path.join(d, "strings.txt")
        if os.path.exists(sp):
            c.executemany("insert into str values(?,?,?,?)", ((b, int(p[0], 16), p[1], p[2] if len(p) > 2 else "")
                                                             for p in rows(sp, 2) if not p[1].startswith("_Z")))  # skip ELF symbol names
        cp = os.path.join(d, "calls.tsv")
        if os.path.exists(cp):
            c.executemany("insert into calls values(?,?,?,?)",
                          ((b, int(p[0], 16), int(p[2], 16), p[4]) for p in rows(cp, 5) if p[0] != "caller_ea"))
    for b, p in (("lin", os.path.join(LIN, "all_Torment.c")),
                 ("win", os.path.join(WIN, "all_Torment.c"))):
        if p and os.path.exists(p):
            c.executemany("insert into code values(?,?,?,?)", ((b, ea, nm, body) for ea, nm, body in code_chunks(p)))
    nw = os.path.join(WIN, "names_win.tsv")
    if os.path.exists(nw):
        c.executemany("insert into xmap values(?,?,?,?)",
                      ((int(p[0], 16), int(p[3], 16), p[1], float(p[2])) for p in rows(nw, 6) if p[0] != "win_ea"))
    vw = os.path.join(WIN, "vtables_win.tsv")
    if os.path.exists(vw):
        c.executemany("insert into vt values(?,?,?,?)", (("win", int(p[0], 16), p[1], p[2]) for p in rows(vw, 3) if p[0] != "win_vtable_ea"))
    vl = os.path.join(LIN, "vtables.tsv")
    if os.path.exists(vl):
        c.executemany("insert into vt values(?,?,?,?)", (("lin", int(p[0], 16), p[1], p[2]) for p in rows(vl, 3)))
    c.executescript("""
      create index fb on func(base); create index fe on func(bin, ea); create index xw on xmap(win); create index xl on xmap(lin);
      create index cr on calls(bin, caller); create index ce on calls(bin, callee); create index ke on code(bin, ea);
      create index ae on asm(bin, ea); create index dt on dref(bin, tname); create index df on dref(bin, func); create index pe on proto(bin, ea);
    """)
    c.commit()
    for t in ("func", "xmap", "calls", "str", "code", "asm", "dref", "proto", "vt"):
        print(t, c.execute(f"select bin, count(*) from {t} group by 1" if t != "xmap" else "select 'map', count(*) from xmap").fetchall())


def db():
    if not os.path.exists(DB):
        sys.exit("no index: run  python tools/re.py build")
    return sqlite3.connect(DB)


def resolve(c, q, binary="lin"):
    """name/EA -> list of (bin, ea, name) on the requested side (via xmap if needed)."""
    m = re.fullmatch(r"(?:0x)?([0-9A-Fa-f]{5,16})", q)
    if m:
        ea = int(m.group(1), 16)
        r = c.execute("select bin, ea, name from func where ea=? order by bin=? desc", (ea, binary)).fetchall()
    else:
        r = c.execute("select bin, ea, name from func where base=? or name=?", (q, q)).fetchall()
        if not r:
            r = c.execute("select bin, ea, name from func where base like ? limit 50", ("%" + q + "%",)).fetchall()
    out = []
    for b, ea, nm in r:
        if b == binary:
            out.append((b, ea, nm))
        else:
            col_from, col_to = ("win", "lin") if b == "win" else ("lin", "win")
            for (t,) in c.execute(f"select {col_to} from xmap where {col_from}=?", (ea,)):
                n2 = c.execute("select name from func where bin=? and ea=?", (binary, t)).fetchone()
                out.append((binary, t, n2[0] if n2 else nm))
    return list(dict.fromkeys(out))


def twin(c, b, ea):
    if b == "lin":
        r = c.execute("select win, method, score from xmap where lin=?", (ea,)).fetchone()
    else:
        r = c.execute("select lin, method, score from xmap where win=?", (ea,)).fetchone()
    return r


def cmd_find(a):
    c = db(); rx = re.compile(a.q, re.I); n = 0
    for b, ea, size, nm in c.execute("select bin, ea, size, name from func where bin='lin' or name not like 'sub_%' order by bin, name"):
        if not rx.search(nm):
            continue
        if b == "win" and c.execute("select 1 from xmap where win=?", (ea,)).fetchone():
            continue  # printed with its Linux twin
        t = twin(c, b, ea)
        other = ("win %X (%s %.1f)" % t if b == "lin" else "lin %X" % t[0]) if t else ("win -" if b == "lin" else "")
        print("%s %X %5d  %-60s %s" % (b, ea, size, nm[:60], other))
        n += 1
        if n >= a.limit:
            print("... (limit %d, use --limit)" % a.limit); break


def show_code(c, b, ea, nm, lines):
    r = c.execute("select body from code where bin=? and ea=?", (b, ea)).fetchone()
    t = twin(c, b, ea)
    print("== %s %X %s%s" % (b, ea, nm, ("   [twin %s %X]" % ("win" if b == "lin" else "lin", t[0])) if t else ""))
    if not r:
        print("(no pseudocode in index)"); return
    body = r[0].splitlines()
    if lines:
        lo, hi = [int(x) for x in lines.split("-")]
        body = ["%4d %s" % (i, l) for i, l in enumerate(body, 1) if lo <= i <= hi]
    print("\n".join(body))


def cmd_code(a):
    c = db(); hits = resolve(c, a.q, "win" if a.win else "lin")
    if not hits:
        sys.exit("not found (try: re.py find %s)" % a.q)
    if len(hits) > 1 and not a.all:
        for h in hits[:30]: print("%s %X %s" % h)
        print("%d matches: give the exact name, an EA, or --all" % len(hits)); return
    for h in hits:
        show_code(c, *h, a.lines)


def cmd_xref(a):
    c = db(); b = "win" if a.win else "lin"
    for _, ea, nm in resolve(c, a.q, b)[:3]:
        print("== %s %X %s" % (b, ea, nm))
        for title, q in (("callers", "select caller, kind from calls where bin=? and callee=?"),
                         ("callees", "select callee, kind from calls where bin=? and caller=?")):
            rs = c.execute(q, (b, ea)).fetchall()
            print("  %s (%d):" % (title, len(rs)))
            for e, k in rs[:a.limit]:
                n = c.execute("select name from func where bin=? and ea=?", (b, e)).fetchone()
                print("    %X %s%s" % (e, n[0] if n else "?", "" if k == "call" else "  [" + k + "]"))


def cmd_str(a):
    c = db()
    for b, ea, t, refs in c.execute("select bin, ea, text, refs from str where text like ? order by bin limit ?", ("%" + a.q + "%", a.limit)):
        print("%s %X %-50s <- %s" % (b, ea, t[:50], refs[:200]))


def cmd_grep(a):
    c = db(); rx = re.compile(a.q); n = 0
    for ea, nm, body in c.execute("select ea, name, body from code where bin=?", ("win" if a.win else "lin",)):
        if not rx.search(body):
            continue
        for i, l in enumerate(body.splitlines(), 1):
            if rx.search(l):
                print("%X %s:%d: %s" % (ea, nm[:50], i, l.strip()[:160])); n += 1
                if n >= a.limit:
                    print("... (limit %d)" % a.limit); return


def cmd_addr(a):
    c = db(); d = open(EXE, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]; ns = struct.unpack_from("<H", d, pe + 6)[0]
    so = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    magic = struct.unpack_from("<H", d, pe + 24)[0]  # 0x20B = PE32+
    ib = struct.unpack_from("<Q", d, pe + 48)[0] if magic == 0x20B else struct.unpack_from("<I", d, pe + 52)[0]
    secs = [struct.unpack_from("<IIII", d, so + i * 40 + 8) for i in range(ns)]
    for _, ea, nm in resolve(c, a.q, "win"):
        rva = ea - ib
        off = next(rva - va + ro for vs, va, rs, ro in secs if va <= rva < va + rs)
        b = d[off:off + 32]
        n = 8
        while n < 32 and d.count(b[:n]) > 1:
            n += 1
        t = twin(c, "win", ea)
        print("%s\n  VA %X  RVA %X  file %X  %s" % (nm, ea, rva, off, ("port: %s %.1f" % (t[1], t[2])) if t else "manual/IDA name"))
        print("  bytes %s" % b.hex(" ").upper())
        print("  unique prefix: %d bytes (%s)" % (n, "unique" if d.count(b[:n]) == 1 else "NOT unique in 32"))


def cmd_asm(a):
    c = db(); b = "lin" if a.lin else "win"
    for _, ea, nm in resolve(c, a.q, b)[:3]:
        r = c.execute("select body from asm where bin=? and ea=?", (b, ea)).fetchone()
        p = c.execute("select proto from proto where bin=? and ea=?", (b, ea)).fetchone()
        print("== %s %X %s   %s" % (b, ea, nm, p[0] if p else ""))
        body = (r[0] if r else "(no disassembly)").splitlines()
        if a.lines:
            lo, hi = [int(x) for x in a.lines.split("-")]; body = body[lo - 1:hi]
        print("\n".join(body if a.lines or a.all else body[:200]))
        if not (a.lines or a.all) and len(body) > 200:
            print("... %d more lines (--lines A-B or --all)" % (len(body) - 200))


def cmd_data(a):
    c = db(); b = "win" if a.win else "lin"
    if a.uses:
        for _, ea, nm in resolve(c, a.q, b)[:3]:
            print("== %s %X %s uses:" % (b, ea, nm))
            for t, tn, k in c.execute("select target, tname, kind from dref where bin=? and func=? order by target", (b, ea)):
                print("  %s %X %s" % (k, t, tn))
        return
    rx = re.compile(a.q); n = 0
    for f, t, tn, k in c.execute("select func, target, tname, kind from dref where bin=? order by tname", (b,)):
        if rx.search(tn or "%X" % t):
            fn = c.execute("select name from func where bin=? and ea=?", (b, f)).fetchone()
            print("%s %X %-40s <- %X %s" % (k, t, tn[:40], f, fn[0] if fn else "?")); n += 1
            if n >= a.limit:
                print("... (limit %d)" % a.limit); return


def cmd_stats(a):
    c = db()
    for q in ("select bin, count(*), sum(name not like 'sub_%') from func group by bin",
              "select method, count(*) from xmap group by 1 order by 2 desc",
              "select bin, count(*) from code group by bin"):
        print(c.execute(q).fetchall())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("build").set_defaults(f=lambda a: build())
    sp.add_parser("stats").set_defaults(f=cmd_stats)
    for name, f in (("find", cmd_find), ("code", cmd_code), ("xref", cmd_xref), ("str", cmd_str),
                    ("grep", cmd_grep), ("addr", cmd_addr), ("asm", cmd_asm), ("data", cmd_data)):
        p = sp.add_parser(name); p.add_argument("q"); p.set_defaults(f=f)
        p.add_argument("--win", action="store_true"); p.add_argument("--limit", type=int, default=60)
        p.add_argument("--lines"); p.add_argument("--all", action="store_true"); p.add_argument("--raw", action="store_true")
        p.add_argument("--lin", action="store_true"); p.add_argument("--uses", action="store_true")
    a = ap.parse_args(); a.f(a)


if __name__ == "__main__":
    main()
