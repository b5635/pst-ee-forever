# SPDX-License-Identifier: GPL-2.0-or-later
"""Print raw bytes of the on-disk Torment.exe at a VA (for symbol-manifest entries).

  python tools/exe_bytes.py 0x14022627A 5            -> hex bytes
  python tools/exe_bytes.py 0x14022627A 5 --rel32    -> also decode a call/jmp/mov rel32 target
  python tools/exe_bytes.py 0x14071F5FC 4 --u32      -> little-endian dword (static .data value)
"""
import argparse
import struct

EXE = r"C:\GOG Games\Planescape Torment - Enhanced Edition\Torment.exe"
BASE = 0x140000000


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    off = pe + 24 + opt
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", d, off + 40 * i)
        yield name.rstrip(b"\0").decode(), va, vsize, raw, rsize


def read(d, va, n):
    rva = va - BASE
    for name, sva, vsize, raw, rsize in sections(d):
        if sva <= rva < sva + max(vsize, rsize):
            o = raw + rva - sva
            if rva - sva + n > rsize:
                return name, None  # bss part: zero-initialised at runtime
            return name, d[o:o + n]
    raise SystemExit(f"VA {va:X} not in image")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("va")
    ap.add_argument("n", type=int)
    ap.add_argument("--rel32", action="store_true", help="rel32 = last 4 bytes, target = va+n+rel")
    ap.add_argument("--u32", action="store_true")
    ap.add_argument("--exe", default=EXE)
    a = ap.parse_args()
    d = open(a.exe, "rb").read()
    va = int(a.va, 16)
    sec, b = read(d, va, a.n)
    if b is None:
        print(f"{va:X} [{sec}] uninitialised (bss)")
        return
    print(f"{va:X} [{sec}] rva {va - BASE:X}: " + " ".join(f"{x:02X}" for x in b))
    print("  C++: {" + ", ".join(f"0x{x:02X}" for x in b) + "}")
    if a.rel32:
        rel = struct.unpack_from("<i", b, a.n - 4)[0]
        print(f"  rel32 target {va + a.n + rel:X}")
    if a.u32:
        print(f"  u32 {struct.unpack_from('<I', b, 0)[0]}")


if __name__ == "__main__":
    main()
