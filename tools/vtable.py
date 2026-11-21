# SPDX-License-Identifier: GPL-2.0-or-later
"""Dump vtable slots with names, Linux (named) and Windows (ported names).

  python -I tools/vtable.py lin CGameSprite            Linux _ZTV<class>: slot, EA, name
  python -I tools/vtable.py win 0x1406220F8 [N]        Windows vtable at VA: slot, EA, ported name
  python -I tools/vtable.py find 0x140421830           Windows vtables (scan) that contain this function

GCC vtables have two destructor slots where MSVC has one (scalar deleting dtor), so the same
virtual usually sits one slot earlier on Windows after the destructors. Always confirm by content.
"""
import struct
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent / "resources" / "re"
LIN = R / "lin" / "Torment"
WIN = R / "win" / "Torment.exe"


def lin_names():
    n = {}
    for line in (R / "lin" / "dump" / "names.txt").read_text(errors="replace").splitlines():
        p = line.split("\t")
        if len(p) >= 3:
            n[int(p[0], 16)] = p[2]
    for line in (R / "lin" / "dump" / "funcs.txt").read_text(errors="replace").splitlines():
        p = line.split("\t")
        if len(p) >= 3:
            n.setdefault(int(p[0], 16), p[2])
    return n


def win_names():
    n = {}
    for line in (R / "win" / "dump" / "names_win.tsv").read_text(errors="replace").splitlines()[1:]:
        p = line.split("\t")
        n[int(p[0], 16)] = p[5] + f"  ({p[1]} {p[2]})"
    mn = R / "win" / "manual_names.tsv"
    for line in mn.read_text(errors="replace").splitlines():
        if line.startswith("#") or "\t" not in line:
            continue
        a, b = line.split("\t", 1)
        n[int(a, 16)] = b + "  (manual)"
    return n


def elf_reader(d):
    shoff = struct.unpack_from("<Q", d, 0x28)[0]
    shentsize, shnum = struct.unpack_from("<HH", d, 0x3A)
    secs = []
    for i in range(shnum):
        _, typ, _, addr, off, size = struct.unpack_from("<IIQQQQ", d, shoff + i * shentsize)
        if addr and typ != 8:
            secs.append((addr, off, size))

    def q(va):
        for a, o, s in secs:
            if a <= va < a + s:
                return struct.unpack_from("<Q", d, o + va - a)[0]
        return None
    return q


def pe_reader(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIII", d, pe + 24 + opt + 40 * i) for i in range(nsec)]

    def q(va):
        rva = va - 0x140000000
        for _, vs, sva, rs, raw in secs:
            if sva <= rva < sva + rs:
                return struct.unpack_from("<Q", d, raw + rva - sva)[0]
        return None
    return q


def main():
    mode = sys.argv[1]
    if mode == "lin":
        cls = sys.argv[2]
        names = lin_names()
        sym = f"_ZTV{len(cls)}{cls}"
        va = next((a for a, nm in names.items() if nm in (f"`vtable for'{cls}",)), None)
        if va is None:
            for line in (R / "lin" / "dump" / "names.txt").read_text(errors="replace").splitlines():
                p = line.split("\t")
                if len(p) >= 2 and p[1] == sym:
                    va = int(p[0], 16)
        q = elf_reader(LIN.read_bytes())
        print(f"{sym} at {va:X}; slots start at +0x10")
        for i in range(400):
            f = q(va + 16 + 8 * i)
            if not f or f < 0xE4000 or f > 0xC00000:
                break
            print(f"{i:3d} +{8*i:#05x} {f:X} {names.get(f, '?')}")
    elif mode == "win":
        va = int(sys.argv[2], 16)
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 400
        names = win_names()
        q = pe_reader(WIN.read_bytes())
        for i in range(n):
            f = q(va + 8 * i)
            if not f or not (0x140001000 <= f < 0x140600000):
                break
            print(f"{i:3d} +{8*i:#05x} {f:X} {names.get(f, '?')}")
    elif mode == "find":
        f = sys.argv[2].upper().replace("0X", "")
        for line in (R / "win" / "dump" / "vtables_scan.tsv").read_text().splitlines():
            if f in line:
                print(line.split("\t")[0])


if __name__ == "__main__":
    main()
