# SPDX-License-Identifier: GPL-2.0-or-later
"""Port Linux Torment function names (3.2.2, not stripped) onto Windows Torment.exe (3.2.2 x64, no symbols).

Inputs : ee/dump/features.jsonl, win/dump/features.jsonl, ee/dump/vtables.tsv, win/dump/vtables.tsv,
         win/manual_names.tsv (hand-confirmed seeds)
Output : win/dump/names_win.tsv  (win_ea, method, score, linux_ea, linux_mangled, linux_demangled)
         win/dump/match_report.txt
Run    : python -I match.py   (from resources/re)
"""
import json, os, re, struct, sys
from collections import defaultdict, Counter

HERE = os.path.dirname(os.path.abspath(__file__))
L_DIR, W_DIR = os.path.join(HERE, "lin", "dump"), os.path.join(HERE, "win", "dump")


def load(path):
    fs = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            o = json.loads(line)
            fs[o["ea"]] = o
    return fs


L, W = load(os.path.join(L_DIR, "features.jsonl")), load(os.path.join(W_DIR, "features.jsonl"))
named = lambda o: not re.match(r"^(sub_|nullsub_|j_|unknown_libname|\.)", o["n"])

match_w2l, match_l2w, method = {}, {}, {}
score = {}


def plausible(l, w):
    a, b = L[l], W[w]
    if a["sz"] and b["sz"]:
        r = b["sz"] / a["sz"]
        if not (0.25 <= r <= 4.0) and abs(a["sz"] - b["sz"]) > 64:
            return False
    if a["bb"] and b["bb"] and abs(a["bb"] - b["bb"]) > max(6, 0.6 * max(a["bb"], b["bb"])):
        return False
    return True


def similarity(l, w):
    a, b = L[l], W[w]
    s = 0.0
    sa, sb = Counter(a["s"]), Counter(b["s"])
    if sa or sb:
        s += 3.0 * sum((sa & sb).values()) / max(1, sum((sa | sb).values()))
    ka, kb = Counter(a["k"]), Counter(b["k"])
    if ka or kb:
        s += 1.5 * sum((ka & kb).values()) / max(1, sum((ka | kb).values()))
    if a["bb"] and b["bb"]:
        s += 1.0 - abs(a["bb"] - b["bb"]) / max(a["bb"], b["bb"])
    va, vb = Counter(a["v"]), Counter(b["v"])
    if va or vb:
        s += 1.0 * sum((va & vb).values()) / max(1, sum((va | vb).values()))
    oa, ob = set(a.get("o", ())), set(b.get("o", ()))  # struct member offsets: same release, both x64
    if oa or ob:
        s += 2.0 * len(oa & ob) / len(oa | ob)
    return s


def add(l, w, how, sc=None, check=True):
    if l in match_l2w or w in match_w2l:
        return False
    if check and not plausible(l, w):
        return False
    match_l2w[l], match_w2l[w], method[w] = w, l, how
    score[w] = similarity(l, w) if sc is None else sc
    return True


# ---- 0. seeds ----
l_by_dem = defaultdict(list)
for ea, o in L.items():
    l_by_dem[o["d"].split("(")[0]].append(ea)

# hand-confirmed names: win/manual_names.tsv  (win_ea<TAB>linux demangled name, '#' comments)
mp = os.path.join(HERE, "win", "manual_names.tsv")
if os.path.exists(mp):
    for line in open(mp, encoding="utf-8"):
        if line.strip() and not line.startswith("#"):
            w, nm = line.rstrip("\n").split("\t")[:2]
            cands = l_by_dem.get(nm.split("(")[0], [])
            cands = [l for l in cands if L[l]["d"] == nm] or cands
            if int(w, 16) in W and len(cands) == 1:
                add(cands[0], int(w, 16), "manual", 10.0, check=False)
            else:
                print("manual_names: cannot place", w, nm, file=sys.stderr)

