# SPDX-License-Identifier: GPL-2.0-or-later
# IDA batch: apply ported names (dump/names_win.tsv, dump/vtables_win.tsv) if present, then a
# COMPLETE dump of the database into dump/:
#   funcs.txt      EA<TAB>size<TAB>demangled name
#   names.txt      EA<TAB>name<TAB>demangled              (every named address, data included)
#   strings.txt    EA<TAB>text<TAB>referencing functions (all)
#   calls.tsv      caller_ea<TAB>caller<TAB>callee_ea<TAB>callee<TAB>call|ptr
#   drefs.tsv      func_ea<TAB>func<TAB>target_ea<TAB>target<TAB>r|w|o   (globals/data used; strings excluded)
#   protos.tsv     EA<TAB>IDA prototype
#   imports.txt, exports.txt, segments.txt
#   all_<bin>.asm  every function's disassembly, separated by "//@@ <EA> <name>"
#   all_<bin>.c    every function's pseudocode, same separators (disassembly when Hex-Rays fails)
#   dec_all/<EA>.c (or <EA>.asm.c on failure), manifest.tsv, errors.tsv, report.json
#   dec_all_named/<demangled>.c   (named functions only)
#   verify.json    completeness proof: counts, exec-segment coverage, code outside functions
# Works on any IDB (Linux/Windows, x86/x64).
import idautils, idc, ida_auto, ida_funcs, ida_hexrays, ida_nalt, ida_name, ida_bytes, ida_ua, ida_xref
import ida_entry, ida_segment, ida_lines, json, os, re, time

ida_auto.auto_wait()
base = os.path.dirname(idc.get_idb_path())
out = os.path.join(base, "dump"); os.makedirs(out, exist_ok=True)
binname = os.path.basename(idc.get_idb_path()).split(".")[0]
DN = idc.get_inf_attr(idc.INF_SHORT_DN)
dem = lambda n: (idc.demangle_name(n, DN) or n) if n else n
dis = lambda h: ida_lines.tag_remove(idc.generate_disasm_line(h, 0) or "")
W = lambda name: open(os.path.join(out, name), "w", encoding="utf-8")

