# SPDX-License-Identifier: GPL-2.0-or-later
"""BCS (compiled script) decompiler and creature-script audit for PST:EE (override first, then BIFs, via ieres.py).

  python -I tools/bcs.py dump NAME [NAME..]     decompiled script text (BAF-like; objects as Myself/[EA=2]/"name")
  python -I tools/bcs.py users NAME             which CREs / area actors run it (CRE slot or ARE actor slot)
  python -I tools/bcs.py who CRE                a CRE's scripts, EA, name; where it is placed (area actor slots)
  python -I tools/bcs.py grep REGEX             script blocks (one line each) matching REGEX over every creature script
  python -I tools/bcs.py audit                  turn-based risks (docs/SCENARIO_TEST_PLAN.md): responses whose script
                                                state sits behind a Wait (dropped at turn end), Wait-first casters,
                                                local-timer casters, hostile fleers
Objects: PST:EE stores EA FACTION TEAM GENERAL RACE CLASS SPECIFIC + 5 OBJECT.IDS ids (innermost first) + name.
INSTANT.IDS actions run at once; others take engine updates (a turn-based AI turn ends at the first Wait after its
action and ClearAI drops the rest: that is what `audit` looks for). Index cached in build/bcs_index.json.
"""
import importlib.util, json, os, re, struct, sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location("ieres", os.path.join(_ROOT, "tools", "ieres.py"))
ieres = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ieres)


def ids(name):
  m = {}
  for ln in ieres.read(name, "IDS").decode("latin1").splitlines()[1:]:
    p = ln.strip().split(None, 1)
    if len(p) == 2:
      try:
        m.setdefault(int(p[0], 0), p[1])
      except ValueError:
        pass
  return m


TRG, ACT, OBJ = ids("TRIGGER"), ids("ACTION"), ids("OBJECT")
INSTANT = {int(x, 0) for x in ieres.read("INSTANT", "IDS").decode("latin1").split() if x[:1].isdigit()}
WAIT = {63, 83, 368, 85, 130, 200, 410, 411}
CAST = {31, 95, 113, 114, 181, 191, 192, 240, 318, 319, 337, 466}
STATE = {7, 8, 30, 60, 61, 109, 111, 115, 146, 153, 369, 392, 404, 405, 418, 433, 438}
COMBAT_T = {"AttackedBy", "See", "Help", "HPPercentLT", "HPLT", "Range", "Heard", "LOS"}
_TOK = re.compile(rb'"[^"]*"|-?\d+|[A-Z]{2}')


def _nm(sig):
  return sig.split("(")[0]


class _P:
  def __init__(s, b):
    s.t = []
    for m in _TOK.finditer(b):
      t = m.group()
      s.t.append(("s", t[1:-1].decode("latin1")) if t[:1] == b'"' else ("k", t.decode()) if t[:1].isalpha() else ("n", int(t)))
    s.i = 0

  def peek(s):
    return s.t[s.i] if s.i < len(s.t) else (None, None)

  def nx(s):
    s.i += 1
    return s.t[s.i - 1]

  def kw(s, k):
    v = s.nx()
    if v != ("k", k): raise ValueError("expected %s got %r at token %d" % (k, v, s.i))

  def obj(s):
    s.kw("OB")
    nums, strs = [], []
    while s.peek() != ("k", "OB"):
      k, v = s.nx()
      (nums if k == "n" else strs).append(v)
    s.nx()
    if strs and strs[0]: return '"%s"' % strs[0]
    r = ""
    for c in [OBJ.get(x, str(x)) for x in nums[7:12] if x]:
      r = c + "(" + r + ")" if r else c
    fil = ["%s=%d" % (l, x) for l, x in zip(["EA", "FAC", "TEAM", "GEN", "RACE", "CLASS", "SPEC"], nums[:7]) if x]
    if fil: r += "[" + ",".join(fil) + "]"
    return r or "-"

  def trig(s):
    s.kw("TR")
    vals, o = [], None
    while s.peek() != ("k", "TR"):
      if s.peek() == ("k", "OB"): o = s.obj()
      else: vals.append(s.nx()[1])
    s.nx()
    tid, i1, fl, i2 = vals[:4]
    a = [x for x in vals[5:] if isinstance(x, str) and x] + [str(i1)] + ([str(i2)] if i2 else [])
    if o and o != "-": a.insert(0, o)
    return ("!" if fl & 1 else "") + _nm(TRG.get(tid, "T%x" % tid)) + "(" + ",".join(a) + ")", tid

  def act(s):
    s.kw("AC")
    aid = s.nx()[1]
    os_, vals = [], []
    while s.peek() != ("k", "AC"):
      if s.peek() == ("k", "OB"): os_.append(s.obj())
      else: vals.append(s.nx()[1])
    s.nx()
    nums = [v for v in vals if isinstance(v, int)]
    a = [o for o in os_[1:3] if o != "-"] + ['"%s"' % x for x in vals if isinstance(x, str) and x]
    i1, x, y, i2, i3 = (nums + [0] * 5)[:5]
    if x or y: a.append("[%d.%d]" % (x, y))
    a += [str(v) for v in (i1, i2, i3) if v]
    pre = ("ActionOverride(%s," % os_[0]) if os_ and os_[0] != "-" else ""
    return pre + _nm(ACT.get(aid, "A%d" % aid)) + "(" + ",".join(a) + ")" + (")" if pre else ""), aid