# ---- 0b. Diaphora (independent differ). Cross-compiler (GCC vs MSVC+LTCG) most of its heuristics are wrong
# here; measured against string evidence on 3.2.2 only these were >= 85% precise, so only these are used, and
# only after our own string/vtable evidence (they never override it). Re-measure if the build changes.
DIA = os.path.join(HERE, "diaphora", "lin_vs_win.diaphora")
DIA_OK = {"Perfect match, same name", "Callee found diffing matches assembly (iteration #1)", "Switch structures",
          "Same constants", "Same MD Index and constants", "Same low complexity and names", "Equal pseudo-code",
          "Similar pseudo-code and names", "Same cleaned pseudo-code", "Same cleaned microcode",
          "Microcode mnemonics small primes product"}
dia_ok = []
if os.path.exists(DIA) and not os.getenv("NO_DIAPHORA"):
    import sqlite3
    for typ, a1, a2, ratio, desc in sqlite3.connect(DIA).execute(
            "select type, address, address2, ratio, description from results"):
        l, w = int(str(a1), 16), int(str(a2), 16)
        if l in L and w in W and desc in DIA_OK:
            dia_ok.append((float(ratio), l, w, desc))
    dia_ok.sort(reverse=True)
    print("diaphora candidates (trusted heuristics):", len(dia_ok))


def diaphora_pass():
    n = 0
    for r, l, w, desc in dia_ok:
        n += add(l, w, "diaphora", 4.0 + r)
    return n


# ---- 1. unique string sets, then unique single strings ----
def by_key(fs, keyf):
    m = defaultdict(list)
    for ea, o in fs.items():
        k = keyf(o)
        if k:
            m[k].append(ea)
    return m


for keyf, how in [(lambda o: tuple(sorted(set(o["s"]))) if o["s"] else None, "strset"),
                  (lambda o: tuple(sorted(set(o["k"]))) if len(set(o["k"])) >= 3 else None, "constset"),
                  (lambda o: tuple(sorted(set(o.get("i", [])))) if len(set(o.get("i", []))) >= 2 else None, "importset"),
                  (lambda o: (tuple(sorted(set(o.get("o", [])))), tuple(sorted(set(o["v"])))) if len(set(o.get("o", []))) >= 5 else None, "offsetset")]:
    lm, wm = by_key(L, keyf), by_key(W, keyf)
    for k, ls in lm.items():
        ws = wm.get(k)
        if ws and len(ls) == 1 and len(ws) == 1:
            add(ls[0], ws[0], how)

ls_ref, ws_ref = defaultdict(set), defaultdict(set)
for ea, o in L.items():
    for s in set(o["s"]): ls_ref[s].add(ea)
for ea, o in W.items():
    for s in set(o["s"]): ws_ref[s].add(ea)
votes = Counter()
for s, ls in ls_ref.items():
    ws = ws_ref.get(s)
    if ws and len(ls) == 1 and len(ws) == 1:
        votes[(next(iter(ls)), next(iter(ws)))] += 1
for (l, w), n in votes.most_common():
    add(l, w, "str1")

# ---- 2. vtables: Linux has class names, Windows (no RTTI) has scanned candidates ----
def load_vt(path):
    out = []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            p = line.rstrip("\n").split("\t")
            if len(p) >= 3 and p[2]:
                out.append((int(p[0], 16), p[1], [int(x, 16) for x in p[2].split(",")]))
    return out


LVT = []
for ea, cls, slots in load_vt(os.path.join(L_DIR, "vtables.tsv")):
    # Itanium has two adjacent destructor slots (complete + deleting), MSVC one (scalar deleting)
    col = []
    for e in slots:
        if col and e in L and col[-1] in L and "~" in L[e]["d"] and "~" in L[col[-1]]["d"]:
            continue
        col.append(e)
    LVT.append((ea, cls, col))
