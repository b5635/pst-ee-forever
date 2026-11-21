# SPDX-License-Identifier: GPL-2.0-or-later
"""Read the engine's crash dumps (no debugger needed): exception, registers, symbolized stack.

  python -I tools/crash.py                 -> newest dump in Documents\\Infinity Engine - Enhanced Edition\\crash
  python -I tools/crash.py DUMP [--all]    -> one dump; --all also lists every thread's top frames

Torment.exe addresses are named from resources/re/win/dump/funcs.txt (+ manual_names.tsv);
pst_forever.dll addresses from build/Release/pst_forever.map (linker /MAP).
The stack walk is a scan: every qword on the stack that points into code is listed (callers show
up in order; a few stale values may appear too).
"""
import bisect
import glob
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRASH_DIR = os.path.join(os.path.expanduser("~"), "Documents", "Infinity Engine - Enhanced Edition", "crash")
CODES = {0xC0000005: "ACCESS_VIOLATION", 0xC0000094: "INT_DIVIDE_BY_ZERO", 0xC00000FD: "STACK_OVERFLOW",
         0x80000003: "BREAKPOINT", 0xC0000409: "STACK_BUFFER_OVERRUN (fast fail)", 0xC000001D: "ILLEGAL_INSTRUCTION",
         0xE06D7363: "C++ exception"}
REGS = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


class Dump:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        sig, _, n, rva = struct.unpack_from("<4sIII", self.d, 0)
        if sig != b"MDMP":
            raise SystemExit("not a minidump")
        self.streams = {}
        for i in range(n):
            t, size, off = struct.unpack_from("<III", self.d, rva + 12 * i)
            self.streams[t] = (off, size)
        self.mem = []  # (start, size, file offset)
        if 9 in self.streams:
            off, _ = self.streams[9]
            cnt, base = struct.unpack_from("<QQ", self.d, off)
            for i in range(cnt):
                s, z = struct.unpack_from("<QQ", self.d, off + 16 + 16 * i)
                self.mem.append((s, z, base))
                base += z
        if 5 in self.streams:
            off, _ = self.streams[5]
            (cnt,) = struct.unpack_from("<I", self.d, off)
            for i in range(cnt):
                s, z, r = struct.unpack_from("<QII", self.d, off + 4 + 16 * i)
                self.mem.append((s, z, r))
        self.mem.sort()
        self.mstarts = [m[0] for m in self.mem]

    def read(self, va, n):
        i = bisect.bisect_right(self.mstarts, va) - 1
        if i < 0:
            return None
        s, z, f = self.mem[i]
        if va + n > s + z:
            return None
        return self.d[f + va - s:f + va - s + n]

    def string(self, rva):
        (n,) = struct.unpack_from("<I", self.d, rva)
        return self.d[rva + 4:rva + 4 + n].decode("utf-16-le")

    def modules(self):
        off, _ = self.streams[4]
        (cnt,) = struct.unpack_from("<I", self.d, off)
        out = []
        for i in range(cnt):
            base, size, _, _, name = struct.unpack_from("<QIIII", self.d, off + 4 + 108 * i)
            out.append((base, size, os.path.basename(self.string(name))))
        return out

    def context(self, rva):
        regs = dict(zip(REGS, struct.unpack_from("<16Q", self.d, rva + 0x78)))
        regs["rip"] = struct.unpack_from("<Q", self.d, rva + 0xF8)[0]
        return regs

    def threads(self):
        off, _ = self.streams[3]
        (cnt,) = struct.unpack_from("<I", self.d, off)
        for i in range(cnt):
            tid, _, _, _, _, sstart, ssize, srva, csize, crva = struct.unpack_from("<IIIIQQIIII", self.d, off + 4 + 48 * i)
            yield tid, sstart, ssize, crva


