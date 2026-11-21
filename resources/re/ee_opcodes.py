# EE opcode -> effect class (from CGameEffect::DecodeEffect switch + vtable symbols)
import re, glob, json
vt = {}
for l in open("ee/dump/names.txt", encoding="utf-8", errors="replace"):
    ea, n, d = l.rstrip("\n").split("\t")
    if d.startswith("`vtable for'"): vt[int(ea, 16) + 16] = d[len("`vtable for'"):]
src = open(glob.glob("ee/dump/dec/CGameEffect_DecodeEffect*.c")[0]).read()
body = src.split("switch", 1)[1]
cur = []; res = {}
for line in body.splitlines():
    m = re.match(r"\s*case (0x[0-9A-F]+|\d+)u:", line)
    if m: cur.append(int(m.group(1), 0)); continue
    m = re.search(r"= off_([0-9A-F]+);", line) or re.search(r"= &`vtable for'(\w+)", line)
    if m and cur:
        try: name = vt.get(int(m.group(1), 16), "off_" + m.group(1))
        except ValueError: name = m.group(1)
        for c in cur: res.setdefault(c, name)
    if re.match(r"\s*break;", line) or re.match(r"\s*return", line): cur = []
json.dump({str(k): v for k, v in sorted(res.items())}, open("ee_opcodes.json", "w"), indent=0)
print(len(res)); [print(k, v) for k, v in sorted(res.items())]
