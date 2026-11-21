import pstres, struct, re, json, os
o, e = pstres.orig(), pstres.ee()
gem = {}
for l in open(os.path.join(pstres.GEMRB_UNHARD, "gemprjtl.ids"), encoding="latin1"):
    m = re.match(r"\s*(\d+)\s+(\S+)", l)
    if m: gem[int(m.group(1))] = m.group(2)
eeids = {}
for l in pstres.text(e.res("PROJECTL", "ids")).splitlines():
    m = re.match(r"\s*(\d+)\s+(\S+)\s*(//.*)?", l)
    if m: eeids[int(m.group(1))] = (m.group(2), (m.group(3) or "").strip("/ "))
cases = set(int(x) for x in re.findall(r"case (\d+):", open([os.path.join("ee/dump/dec", f) for f in os.listdir("ee/dump/dec") if f.startswith("CProjectile_DecodeProjectile")][0]).read()))
gemdir = {f[:-4].upper() for f in os.listdir(pstres.GEMRB_UNHARD) if f.endswith(".pro")}
rows = []
def projs(d, kind):
    if not d: return []
    eho, ehc = struct.unpack_from("<IH", d, 0x64)
    hs, po = (0x28, 0x26) if kind == "spl" else (0x38, 0x2A)
    return sorted({struct.unpack_from("<H", d, eho + h*hs + po)[0] for h in range(ehc)})
for kind in ("spl", "itm"):
    for n in o.names(kind):
        d = o.res(n, kind)
        if not d or d[:3] not in (b"SPL", b"ITM"): continue
        de = e.res(n, kind)
        for p in projs(d, kind):
            if p <= 1: continue
            gname = gem.get(p - 1, "")
            pe = projs(de, kind) if de and de[:3] in (b"SPL", b"ITM") else []
            rows.append(dict(res=n + "." + kind, proj=p, gemrb=gname, gemrb_file=gname.upper() in gemdir,
                ee_proj=pe, ee_name=[eeids.get(x - 1, ("", ""))[0] for x in pe], ee_hard=[x in cases for x in pe],
                ee_file=[bool(e.where(eeids.get(x - 1, ("",))[0], "pro")) if eeids.get(x - 1) else False for x in pe]))
json.dump(rows, open("projmap.json", "w"), indent=1)
bad = [r for r in rows if not r["gemrb_file"]]
print(len(rows), "uses; GemRB has no .pro for", len(bad))
for r in bad: print(" ", r["res"], r["proj"], r["gemrb"], "| EE:", r["ee_proj"], r["ee_name"], "hard" if any(r["ee_hard"]) else "")
diff = [r for r in rows if r["ee_proj"] and r["proj"] not in r["ee_proj"]]
print("EE changed projectile number:", len(diff))
for r in diff[:40]: print(" ", r["res"], r["proj"], r["gemrb"], "->", r["ee_proj"], r["ee_name"])
