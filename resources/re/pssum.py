import os, re, collections
D = "ee/dump/dec"
by = collections.defaultdict(list)
for f in os.listdir(D):
    m = re.match(r"(CPsSpell\w+?)_", f)
    if m: by[m.group(1)].append(f)
out = []
for c in sorted(by):
    strs, calls, effs, nums = set(), collections.Counter(), [], set()
    for f in by[c]:
        s = open(os.path.join(D, f), encoding="utf-8").read()
        strs |= set(re.findall(r'"([A-Za-z0-9_#]{3,8})"', s))
        for m in re.finditer(r"\b(C[A-Z]\w+::\w+)\(", s): calls[m.group(1)] += 1
        # Item_effect_st fills: look for opcode assignment patterns *(_WORD *)&v = N; before DecodeEffect
        for m in re.finditer(r"LOWORD\((\w+)\) = (\d+);|\*\(_WORD \*\)(?:&)?(\w+)(?:\[0\])? = (\d+);", s): pass
        effs += re.findall(r"CGameEffect(\w+)::CGameEffect\w*\(", s)
        effs += [x for x in re.findall(r"CGameEffect::DecodeEffect", s)]
    interesting = [k for k in calls if re.search(r"Effect|Damage|Spell|Save|Hit|Target|Sprite::|Area::|Search|Visible|Morale|Apply|Cast", k) and not k.startswith("CResRef") and not k.startswith("CString")]
    out.append("### %s\nfuncs: %s\nresrefs: %s\neffects: %s\ncalls: %s\n" % (c, ", ".join(sorted(f[len(c)+1:-2] for f in by[c])), " ".join(sorted(strs)), ", ".join(sorted(set(effs))), ", ".join(sorted(interesting))))
open("pssum.md", "w").write("\n".join(out)); print(len(out))
