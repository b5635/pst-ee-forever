# Align effects of the same SPL/ITM in original PST and PST:EE -> which EE opcode each PST opcode became
import pstres, struct, json, collections
o, e = pstres.orig(), pstres.ee()
ee_cls = {int(k): v for k, v in json.load(open("ee_opcodes.json")).items()}
def sig(d, eo):
    op, tg, pw, p1, p2, tm, rs, dur = struct.unpack_from("<HBBIIBBI", d, eo)
    res = d[eo+0x14:eo+0x1C].split(bytes(1))[0]
    return (op, (p1, p2, tm, dur, res))
import difflib
def effs(d, kind):
    if not d: return None
    if kind == "spl":
        if d[:4] != b"SPL ": return None
        eho, ehc, fxo, cfi, cfc = struct.unpack_from("<IHIHH", d, 0x64); hs = 0x28
    else:
        if d[:4] != b"ITM ": return None
        eho, ehc, fxo, cfi, cfc = struct.unpack_from("<IHIHH", d, 0x64); hs = 0x38
    groups = [[sig(d, fxo + (cfi + i) * 0x30) for i in range(cfc)]]
    for h in range(ehc):
        nfx, fi = struct.unpack_from("<HH", d, eho + h * hs + 0x1E)
        groups.append([sig(d, fxo + (fi + i) * 0x30) for i in range(nfx)])
    return groups
pairs = collections.defaultdict(collections.Counter); ex = {}
changed_files = []
for kind in ("spl", "itm"):
    for n in sorted(set(o.names(kind)) & set(e.names(kind))):
        a, b = effs(o.res(n, kind), kind), effs(e.res(n, kind), kind)
        if not a or not b: continue
        if [len(x) for x in a] != [len(x) for x in b]:
            changed_files.append((n + "." + kind, [len(x) for x in a], [len(x) for x in b]))
        for ga, gb in zip(a, b):
            sm = difflib.SequenceMatcher(None, [x[1] for x in ga], [y[1] for y in gb], autojunk=False)
            for bl in sm.get_matching_blocks():
                for k in range(bl.size):
                    x, y = ga[bl.a + k][0], gb[bl.b + k][0]
                    pairs[x][y] += 1; ex.setdefault((x, y), n + "." + kind)
out = {}
for x in sorted(pairs):
    out[x] = [(y, c, ee_cls.get(y, "?"), ex[(x, y)]) for y, c in pairs[x].most_common()]
json.dump({"map": {str(k): v for k, v in out.items()}, "changed": changed_files}, open("opmap.json", "w"), indent=1)
for x, v in out.items(): print(x, v[:3])
print("files with different effect counts:", len(changed_files))