# ---------- 1. apply ported names (Windows) ----------
applied = 0
p = os.path.join(out, "names_win.tsv")
OURS = "ported from "
if os.path.exists(p):
    # drop names an earlier run ported, so a re-match never leaves stale names behind
    for ea in idautils.Functions():
        if OURS in (idc.get_func_cmt(ea, 1) or ""):
            ida_name.set_name(ea, "", ida_name.SN_NOWARN); idc.set_func_cmt(ea, "", 1)
    for line in list(open(p, encoding="utf-8"))[1:]:
        w, how, sc, lea, lname, ldem = line.rstrip("\n").split("\t")
        ea = int(w, 16)
        if re.match(r"^(sub_|nullsub_|j_|unknown_libname)", lname):
            continue
        cur = idc.get_func_name(ea)
        if cur and not cur.startswith("sub_"):
            continue  # keep IDA/FLIRT library names
        # plain demangled name without args: a mangled GCC name makes IDA derive a wrong MSVC prototype
        nm = re.sub(r"[^A-Za-z0-9_:~<>,]", "_", ldem.split("(")[0].lstrip(".")).strip("_") or lname
        cand, k = nm, 2
        while idc.get_name_ea_simple(cand) not in (idc.BADADDR, ea):
            cand = "%s__%d" % (nm, k); k += 1  # overloads
        if ida_name.set_name(ea, cand, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
            idc.set_func_cmt(ea, "ported from Linux Torment %s (%s, score %s): %s" % (lea, how, sc, ldem), 1)
            applied += 1
p = os.path.join(out, "vtables_win.tsv")
if os.path.exists(p):
    for line in list(open(p, encoding="utf-8"))[1:]:
        ea, cls, _ = line.rstrip("\n").split("\t")
        if cls != "?":
            ida_name.set_name(int(ea, 16), "vtbl_" + re.sub(r"[^A-Za-z0-9_]", "_", cls),
                              ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE)
print("applied names:", applied)

# ---------- 2. tables ----------
funcs = sorted(ida_funcs.getn_func(i).start_ea for i in range(ida_funcs.get_func_qty()))  # all, incl. extern/plt
with W("funcs.txt") as f:
    for ea in funcs:
        f.write("%X\t%d\t%s\n" % (ea, ida_funcs.get_func(ea).size(), dem(idc.get_func_name(ea))))
with W("names.txt") as f:
    for ea, n in idautils.Names():
        f.write("%X\t%s\t%s\n" % (ea, n, dem(n)))
strs = set()
with W("strings.txt") as f:
    for s in idautils.Strings():
        try: txt = str(s)
        except Exception: continue
        strs.add(s.ea)
        refs = sorted({dem(idc.get_func_name(x.frm)) or ("%X" % x.frm) for x in idautils.XrefsTo(s.ea)})
        f.write("%X\t%s\t%s\n" % (s.ea, txt.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t"), ",".join(refs)))
kinds = {ida_xref.dr_R: "r", ida_xref.dr_W: "w", ida_xref.dr_O: "o"}
insn = ida_ua.insn_t()
with W("calls.tsv") as fc, W("drefs.tsv") as fd, W("protos.tsv") as fp:
    fc.write("caller_ea\tcaller\tcallee_ea\tcallee\tkind\n")
    fd.write("func_ea\tfunc\ttarget_ea\ttarget\tkind\n")
    for ea in funcs:
        cn = dem(idc.get_func_name(ea)); seen_c = set(); seen_d = set()
        fp.write("%X\t%s\n" % (ea, idc.get_type(ea) or ""))
        for h in idautils.FuncItems(ea):
            is_call = ida_ua.decode_insn(insn, h) and insn.get_canon_mnem() == "call"
            for x in idautils.XrefsFrom(h, 0):
                t = x.to; tf = ida_funcs.get_func(t)
                if tf and tf.start_ea == t:
                    kind = "call" if is_call else ("ptr" if not x.iscode else None)
                    if kind and (t != ea or is_call) and (t, kind) not in seen_c:
                        seen_c.add((t, kind))
                        fc.write("%X\t%s\t%X\t%s\t%s\n" % (ea, cn, t, dem(idc.get_func_name(t)), kind))
                elif not x.iscode and t < (1 << 63) and t not in strs and x.type in kinds and (t, kinds[x.type]) not in seen_d:
                    seen_d.add((t, kinds[x.type]))
                    fd.write("%X\t%s\t%X\t%s\t%s\n" % (ea, cn, t, dem(idc.get_name(t)) or "", kinds[x.type]))
with W("imports.txt") as f:
    for i in range(ida_nalt.get_import_module_qty()):
        mod = ida_nalt.get_import_module_name(i)
        def cb(ea, name, ordn, mod=mod):
            f.write("%X\t%s\t%s\n" % (ea, mod, name or "#%d" % ordn)); return True
        ida_nalt.enum_import_names(i, cb)
with W("exports.txt") as f:
    for i in range(ida_entry.get_entry_qty()):
        o = ida_entry.get_entry_ordinal(i)
        f.write("%X\t%d\t%s\n" % (ida_entry.get_entry(o), o, ida_entry.get_entry_name(o)))
with W("segments.txt") as f:
    for sg in idautils.Segments():
        f.write("%X\t%X\t%s\n" % (sg, idc.get_segm_end(sg), idc.get_segm_name(sg)))

# ---------- 3. disassembly ----------
with W("all_%s.asm" % binname) as fa:
    for ea in funcs:
        fa.write("//@@ %08X %s\n" % (ea, dem(idc.get_func_name(ea))))
        for h in idautils.FuncItems(ea):
            fa.write("%X  %s\n" % (h, dis(h)))
print("tables + asm done")

# ---------- 4. decompile everything ----------
if not ida_hexrays.init_hexrays_plugin():
    raise RuntimeError("Hex-Rays decompiler unavailable")
d1 = os.path.join(out, "dec_all"); d2 = os.path.join(out, "dec_all_named")
for d in (d1, d2):
    os.makedirs(d, exist_ok=True)
    for x in os.listdir(d):
        os.remove(os.path.join(d, x))  # no stale files from an earlier run
man = open(os.path.join(d1, "manifest.tsv"), "w", encoding="utf-8"); man.write("Address\tName\tSize\tStatus\n")
err = open(os.path.join(d1, "errors.tsv"), "w", encoding="utf-8"); err.write("Address\tName\tError\n")
allc = W("all_%s.c" % binname)
ok = bad = 0; t0 = time.time(); used = set()
for i, ea in enumerate(funcs, 1):
    n = idc.get_func_name(ea); d = dem(n); sz = ida_funcs.get_func(ea).size()
    try:
        hf = ida_hexrays.hexrays_failure_t()
        cf = ida_hexrays.decompile(ea, hf)
        if cf is None: raise RuntimeError("Hex-Rays: %s (at %X)" % (hf.desc() or hf.str, hf.errea))
        code = "// Address: %08X\n// Function: %s\n\n%s" % (ea, d, str(cf))
        fname = "%08X.c" % ea; ok += 1; status = "OK"
    except Exception as e:
        msg = str(e).replace("\n", " ")
        code = "// Address: %08X\n// Function: %s\n// HEX-RAYS FAILED: %s\n// disassembly follows\n%s\n" % (
            ea, d, msg, "\n".join("%X  %s" % (h, dis(h)) for h in idautils.FuncItems(ea)))
        fname = "%08X.asm.c" % ea; bad += 1; status = "FAILED"
        err.write("%08X\t%s\t%s\n" % (ea, d, msg))
    open(os.path.join(d1, fname), "w", encoding="utf-8").write(code)
    allc.write("//@@ %08X %s\n%s\n" % (ea, d, code))
    if not re.match(r"^(sub_|nullsub_|j_)", n):
        fn = re.sub(r"[^A-Za-z0-9_]+", "_", d)[:120]
        if fn in used: fn += "_%X" % ea
        used.add(fn)
        open(os.path.join(d2, fn + ".c"), "w", encoding="utf-8").write(code)
    man.write("%08X\t%s\t%d\t%s\n" % (ea, d, sz, status))
    if i % 2000 == 0:
        print("%d/%d ok=%d failed=%d %.0fs" % (i, len(funcs), ok, bad, time.time() - t0)); man.flush(); allc.flush()
man.close(); err.close(); allc.close()
rep = {"binary": binname, "total_functions": len(funcs), "success": ok, "failed": bad,
       "names_applied": applied, "elapsed_seconds": round(time.time() - t0), "complete": ok + bad == len(funcs)}
json.dump(rep, open(os.path.join(d1, "report.json"), "w"), indent=2)
print(json.dumps(rep))

# ---------- 5. completeness proof ----------
execs = [sg for sg in idautils.Segments() if ida_segment.getseg(sg).perm & ida_segment.SEGPERM_EXEC]
exec_bytes = sum(idc.get_segm_end(sg) - sg for sg in execs)
in_funcs = sum(ida_funcs.get_func(ea).size() for ea in funcs)
orphan = [h for sg in execs for h in idautils.Heads(sg, idc.get_segm_end(sg))
          if ida_bytes.is_code(ida_bytes.get_flags(h)) and not ida_funcs.get_func(h)]
# loose code IDA could not turn into functions (after fixfuncs.py): dumped too, so nothing is missing
with W("orphans.asm") as fo:
    prev = None
    for h in orphan:
        if prev is None or idc.prev_head(h) != prev:
            fo.write("//@@ %08X orphan_code\n" % h)
        fo.write("%X  %s\n" % (h, dis(h))); prev = h
count = lambda fn: sum(1 for line in open(os.path.join(out, fn), encoding="utf-8") if line.startswith("//@@ "))
ver = {"binary": binname, "ida_function_count": ida_funcs.get_func_qty(), "listed_functions": len(funcs),
       "decompiled": ok, "hexrays_failed_with_disasm_fallback": bad,
       "all_c_entries": count("all_%s.c" % binname), "all_asm_entries": count("all_%s.asm" % binname),
       "dec_all_files": len([x for x in os.listdir(d1) if x.endswith(".c")]),
       "exec_segment_bytes": exec_bytes, "bytes_in_functions": in_funcs,
       "function_coverage_pct": round(100.0 * in_funcs / max(1, exec_bytes), 2),
       "code_heads_outside_functions": len(orphan), "orphan_heads_dumped": sum(1 for line in open(os.path.join(out, "orphans.asm"), encoding="utf-8") if not line.startswith("//@@ ")),
       "orphan_sample": ["%X" % h for h in orphan[:50]],
       "names_applied": applied}
ver["complete"] = (ver["ida_function_count"] == ver["listed_functions"] == ver["all_c_entries"]
                   == ver["all_asm_entries"] == ver["dec_all_files"]
                   and ver["orphan_heads_dumped"] == ver["code_heads_outside_functions"])
json.dump(ver, open(os.path.join(out, "verify.json"), "w"), indent=2)
print(json.dumps(ver))
idc.save_database(idc.get_idb_path(), 0)
idc.qexit(0)
