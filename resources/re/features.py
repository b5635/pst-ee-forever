# SPDX-License-Identifier: GPL-2.0-or-later
# IDA batch: per-function features for cross-binary matching -> dump/features.jsonl, dump/vtables.tsv
# Works on both the Linux x64 IDB and the Windows x86 IDB.
import idautils, idc, ida_auto, ida_funcs, ida_bytes, ida_gdl, ida_ida, ida_ua, ida_hexrays, json, os

ida_auto.auto_wait()
base = os.path.dirname(idc.get_idb_path())
out = os.path.join(base, "dump"); os.makedirs(out, exist_ok=True)
is64 = ida_ida.inf_is_64bit()
PTR = 8 if is64 else 4
rdp = ida_bytes.get_qword if is64 else ida_bytes.get_dword
DN = idc.get_inf_attr(idc.INF_SHORT_DN)

def dem(n):
    return idc.demangle_name(n, DN) or n

strs = {}
for s in idautils.Strings():
    try: strs[s.ea] = str(s)
    except Exception: pass

lo, hi = ida_ida.inf_get_min_ea(), ida_ida.inf_get_max_ea()

def callee_name(t):
    n = idc.get_name(t) or ""
    return dem(n)

with open(os.path.join(out, "features.jsonl"), "w", encoding="utf-8") as f:
    for ea in idautils.Functions():
        fn = ida_funcs.get_func(ea)
        name = idc.get_func_name(ea)
        calls, imps, slots, srefs, consts, fps, offs = [], [], [], [], [], [], []
        insn = ida_ua.insn_t()
        for h in idautils.FuncItems(ea):
            if not ida_ua.decode_insn(insn, h):
                continue
            mn = insn.get_canon_mnem()
            if mn == "call":
                op = insn.ops[0]
                if op.type in (idc.o_near, idc.o_far):
                    t = op.addr
                    tf = ida_funcs.get_func(t)
                    if tf and (tf.flags & ida_funcs.FUNC_THUNK):
                        imps.append(callee_name(t))
                    calls.append(t)
                elif op.type == idc.o_mem:  # call [__imp_X]
                    imps.append(callee_name(op.addr))
                elif op.type == idc.o_displ:  # call [reg+off] -> vtable slot
                    slots.append(op.addr // PTR)
                elif op.type == idc.o_phrase:
                    slots.append(0)
            for x in idautils.DataRefsFrom(h):
                if x in strs:
                    srefs.append(strs[x])
                else:
                    xf = ida_funcs.get_func(x)
                    if xf and xf.start_ea == x and mn != "call":
                        fps.append(x)
            for i in range(3):
                op = insn.ops[i]
                if op.type == idc.o_void: break
                if op.type == idc.o_displ and op.phrase not in (4, 5):  # [reg+disp], not rsp/rbp/esp/ebp frames
                    dsp = op.addr if op.addr < (1 << 63) else op.addr - (1 << 64)
                    if 8 <= dsp < 0x10000:
                        offs.append(dsp)
                if op.type == idc.o_imm:
                    v = op.value & 0xFFFFFFFF
                    if 0x100 < v < 0xFFFFFF00 and not (lo <= v < hi):
                        consts.append(v)
        try: nbb = ida_gdl.FlowChart(fn).size
        except Exception: nbb = 0
        f.write(json.dumps({"ea": ea, "n": name, "d": dem(name), "sz": fn.size(), "bb": nbb,
                            "c": calls, "f": fps, "i": [i.lstrip(".").replace("__imp_", "") for i in imps], "v": slots, "s": srefs, "k": consts, "o": offs},
                           ensure_ascii=False) + "\n")

# vtables: Itanium "vtable for X" (slots start at +2*PTR) or MSVC "X::`vftable'" (slots start at +0)
with open(os.path.join(out, "vtables.tsv"), "w", encoding="utf-8") as f:
    for ea, n in idautils.Names():
        d = dem(n)
        if d.startswith("`vtable for'"):
            cls, p = d[len("`vtable for'"):], ea + 2 * PTR
        elif "`vftable'" in d:
            cls, p = d.replace("const ", "").split("::`vftable'")[0], ea
        else:
            continue
        slots = []
        while len(slots) < 2000:
            t = rdp(p)
            if not ida_funcs.get_func(t) or ida_funcs.get_func(t).start_ea != t:
                if slots: break
                if t == 0: slots.append(0); p += PTR; continue
                break
            if slots and idc.get_name(p) and idc.get_name(p) != n and p != ea:
                break  # next vtable starts
            slots.append(t); p += PTR
        f.write("%X\t%s\t%s\n" % (ea, cls, ",".join("%X" % s for s in slots)))

hr = ida_hexrays.init_hexrays_plugin()
open(os.path.join(out, "hexrays.txt"), "w").write(str(hr))
idc.qexit(0)
