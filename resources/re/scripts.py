# Script audit: actions/triggers/objects used by PST BCS + DLG, mapped to GemRB's tables.
import re, struct, os, json, collections, sys
sys.path.insert(0, os.path.dirname(__file__))
import pstres

g = pstres.gemrb()
def ids(name):
	t = pstres.text(g.res(name, "ids"))
	out = {}
	for line in t.splitlines():
		m = re.match(r"\s*(0x[0-9a-fA-F]+|-?\d+)\s+(\w+)\s*(\(.*)?", line)
		if m: out[int(m.group(1), 0)] = (m.group(2), (m.group(3) or "").strip())
	return out
ACT, TRG, OBJ = ids("action"), ids("trigger"), ids("object")

act_use = collections.Counter(); trg_use = collections.Counter(); where = collections.defaultdict(set)
def scan_bcs(text, src):
	# actions: AC\n<id> ... ; triggers: TR\n<id>
	for m in re.finditer(r"AC\s*\n?(\d+)", text):
		a = int(m.group(1)); act_use[a] += 1; where[("a", a)].add(src)
	for m in re.finditer(r"TR\s*\n?(\d+)", text):
		t = int(m.group(1)); trg_use[t] += 1; where[("t", t)].add(src)
for n in g.names("bcs"):
	d = g.res(n, "bcs")
	if d: scan_bcs(d.decode("latin1"), n + ".bcs")
# dialogs: actions/triggers as text
dact = collections.Counter(); dtrg = collections.Counter()
for n in g.names("dlg"):
	d = g.res(n, "dlg")
	if not d or d[:4] != b"DLG ": continue
	hdr = struct.unpack_from("<IIIIIIIIII", d, 8)
	ns, so, nt, to, sto, nst, tto, ntt, ao, na = hdr
	def strs(off, cnt):
		for i in range(cnt):
			if off + i*8 + 8 > len(d): return
			o, l = struct.unpack_from("<II", d, off + i*8)
			yield d[o:o+l].decode("latin1")
	for s in list(strs(sto, nst)) + list(strs(tto, ntt)):
		for m in re.finditer(r"(!?)([A-Za-z_]\w*)\s*\(", s):
			dtrg[m.group(2).lower()] += 1; where[("dt", m.group(2).lower())].add(n + ".dlg")
	for s in strs(ao, na):
		for m in re.finditer(r"([A-Za-z_]\w*)\s*\(", s):
			dact[m.group(2 - 1).lower()] += 1; where[("da", m.group(1).lower())].add(n + ".dlg")

src = open(r"D:\gemrb\gemrb\core\GameScript\GameScript.cpp", encoding="utf-8").read()
def table(name):
	blk = re.search(r"static const \w+ %s\[\] = \{(.*?)\n\};" % name, src, re.S).group(1)
	out = {}
	for m in re.finditer(r'\{\s*"(\w+)",\s*([\w:]+|nullptr)\s*,\s*([^}]*)\}', blk):
		out[m.group(1)] = (m.group(2), m.group(3).strip())
	return out
GA, GT, GO = table("actionnames"), table("triggernames"), table("objectnames")
# GemRB treats some functions as "not really implemented"
NOOP = {"GameScript::NoAction", "nullptr", "GameScript::False", "GameScript::NoActionAtAll"}

def status(gem, name):
	if name not in gem: return "MISSING"
	fn, fl = gem[name]
	if fn in NOOP: return "noop(" + fn.split("::")[-1] + ")"
	if "AF_INVALID" in fl: return "invalid"
	return ""

rows = {"actions": [], "triggers": []}
for i in sorted(set(act_use) | {k for k in ACT}):
	nm, sig = ACT.get(i, ("?%d" % i, ""))
	used = act_use.get(i, 0) + dact.get(nm.lower(), 0)
	if not used: continue
	st = status(GA, nm.lower())
	rows["actions"].append(dict(id=i, name=nm, sig=sig, uses=used, status=st, gemrb=GA.get(nm.lower(), ("",))[0],
		where=sorted(where[("a", i)] | where[("da", nm.lower())])[:6]))
for i in sorted(set(trg_use) | {k for k in TRG}):
	nm, sig = TRG.get(i, ("?%d" % i, ""))
	used = trg_use.get(i, 0) + dtrg.get(nm.lower(), 0)
	if not used: continue
	st = status(GT, nm.lower())
	rows["triggers"].append(dict(id=i, name=nm, sig=sig, uses=used, status=st, gemrb=GT.get(nm.lower(), ("",))[0],
		where=sorted(where[("t", i)] | where[("dt", nm.lower())])[:6]))
# dialog names not in ids at all
known_a = {v[0].lower() for v in ACT.values()}; known_t = {v[0].lower() for v in TRG.values()}
rows["dlg_unknown_actions"] = sorted(k for k in dact if k not in known_a)
rows["dlg_unknown_triggers"] = sorted(k for k in dtrg if k not in known_t)
json.dump(rows, open(os.path.join(os.path.dirname(__file__), "scripts.json"), "w"), indent=1)
for k in ("actions", "triggers"):
	bad = [r for r in rows[k] if r["status"]]
	print("== %s used: %d, problems: %d" % (k, len(rows[k]), len(bad)))
	for r in bad: print("  %4d %-28s %-12s n=%-5d %s" % (r["id"], r["name"], r["status"], r["uses"], ",".join(r["where"][:4])))
print("dlg unknown actions", rows["dlg_unknown_actions"][:40])
print("dlg unknown triggers", rows["dlg_unknown_triggers"][:40])