def parse(b):
  """[(triggers [(text, id)], responses [(weight, [(text, action id)])])]"""
  p = _P(b)
  p.kw("SC")
  out = []
  while p.peek() == ("k", "CR"):
    p.nx(); p.kw("CO")
    trs = []
    while p.peek() == ("k", "TR"): trs.append(p.trig())
    p.kw("CO"); p.kw("RS")
    resp = []
    while p.peek() == ("k", "RE"):
      p.nx()
      w = p.nx()[1]
      al = []
      while p.peek() == ("k", "AC"): al.append(p.act())
      p.kw("RE")
      resp.append((w, al))
    p.kw("RS"); p.kw("CR")
    out.append((trs, resp))
  return out


def text(blocks):
  out = []
  for trs, resp in blocks:
    out += ["IF"] + ["  " + t for t, _ in trs] + ["THEN"]
    for w, al in resp:
      out += ["  RESPONSE #%d" % w] + ["    " + a for a, _ in al]
    out.append("END")
  return "\n".join(out)


def _names(ext):
  k = ieres.load_key()
  ov = os.listdir(os.path.join(ieres.GAME, "override"))
  return sorted({n for (n, t) in k if t == ext} | {f[:-4].upper() for f in ov if f.upper().endswith("." + ext)})


def _rs(d, o):
  return d[o:o + 8].split(b"\0")[0].decode("latin1").upper()


def index():
  """{cre: {...}, users: {script: [CRE | CRE@AREA]}} (cached; delete build/bcs_index.json after a mod changes)."""
  path = os.path.join(_ROOT, "build", "bcs_index.json")
  if os.path.exists(path): return json.load(open(path))
  cre, users = {}, {}
  for n in _names("CRE"):
    d = ieres.read(n, "CRE")
    if not d or d[:8] != b"CRE V1.0": continue
    sc = [_rs(d, 0x248 + 8 * i) for i in range(5)]
    cre[n] = dict(scripts=sc, ea=d[0x270], name=ieres.tlk(struct.unpack_from("<I", d, 8)[0]),
                  dv=d[0x280:0x2A0].split(b"\0")[0].decode("latin1"), dlg=_rs(d, 0x2CC))
    for s in sc:
      if s and s != "NONE": users.setdefault(s, []).append(n)
  for a in _names("ARE"):
    d = ieres.read(a, "ARE")
    if not d or d[:4] != b"AREA": continue
    off, cnt = struct.unpack_from("<IH", d, 0x54)
    for i in range(cnt):
      o = off + i * 0x110
      c = _rs(d, o + 0x80)
      for j in range(6):
        s = _rs(d, o + 0x50 + 8 * j)
        if s and s != "NONE": users.setdefault(s, []).append(c + "@" + a)
  # scripts reached through ChangeAIScript
  todo, seen = list(users), set()
  while todo:
    s = todo.pop()
    if s in seen: continue
    seen.add(s)
    b = ieres.read(s, "BCS")
    if not b: continue
    for m in re.finditer(rb'"([A-Za-z0-9_#]{1,8})"', b):
      pass
    try:
      for _, resp in parse(b):
        for _, al in resp:
          for t, aid in al:
            mm = re.search(r'ChangeAIScript\("([^"]+)"', t)
            if mm:
              n = mm.group(1).upper()
              users.setdefault(n, []).append("via " + s)
              todo.append(n)
    except ValueError:
      pass
  os.makedirs(os.path.dirname(path), exist_ok=True)
  json.dump(dict(cre=cre, users=users), open(path, "w"))
  return dict(cre=cre, users=users)


