# Opcode audit: every effect opcode PST data uses (SPL/ITM/EFF/CRE), mapped to GemRB's PST
# effects.ids name and the fx_ function GemRB binds it to; flags stub/partial implementations.
import re, struct, os, json, collections, sys
sys.path.insert(0, os.path.dirname(__file__))
import pstres

REPO = r"D:\gemrb"
g = pstres.gemrb()

def effects_v1(d, off, n):
	for i in range(n):
		eo = off + i*0x30
		if eo + 0x30 > len(d): return
		op, tgt, pw, p1, p2, tm, rs, dur, pmax, pmin = struct.unpack_from("<HBBIIBBIBB", d, eo)
		res = d[eo+0x14:eo+0x1C].split(b"\0")[0].decode("latin1")
		yield dict(op=op, p1=p1, p2=p2, tm=tm, dur=dur, res=res)

uses = collections.defaultdict(list)   # op -> [(resource, p1, p2, res)]
for n in g.names("spl"):
	d = g.res(n, "spl")
	if not d or d[:4] != b"SPL ": continue
	eho, ehc, fxo, cfi, cfc = struct.unpack_from("<IHIHH", d, 0x64)
	for e in effects_v1(d, fxo + cfi*0x30, cfc): uses[e["op"]].append((n+".spl", e))
	for h in range(ehc):
		nfx, fi = struct.unpack_from("<HH", d, eho + h*0x28 + 0x1E)
		for e in effects_v1(d, fxo + fi*0x30, nfx): uses[e["op"]].append((n+".spl", e))
for n in g.names("itm"):
	d = g.res(n, "itm")
	if not d or d[:4] != b"ITM ": continue
	eho, ehc, fxo, fei, fec = struct.unpack_from("<IHIHH", d, 0x64)
	for e in effects_v1(d, fxo + fei*0x30, fec): uses[e["op"]].append((n+".itm", e))
	for h in range(ehc):
		nfx, fi = struct.unpack_from("<HH", d, eho + h*0x38 + 0x1E)
		for e in effects_v1(d, fxo + fi*0x30, nfx): uses[e["op"]].append((n+".itm", e))
for n in g.names("eff"):
	d = g.res(n, "eff")
	if not d: continue
	if d[:8] == b"EFF V2.0":
		op, = struct.unpack_from("<I", d, 0x10)
		p1, p2 = struct.unpack_from("<II", d, 0x1C - 0x10 + 0x10 + 4) if False else struct.unpack_from("<II", d, 0x1C)
		res = d[0x30:0x38].split(b"\0")[0].decode("latin1")
		uses[op].append((n+".eff", dict(op=op, p1=p1, p2=p2, res=res)))
for n in g.names("cre"):
	d = g.res(n, "cre")
	if not d or d[:4] != b"CRE ": continue
	ver = d[4:8]
	if ver != b"V1.2": continue
	fxo, fxc = struct.unpack_from("<II", d, 0x35A)
	eff2, = struct.unpack_from("<B", d, 0x33)
	if eff2: continue
	for e in effects_v1(d, fxo, fxc): uses[e["op"]].append((n+".cre", e))

# GemRB side
ids = {}
for line in open(os.path.join(pstres.GEMRB_UNHARD, "effects.ids"), encoding="latin1"):
	m = re.match(r"\s*(0x[0-9a-fA-F]+|\d+)\s+(\S+)", line)
	if m: ids[int(m.group(1), 0)] = m.group(2)
bind = {}   # name -> (func, file)
bodies = {}
for f in ("FXOpcodes/FXOpcodes.cpp", "PSTOpcodes/PSTOpcodes.cpp", "IWDOpcodes/IWDOpcodes.cpp"):
	src = open(os.path.join(REPO, "gemrb", "plugins", f), encoding="utf-8", errors="replace").read()
	for m in re.finditer(r'EffectDesc\("([^"]+)",\s*(\w+)', src):
		bind.setdefault(m.group(1), (m.group(2), f))
	for m in re.finditer(r'\nint (fx_\w+)\s*\([^)]*\)\s*\{(.*?)\n\}', src, re.S):
		bodies[m.group(1)] = (f, m.group(2))
core = ""
for root, _, fs in os.walk(os.path.join(REPO, "gemrb", "core")):
	for f in fs:
		if f == "EffectQueue.cpp":
			core = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
for m in re.finditer(r'EffectDesc\("([^"]+)",\s*(\w+)', core):
	bind.setdefault(m.group(1), (m.group(2), "core/EffectQueue.cpp"))

def stub_kind(func):
	if func not in bodies: return "?"
	body = bodies[func][1]
	lines = [l.strip() for l in body.splitlines() if l.strip() and not l.strip().startswith("//")]
	low = body.lower()
	tags = []
	if re.search(r"not (yet )?implemented|unimplemented|stub", low): tags.append("says-unimplemented")
	if "todo" in low or "fixme" in low: tags.append("todo")
	if len(lines) <= 2 and re.search(r"return FX_NOT_APPLIED", body): tags.append("no-op")
	if "Log(" in body and "Unimplemented" in body: tags.append("logs-unimpl")
	return ",".join(tags)

rows = []
for op in sorted(uses):
	name = ids.get(op, "")
	func, f = bind.get(name, ("", ""))
	files = sorted({u[0] for u in uses[op]})
	rows.append(dict(op=op, name=name, func=func, file=f, uses=len(uses[op]), files=len(files),
		example=files[:6], stub=stub_kind(func) if func else ("unmapped" if not name else "unbound"),
		p2=sorted({u[1]["p2"] for u in uses[op]})[:8], res=sorted({u[1]["res"] for u in uses[op] if u[1]["res"]})[:6]))
json.dump(rows, open(os.path.join(os.path.dirname(__file__), "opcodes.json"), "w"), indent=1)
for r in rows:
	print("%3d %-32s %-34s %-6s n=%-4d %s %s" % (r["op"], r["name"], r["func"], r["stub"][:20], r["uses"], ",".join(r["example"][:3]), r["p2"][:4]))
