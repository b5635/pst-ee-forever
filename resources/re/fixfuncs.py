# SPDX-License-Identifier: GPL-2.0-or-later
# IDA batch: recover functions IDA left as loose code. For every run of code heads in executable segments that
# belongs to no function, try add_func at the run start; if that fails, try to append the run as a chunk of the
# functions that jump or fall into it. Repeats until nothing changes. Writes dump/fixfuncs.json.
import idautils, idc, ida_auto, ida_funcs, ida_bytes, ida_segment, ida_xref, json, os

ida_auto.auto_wait()
base = os.path.dirname(idc.get_idb_path())
os.makedirs(os.path.join(base, "dump"), exist_ok=True)
execs = [s for s in idautils.Segments() if ida_segment.getseg(s).perm & ida_segment.SEGPERM_EXEC]


def orphan_runs():
    runs, start, prev = [], None, None
    for sg in execs:
        for h in idautils.Heads(sg, idc.get_segm_end(sg)):
            loose = ida_bytes.is_code(ida_bytes.get_flags(h)) and not ida_funcs.get_func(h)
            if loose and start is None:
                start = h
            elif not loose and start is not None:
                runs.append((start, h)); start = None
        if start is not None:
            runs.append((start, idc.get_segm_end(sg))); start = None
    return runs


before = sum(1 for _ in orphan_runs())
n0 = ida_funcs.get_func_qty()
created = appended = 0
for rnd in range(30):
    changed = 0
    for s, e in orphan_runs():
        if ida_funcs.get_func(s):
            continue
        if ida_funcs.add_func(s):
            created += 1; changed += 1
            continue
        # tail chunk: owned by every function whose code jumps here (shared tails are legal in IDA),
        # or by the function whose last instruction falls through into it
        owners = {ida_funcs.get_func(x).start_ea for x in idautils.CodeRefsTo(s, 0) if ida_funcs.get_func(x)}
        prev = idc.prev_head(s)
        if not owners and ida_funcs.get_func(prev) and ida_bytes.is_flow(ida_bytes.get_flags(s)):
            owners = {ida_funcs.get_func(prev).start_ea}
        done = False
        for o in owners:
            done |= bool(ida_funcs.append_func_tail(ida_funcs.get_func(o), s, e))
        if done:
            appended += 1; changed += 1
    ida_auto.auto_wait()
    if not changed:
        break
left = orphan_runs()
rep = {"orphan_runs_before": before, "functions_created": created, "tails_appended": appended,
       "orphan_runs_after": len(left), "orphan_heads_after": sum(sum(1 for _ in idautils.Heads(s, e)) for s, e in left),
       "functions_before": n0, "functions_after": ida_funcs.get_func_qty(),
       "left_sample": ["%X-%X" % r for r in left[:40]]}
json.dump(rep, open(os.path.join(base, "dump", "fixfuncs.json"), "w"), indent=2)
print(json.dumps(rep))
idc.save_database(idc.get_idb_path(), 0)
idc.qexit(0)