WVT = load_vt(os.path.join(W_DIR, "vtables_scan.tsv"))
l_slot_index = defaultdict(list)
for vi, (_, _, col) in enumerate(LVT):
    for i, e in enumerate(col):
        l_slot_index[e].append((vi, i))
vt_names = {}


def vtable_pass():
    added = 0
    for wea, _, ws in WVT:
        votes = Counter()
        for i, w in enumerate(ws):
            l = match_w2l.get(w)
            for vi, li in l_slot_index.get(l, ()):
                if li == i:
                    votes[vi] += 1
        if not votes:
            continue
        (vi, n), *rest = votes.most_common(2) + [(None, 0)]
        if n < 2 or (rest and rest[0][1] == n):
            continue
        lea, cls, col = LVT[vi]
        if len(ws) != len(col) and n < 4:
            continue
        vt_names[wea] = cls
        for le, we in zip(col, ws):
            if le in L and we in W and "~" not in L[le]["d"]:
                added += add(le, we, "vtable")
    return added


# ---- 3. call-graph propagation to a fixpoint (vtables re-aligned each round) ----
def lcs_anchor_pairs(lc, wc):
    """Between aligned matched callees, pair the single unmatched callee on each side."""
    ltok = [match_l2w.get(x, ("u", x)) for x in lc]
    wtok = [x if x in match_w2l else ("u", x) for x in wc]
    n, m = len(ltok), len(wtok)
    if n * m > 250000:
        return []
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            dp[i][j] = dp[i + 1][j + 1] + 1 if (ltok[i] == wtok[j] and not isinstance(ltok[i], tuple)) else max(dp[i + 1][j], dp[i][j + 1])
    anchors, i, j = [(-1, -1)], 0, 0
    while i < n and j < m:
        if ltok[i] == wtok[j] and not isinstance(ltok[i], tuple):
            anchors.append((i, j)); i += 1; j += 1
        elif dp[i + 1][j] >= dp[i][j + 1]:
            i += 1
        else:
            j += 1
    anchors.append((n, m))
    pairs = []
    for (a0, b0), (a1, b1) in zip(anchors, anchors[1:]):
        lu = {lc[k] for k in range(a0 + 1, a1) if lc[k] not in match_l2w and lc[k] in L}
        wu = {wc[k] for k in range(b0 + 1, b1) if wc[k] not in match_w2l and wc[k] in W}
        if len(lu) == 1 and len(wu) == 1:
            pairs.append((lu.pop(), wu.pop()))
    return pairs


callers_l, callers_w = defaultdict(set), defaultdict(set)
for ea, o in L.items():
    for c in o["c"]: callers_l[c].add(ea)
for ea, o in W.items():
    for c in o["c"] + o.get("f", []): callers_w[c].add(ea)
for ea, o in L.items():
    for c in o.get("f", []): callers_l[c].add(ea)

def neighbour_pass(min_score=2.2, margin=0.6):
    matched_l = set(match_l2w)
    props = []
    for w, wo in W.items():
        if w in match_w2l:
            continue
        cands = set()
        for c in wo["c"]:
            if c in match_w2l:
                cands |= callers_l[match_w2l[c]]
        for c in callers_w[w]:
            if c in match_w2l:
                cands |= set(L[match_w2l[c]]["c"]) | set(L[match_w2l[c]].get("f", []))
        cands = [l for l in cands if l not in match_l2w and l in L]
        if not cands or len(cands) > 400:
            continue
        mc_w = {match_w2l[x] for x in wo["c"] if x in match_w2l}
        mr_w = {match_w2l[x] for x in callers_w[w] if x in match_w2l}
        scored = []
        for l in cands:
            if not plausible(l, w):
                continue
            lc = set(L[l]["c"]) & matched_l
            lr = callers_l[l] & matched_l
            sc = similarity(l, w)
            if mc_w or lc:
                sc += 3.0 * len(mc_w & lc) / len(mc_w | lc)
            if mr_w or lr:
                sc += 2.0 * len(mr_w & lr) / len(mr_w | lr)
            scored.append((sc, l))
        if not scored:
            continue
        scored.sort(reverse=True)
        best, l = scored[0]
        second = scored[1][0] if len(scored) > 1 else 0.0
        if best >= min_score and best - second >= margin:
            props.append((best, l, w))
    props.sort(reverse=True)
    n = 0
    for sc, l, w in props:
        n += add(l, w, "neighbour", sc)
    return n


