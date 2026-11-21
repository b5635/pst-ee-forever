# SPDX-License-Identifier: GPL-2.0-or-later
# IDA batch (Windows, no RTTI; x86 or x64): find vtable candidates = runs of >=2 function-start pointers in
# data segments whose first slot has a data xref from code (mov [reg], offset vtbl). -> dump/vtables_scan.tsv
import idautils, idc, ida_auto, ida_funcs, ida_bytes, ida_segment, ida_ida, os
ida_auto.auto_wait()
PTR = 8 if ida_ida.inf_is_64bit() else 4
rd = ida_bytes.get_qword if PTR == 8 else ida_bytes.get_dword
base = os.path.dirname(idc.get_idb_path())
out = open(os.path.join(base, "dump", "vtables_scan.tsv"), "w")
is_fstart = lambda t: (ida_funcs.get_func(t) is not None and ida_funcs.get_func(t).start_ea == t)
for s in idautils.Segments():
    seg = ida_segment.getseg(s)
    if seg.perm & ida_segment.SEGPERM_EXEC:
        continue
    ea, end = seg.start_ea, seg.end_ea
    while ea < end - 2 * PTR:
        if is_fstart(rd(ea)) and any(ida_funcs.get_func(x) for x in idautils.DataRefsTo(ea)):
            slots, p = [], ea
            while p < end and is_fstart(rd(p)):
                if slots and any(ida_funcs.get_func(x) for x in idautils.DataRefsTo(p)):
                    break  # next vtable referenced separately
                slots.append(rd(p)); p += PTR
            if len(slots) >= 2:
                refs = sorted({ida_funcs.get_func(x).start_ea for x in idautils.DataRefsTo(ea) if ida_funcs.get_func(x)})
                out.write("%X\t?\t%s\t%s\n" % (ea, ",".join("%X" % x for x in slots), ",".join("%X" % x for x in refs[:16])))
                ea = p
                continue
        ea += PTR
out.close()
idc.qexit(0)
