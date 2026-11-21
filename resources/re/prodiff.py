# For each EE CPsSpell class: resrefs it uses vs resrefs reachable from GemRB's .pro for that spell
import os, re, struct, json, pstres
g = pstres.gemrb()
U = pstres.GEMRB_UNHARD
gem = {}
for l in open(os.path.join(U, "gemprjtl.ids"), encoding="latin1"):
    m = re.match(r"\s*(\d+)\s+(\S+)", l)
    if m: gem[int(m.group(1))] = m.group(2).upper()
def rr(d, o): return d[o:o+8].split(bytes(1))[0].decode("latin1").upper()
def pro_refs(name, seen=None):
    seen = seen if seen is not None else set()
    if not name or name in seen: return set()
    seen.add(name)
    d = g.res(name, "pro")
    if not d: return {"<missing %s.pro>" % name}
    refs = {rr(d, 0x10), rr(d, 0x18), rr(d, 0x20), rr(d, 0x58), rr(d, 0x60), rr(d, 0x104 + 0x0), rr(d, 0x10C), rr(d, 0x114)}
    refs |= {rr(d, 0x104 + 0x24)}
    if len(d) >= 0x220:
        refs |= {rr(d, 0x204), rr(d, 0x200 + 0x0C), rr(d, 0x200 + 0x16)}
    for o in range(0x100, len(d) - 8):
        pass
    refs = {r for r in refs if r and re.match(r"^[A-Z0-9_#]+$", r)}
    # GemRB extension spells and secondary projectiles
    out = set(refs)
    for s in (rr(d, 0x44), rr(d, 0x4C)):
        out |= spl_refs(s)
    if len(d) >= 0x220:
        fp, = struct.unpack_from("<H", d, 0x200 + 0x12)  # fragment proj
        ep, = struct.unpack_from("<H", d, 0x200 + 0x1A)
        for p in (fp, ep):
            if p and gem.get(p - 1): out |= pro_refs(gem[p - 1], seen)
    return out
def spl_refs(n):
    d = g.res(n, "spl") if n else None
    if not d or d[:4] != b"SPL ": return set()
    eho, ehc, fxo, cfi, cfc = struct.unpack_from("<IHIHH", d, 0x64)
    s = {n}
    tot = cfc
    for h in range(ehc):
        nfx, fi = struct.unpack_from("<HH", d, eho + h*0x28 + 0x1E); tot = max(tot, fi + nfx)
    for i in range(tot):
        s.add(rr(d, fxo + i*0x30 + 0x14))
    return {x for x in s if x}
def all_strings_refs(name):
    d = g.res(name, "pro") or b""
    return {m.decode().upper() for m in re.findall(rb"[A-Za-z0-9_]{4,8}", d)}
pss = {}
for f in os.listdir("ee/dump/dec"):
    m = re.match(r"(CPsSpell\w+?)_", f)
    if m: pss.setdefault(m.group(1), set()).update(re.findall(r'"([A-Za-z0-9_#]{3,8})"', open("ee/dump/dec/" + f, encoding="utf-8").read()))
# map class -> case number -> SPL using it
dp = open([os.path.join("ee/dump/dec", f) for f in os.listdir("ee/dump/dec") if f.startswith("CProjectile_DecodeProjectile")][0]).read()
cls_case = {}
for m in re.finditer(r"case (\d+):(.*?)goto LABEL", dp, re.S):
    c = re.search(r"(CPsSpell\w+)::CPsSpell", m.group(2))
    if c: cls_case.setdefault(c.group(1), int(m.group(1)))
o = pstres.orig()
byname = {}
for n in o.names("spl"):
    d = o.res(n, "spl")
    if not d or d[:4] != b"SPL ": continue
    nm = re.sub(r"[^a-z]", "", o.string(struct.unpack_from("<I", d, 8)[0]).lower())
    eho, ehc = struct.unpack_from("<IH", d, 0x64)
    if ehc and nm and n[:4] in ("SPWI","SPPR","SPIN"): byname.setdefault(nm, (n, struct.unpack_from("<H", d, eho + (ehc-1)*0x28 + 0x26)[0]))
alias = {"axoftorment":"axeoftorment","cloudkill":"cloudkill","elysiumstearsmeteor":None,"embalming":"minorembalming","stygianicestorm":"stygianicestorm","tashaslaughter":"tashasunbearablederisivelaughter","chainlightningstorm":"chainlightningstorm","meteorstormbombardment":"meteorstormbombardment"}
rows = []
for c in sorted(pss):
    key = re.sub(r"[^a-z]", "", c[len("CPsSpell"):].lower()); key = alias.get(key, key)
    hit = byname.get(key) if key else None
    case = cls_case.get(c) or (hit[1] if hit else None)
    gname = gem.get(case - 1) if case else None
    ee_refs = {r.upper() for r in pss[c] if not r.startswith("SPWI")}
    gref = pro_refs(gname) | all_strings_refs(gname) if gname else set()
    # also GemRB spl of same name (spwiNNN) effects
    missing = sorted(r for r in ee_refs if r not in gref and g.where(r, "bam") or (r not in gref and (g.where(r, "wav") or g.where(r, "vvc"))))
    rows.append(dict(spl=hit[0] if hit else None, cls=c, case=case, gemrb_pro=gname, gemrb_has=bool(gname and g.res(gname, "pro")), ee_refs=sorted(ee_refs), not_in_gemrb=missing))
json.dump(rows, open("prodiff.json", "w"), indent=1)
for r in rows: print("%-8s %-32s case=%-4s gemrb=%-9s %-5s missing: %s" % (r["spl"], r["cls"], r["case"], r["gemrb_pro"], r["gemrb_has"], " ".join(r["not_in_gemrb"])))