def tokens(o, side):
    t = {("s", x) for x in o["s"]} | {("k", x) for x in o["k"]} | {("o", x) for x in o.get("o", ()) if x >= 0x40}
    if side == "w":
        t |= {("m", match_w2l[c]) for c in o["c"] + o.get("f", []) if c in match_w2l}
        t |= {("r", match_w2l[c]) for c in callers_w[o["ea"]] if c in match_w2l}
    else:
        t |= {("m", c) for c in o["c"] + o.get("f", []) if c in match_l2w}
        t |= {("r", c) for c in callers_l[o["ea"]] if c in match_l2w}
    return t


def token_pass(min_score=2.6, margin=0.8, max_df=6):
    """Global pass: candidates share rare tokens (strings, constants, member offsets, matched callees/callers)."""
    lt = {ea: tokens(o, "l") for ea, o in L.items() if ea not in match_l2w}
    wt = {ea: tokens(o, "w") for ea, o in W.items() if ea not in match_w2l}
    dfl, dfw = Counter(), Counter()
    for t in lt.values(): dfl.update(t)
    for t in wt.values(): dfw.update(t)
    inv = defaultdict(list)
    for ea, t in lt.items():
        for x in t:
            if dfl[x] <= max_df and dfw.get(x, 0) and dfw[x] <= max_df:
                inv[x].append(ea)
    props = []
    for w, t in wt.items():
        votes = Counter()
        for x in t:
            for l in inv.get(x, ()):
                votes[l] += 1
        if not votes:
            continue
        scored = []
        for l, v in votes.most_common(25):
            if not plausible(l, w):
                continue
            lt_, wt_ = lt[l], t
            sc = similarity(l, w) + 3.0 * len(lt_ & wt_) / max(1, len(lt_ | wt_))
            scored.append((sc, l))
        if not scored:
            continue
        scored.sort(reverse=True)
        if scored[0][0] >= min_score and (len(scored) == 1 or scored[0][0] - scored[1][0] >= margin):
            props.append((scored[0][0], scored[0][1], w))
    props.sort(reverse=True)
    n = 0
    for sc, l, w in props:
        n += add(l, w, "token", sc)
    return n


L_SORTED, W_SORTED = sorted(L), sorted(W)
import bisect


def sandwich_pass(min_sim=1.2, margin=0.5):
    """Same source and link order: unmatched functions between two consecutive matched pairs line up."""
    pairs = sorted(match_w2l.items())
    n = 0
    for (w1, l1), (w2, l2) in zip(pairs, pairs[1:]):
        if not (l1 < l2):
            continue
        wi, wj = bisect.bisect_right(W_SORTED, w1), bisect.bisect_left(W_SORTED, w2)
        li, lj = bisect.bisect_right(L_SORTED, l1), bisect.bisect_left(L_SORTED, l2)
        wg = [w for w in W_SORTED[wi:wj] if w not in match_w2l]
        lg = [l for l in L_SORTED[li:lj] if l not in match_l2w]
        if not wg or not lg or len(wg) > 40 or len(lg) > 80:
            continue
        if len(wg) == len(lg):
            ok = [(l, w) for l, w in zip(lg, wg) if plausible(l, w) and similarity(l, w) >= 0.6]
            if len(ok) == len(wg):
                for l, w in ok:
                    n += add(l, w, "sandwich")
                continue
        for w in wg:
            sc = sorted(((similarity(l, w), l) for l in lg if l not in match_l2w and plausible(l, w)), reverse=True)
            if sc and sc[0][0] >= min_sim and (len(sc) == 1 or sc[0][0] - sc[1][0] >= margin):
                n += add(sc[0][1], w, "sandwich")
    return n