def _scripts():
  for s in sorted(index()["users"]):
    b = ieres.read(s, "BCS")
    if b:
      try:
        yield s, parse(b)
      except ValueError:
        pass


def audit():
  ix = index()
  def who(s):
    u = ix["users"].get(s, [])
    n = ix["cre"].get(u[0].split("@")[0], {}).get("name", "") if u else ""
    return ("%s %s" % (n, ",".join(u[:2])))[:40]
  lost, lead, timers = [], [], []
  for s, bl in _scripts():
    for trs, resp in bl:
      tn = {t.lstrip("!").split("(")[0] for t, _ in trs}
      cond = " & ".join(t for t, _ in trs)
      ids_all = {a for _, al in resp for _, a in al}
      if not (tn & COMBAT_T or ids_all & (CAST | {3, 400})): continue
      if ({"TimerActive", "TimerExpired"} & tn) and ids_all & CAST: timers.append((s, cond))
      for _, al in resp:
        a = [x for _, x in al]
        nonin = [i for i, x in enumerate(a) if x not in INSTANT]
        if not nonin: continue
        if a[nonin[0]] in WAIT and set(a[nonin[0]:]) & (CAST | {3, 400}):
          lead.append((s, cond, "; ".join(t for t, _ in al)))
        cut = next((i for i in nonin if a[i] in WAIT), None)
        if cut is not None:
          l = [al[j][0] for j in range(cut + 1, len(a)) if a[j] in STATE]
          if l: lost.append((s, cond, "; ".join(l)))
  print("== script state behind a Wait (an AI turn ending at the Wait drops it): %d" % len(lost))
  for s, c, l in lost: print("  %-9s %-40s %s  ==> %s" % (s, who(s), c[:80], l[:120]))
  print("== a Wait before the first spell/attack (the turn's nudge replaces it with an attack): %d" % len(lead))
  for s, c, l in lead: print("  %-9s %-40s %s  => %s" % (s, who(s), c[:70], l[:110]))
  print("== casts gated by a local script timer (counts only inside the creature's own turns): %d" % len(timers))
  for s, c in timers: print("  %-9s %-40s %s" % (s, who(s), c[:110]))


def main(a):
  if not a or a[0] in ("-h", "--help"): print(__doc__); return
  cmd = a[0]
  if cmd == "dump":
    for n in a[1:]:
      b = ieres.read(n, "BCS")
      if b is None: sys.exit("%s.BCS not found" % n)
      print("// %s.BCS" % n.upper()); print(text(parse(b)))
  elif cmd == "users":
    for u in index()["users"].get(a[1].upper(), []): print(u)
  elif cmd == "who":
    ix = index(); c = ix["cre"].get(a[1].upper())
    print(c)
    for s, us in ix["users"].items():
      for u in us:
        if u.split("@")[0] == a[1].upper() and "@" in u: print("  placed %s, script %s" % (u, s))
  elif cmd == "grep":
    rx = re.compile(a[1], re.I)
    for s, bl in _scripts():
      for trs, resp in bl:
        line = " & ".join(t for t, _ in trs) + " => " + " | ".join("; ".join(t for t, _ in al) for _, al in resp)
        if rx.search(line): print("%-9s %s" % (s, line[:220]))
  elif cmd == "audit":
    audit()
  else:
    print(__doc__)


if __name__ == "__main__":
  main(sys.argv[1:])