class Symbols:
    def __init__(self, mods):
        self.mods = mods
        self.exe = []
        names = {}
        with open(os.path.join(ROOT, "resources/re/win/dump/funcs.txt"), encoding="utf8", errors="replace") as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 3:
                    names[int(p[0], 16)] = (int(p[1]), p[2])
        man = os.path.join(ROOT, "resources/re/win/manual_names.tsv")
        if os.path.exists(man):
            for line in open(man, encoding="utf8"):
                p = line.rstrip("\n").split("\t")
                if len(p) >= 2 and p[0].strip():
                    try:
                        ea = int(p[0], 16)
                    except ValueError:
                        continue
                    size = names.get(ea, (0, ""))[0]
                    names[ea] = (size, p[1])
        self.exe = sorted((ea, sz, n) for ea, (sz, n) in names.items())
        self.exe_starts = [e[0] for e in self.exe]
        self.dll = []
        mp = os.path.join(ROOT, "build/Release/pst_forever.map")
        if os.path.exists(mp):
            for line in open(mp, encoding="utf8", errors="replace"):
                p = line.split()
                # " 0001:00001234       ?Name@@...     0000000180001234 f   obj.obj"
                if len(p) >= 4 and ":" in p[0] and len(p[2]) == 16:
                    try:
                        self.dll.append((int(p[2], 16), p[1]))
                    except ValueError:
                        pass
            self.dll.sort()
        self.dll_starts = [d[0] for d in self.dll]
        self.dll_pref = 0x180000000

    def module(self, va):
        for base, size, name in self.mods:
            if base <= va < base + size:
                return base, name
        return None, None

    def name(self, va):
        base, mod = self.module(va)
        if mod is None:
            return None
        if mod.lower() == "torment.exe":
            i = bisect.bisect_right(self.exe_starts, va) - 1
            if i >= 0:
                ea, sz, n = self.exe[i]
                if sz == 0 or va < ea + sz + 16:
                    return "Torment!%s+0x%X" % (n, va - ea)
            return "Torment+0x%X" % (va - base)
        if mod.lower() == "pst_forever.dll" and self.dll:
            pref = self.dll_pref + (va - base)
            i = bisect.bisect_right(self.dll_starts, pref) - 1
            if i >= 0:
                return "pst_forever!%s+0x%X" % (self.dll[i][1], pref - self.dll[i][0])
        return "%s+0x%X" % (mod, va - base)


def scan(dump, sym, rsp, size, limit):
    out = []
    data = dump.read(rsp, size) or b""
    for i in range(0, len(data) - 7, 8):
        (v,) = struct.unpack_from("<Q", data, i)
        n = sym.name(v)
        if n and not n.startswith(("ntdll", "KERNEL", "kernel", "ucrt", "VCRUNTIME", "msvcp")):
            out.append((rsp + i, v, n))
        elif n and len(out) < limit:
            out.append((rsp + i, v, n))
        if len(out) >= limit:
            break
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else max(glob.glob(os.path.join(CRASH_DIR, "*.dmp")), key=os.path.getmtime)
    dump = Dump(path)
    mods = dump.modules()
    sym = Symbols(mods)
    print("dump:", path)
    for base, size, name in mods:
        if name.lower() in ("torment.exe", "pst_forever.dll", "version.dll"):
            print("  module %-16s %X..%X" % (name, base, base + size))
    if 6 not in dump.streams:
        print("no exception stream")
        return
    off, _ = dump.streams[6]
    tid = struct.unpack_from("<I", dump.d, off)[0]
    code, flags, _, addr, nparams = struct.unpack_from("<IIQQI", dump.d, off + 8)
    params = struct.unpack_from("<%dQ" % min(nparams, 15), dump.d, off + 8 + 32) if nparams else ()
    crva = struct.unpack_from("<I", dump.d, off + 8 + 152 + 4)[0]
    print("exception %08X %s at %X  %s  (thread %d)" % (code, CODES.get(code, ""), addr, sym.name(addr) or "?", tid))
    if code == 0xC0000005 and len(params) >= 2:
        print("  %s address %X" % (["read", "write", "?", "?", "?", "?", "?", "?", "execute"][params[0]], params[1]))
    ctx = dump.context(crva)
    print("  " + "  ".join("%s=%X" % (r, ctx[r]) for r in ["rip", "rsp", "rax", "rbx", "rcx", "rdx"]))
    print("  " + "  ".join("%s=%X" % (r, ctx[r]) for r in ["rsi", "rdi", "rbp", "r8", "r9", "r10"]))
    print("  " + "  ".join("%s=%X" % (r, ctx[r]) for r in ["r11", "r12", "r13", "r14", "r15"]))
    print("stack (scan from rsp, code pointers):")
    for at, v, n in scan(dump, sym, ctx["rsp"], 0x2000, 40):
        print("  [%X] %X %s" % (at, v, n))
    if "--all" in sys.argv:
        for t, sstart, ssize, crv in dump.threads():
            c = dump.context(crv)
            print("thread %d rip %X %s" % (t, c["rip"], sym.name(c["rip"]) or "?"))
            for at, v, n in scan(dump, sym, c["rsp"], 0x800, 6):
                print("    %X %s" % (v, n))


if __name__ == "__main__":
    main()