added_dia_partial = False
for it in range(80):
    added = vtable_pass()
    if not added_dia_partial and it == 0:
        added += diaphora_pass(); added_dia_partial = True
    for l, w in list(match_l2w.items()):
        for lp, wp in lcs_anchor_pairs(L[l]["c"], W[w]["c"]):
            added += add(lp, wp, "callee")
        lf, wf = L[l].get("f", []), W[w].get("f", [])
        if lf and wf:
            if len(lf) == len(wf):  # same table of function pointers (tolua bindings, dispatch tables)
                for lp, wp in zip(lf, wf):
                    if lp in L and wp in W:
                        added += add(lp, wp, "fptr")
            else:
                for lp, wp in lcs_anchor_pairs(lf, wf):
                    added += add(lp, wp, "fptr")
        lu = [x for x in callers_l[l] if x not in match_l2w]
        wu = [x for x in callers_w[w] if x not in match_w2l]
        if len(lu) == 1 and len(wu) == 1:
            added += add(lu[0], wu[0], "caller")
    if not added:
        added += neighbour_pass()
    if not added:
        added += sandwich_pass()
    if not added:
        added += token_pass()
    print("round", it, "added", added, "total", len(match_w2l), flush=True)
    if not added:
        break

with open(os.path.join(W_DIR, "vtables_win.tsv"), "w", encoding="utf-8") as f:
    f.write("win_vtable_ea\tclass\tslots\n")
    for wea, _, ws in WVT:
        f.write("%X\t%s\t%s\n" % (wea, vt_names.get(wea, "?"), ",".join("%X" % x for x in ws)))

# ---- cross-validation: trusted Diaphora candidates vs the custom-only run ----
cp = os.path.join(W_DIR, "names_win_custom.tsv")
if dia_ok and os.path.exists(cp):
    cust = {int(r.split("	")[0], 16): int(r.split("	")[3], 16) for r in list(open(cp, encoding="utf-8"))[1:]}
    ag = sum(1 for r, l, w, d in dia_ok if cust.get(w) == l)
    dg = sum(1 for r, l, w, d in dia_ok if w in cust and cust[w] != l)
    print("trusted diaphora vs custom-only: agree %d, disagree %d, new %d" % (ag, dg, len(dia_ok) - ag - dg))

# ---- output ----
with open(os.path.join(W_DIR, "names_win_custom.tsv" if os.getenv("NO_DIAPHORA") else "names_win.tsv"), "w", encoding="utf-8") as f:
    f.write("win_ea\tmethod\tscore\tlinux_ea\tlinux_name\tlinux_demangled\n")
    for w in sorted(match_w2l):
        l = match_w2l[w]
        f.write("%X\t%s\t%.2f\t%X\t%s\t%s\n" % (w, method[w], score[w], l, L[l]["n"], L[l]["d"]))

named_l = sum(1 for o in L.values() if named(o))
rep = ["Linux funcs %d (named %d), Windows funcs %d" % (len(L), named_l, len(W)),
       "Matched %d (%.1f%% of Windows)" % (len(match_w2l), 100 * len(match_w2l) / len(W)),
       "Matched named %d (%.1f%% of named Linux)" % (sum(1 for l in match_l2w if named(L[l])),
                                                    100 * sum(1 for l in match_l2w if named(L[l])) / max(1, named_l))]
rep += ["  %-12s %d" % kv for kv in Counter(method.values()).most_common()]
open(os.path.join(W_DIR, "match_report.txt"), "w").write("\n".join(rep) + "\n")
print("\n".join(rep))
