# SPDX-License-Identifier: GPL-2.0-or-later
"""8-member party: generate src/engine/party_sites.h (every Torment.exe instruction that addresses the
CInfGame party arrays, retargeted to the arrays' new home, plus the hand-reviewed patches).

  python -I tools/party_sites.py            write the header, print a summary
  python -I tools/party_sites.py --list     every site (VA, function, instruction, old -> new)
  python -I tools/party_sites.py --check    exit 1 if the header on disk is stale
  python -I tools/party_sites.py --imm VA NEW "why" [VA NEW "why" ...]
                                            print party_patches.tsv lines that set the immediate of the
                                            instruction at VA to NEW (e.g. cmp cx, 6 -> 8)

CInfGame (lin CInfGame::AddCharacterToParty 375A70 / win 1402D0A70): +0xA1E8 i32[6] fixed-order slots,
+0xA200 i32[6] party order, +0xA218 i16 count. The allocation (CBaldurChitin::Init win 140141255
`mov ecx, 0D140h`) grows by 0x40; the arrays move to +0xD140 (i32[8]) and +0xD160 (i32[8]). Every
memory displacement or immediate in [0xA1E8, 0xA218) in the dumped Windows code is a CInfGame array
reference (checked by hand: tools/party_refs.py --list and the review notes in docs/PARTY8_PLAN.md);
the generator rewrites its 4-byte field in place. An end-of-fixed pointer (0xA200) maps to the new
order array, which is also the new end of the fixed array, so pointer loops stay consistent.
Hand-reviewed bound patches (6 -> 8, 5 -> 7, ...) live in tools/party_patches.tsv.
Needs: pip install capstone. Reads resources/re/win/dump/all_Torment.asm line by line.
"""
import re
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

ROOT = Path(__file__).resolve().parent.parent
ASM = ROOT / "resources" / "re" / "win" / "dump" / "all_Torment.asm"
EXE = Path(r"C:\GOG Games\Planescape Torment - Enhanced Edition\Torment.exe")
OUT = ROOT / "src" / "engine" / "party_sites.h"
PATCHES = ROOT / "tools" / "party_patches.tsv"
BASE = 0x140000000

OLD_FIXED, OLD_ORDER, OLD_END = 0xA1E8, 0xA200, 0xA218
NEW_FIXED, NEW_ORDER, NEW_SIZE = 0xD140, 0xD160, 0xD180
OLD_SIZE = 0xD140


def new_offset(v):
    if OLD_FIXED <= v < OLD_ORDER:
        return NEW_FIXED + (v - OLD_FIXED)
    if OLD_ORDER <= v < OLD_END:
        return NEW_ORDER + (v - OLD_ORDER)
    return None


class Exe:
    def __init__(self, path):
        self.d = path.read_bytes()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        n = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        self.secs = [struct.unpack_from("<8sIIII", self.d, pe + 24 + opt + 40 * i)[1:] for i in range(n)]

    def at(self, va, n):
        rva = va - BASE
        for vsize, sva, rsize, raw in self.secs:
            if sva <= rva < sva + rsize:
                return self.d[raw + rva - sva: raw + rva - sva + n]
        return None


def scan(exe):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    txt = re.compile(r"\b0?A(1[E-F]|20|21)[0-9A-F]h\b")
    sites = []
    fn = ""
    for line in open(ASM, encoding="latin-1"):
        if line.startswith("//@@"):
            fn = line[5:].strip()
            continue
        if not txt.search(line):
            continue
        va = int(line[:9], 16)
        b = exe.at(va, 16)
        ins = next(md.disasm(b, va, 1))
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base != x86.X86_REG_RIP:
                v, off, size = op.mem.disp & 0xFFFFFFFF, ins.disp_offset, ins.disp_size
            elif op.type == x86.X86_OP_IMM:
                v, off, size = op.imm & 0xFFFFFFFF, ins.imm_offset, ins.imm_size
            else:
                continue
            nv = new_offset(v)
            if nv is None:
                continue
            if size != 4:
                raise SystemExit(f"{va:X}: {ins.mnemonic} {ins.op_str}: field is {size} bytes")
            if struct.unpack_from("<I", ins.bytes, off)[0] != v:
                raise SystemExit(f"{va:X}: field mismatch")
            sites.append((va, bytes(ins.bytes), off, v, nv, fn, f"{ins.mnemonic} {ins.op_str}"))
    return sites


def load_patches(exe):
    out = []
    for n, line in enumerate(PATCHES.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 4:
            raise SystemExit(f"party_patches.tsv:{n}: want VA<TAB>old<TAB>new<TAB>why")
        va = int(cols[0], 16)
        old = bytes.fromhex(cols[1])
        new = bytes.fromhex(cols[2])
        if len(old) != len(new) or not old:
            raise SystemExit(f"party_patches.tsv:{n}: old/new lengths differ")
        if exe.at(va, len(old)) != old:
            raise SystemExit(f"party_patches.tsv:{n}: {va:X} on-disk bytes {exe.at(va, len(old)).hex()} != {old.hex()}")
        out.append((va, old, new, cols[3]))
    return out


def cbytes(b):
    return "{" + ", ".join(f"0x{x:02X}" for x in b) + "}"


def render(sites, patches):
    o = ["// SPDX-License-Identifier: GPL-2.0-or-later",
         "// GENERATED by tools/party_sites.py from the Windows dump + tools/party_patches.tsv. Do not edit.",
         "// 8-member party: every instruction that addresses the CInfGame party arrays (retargeted to",
         "// their new home past the old end of the object) and the hand-reviewed bound patches.",
         "#pragma once", "#include <cstdint>", "", "namespace pstf::sym::party {", "",
         f"constexpr uint32_t kOldSize = 0x{OLD_SIZE:X}, kNewSize = 0x{NEW_SIZE:X};",
         f"constexpr uint32_t kOldFixed = 0x{OLD_FIXED:X}, kOldOrder = 0x{OLD_ORDER:X}, kCount = 0x{OLD_END:X};",
         f"constexpr uint32_t kNewFixed = 0x{NEW_FIXED:X}, kNewOrder = 0x{NEW_ORDER:X};",
         "constexpr int kMax = 8;", "",
         "// rva, instruction length, offset of the 4-byte field, old and new field value, instruction bytes.",
         "struct Site { uint32_t rva; uint8_t len; uint8_t field; uint32_t old_val, new_val; uint8_t bytes[15]; };",
         "inline constexpr Site kSites[] = {"]
    for va, b, off, v, nv, fn, text in sites:
        o.append(f"    {{0x{va - BASE:X}, {len(b)}, {off}, 0x{v:X}, 0x{nv:X}, {cbytes(b)}}},  // {fn.split(' ', 1)[-1][:40]}: {text}")
    o += ["};", "",
          "// Hand-reviewed patches (tools/party_patches.tsv): rva, length, on-disk bytes, new bytes.",
          "struct Patch { uint32_t rva; uint8_t len; uint8_t old_bytes[96]; uint8_t new_bytes[96]; const char* why; };",
          "inline constexpr Patch kPatches[] = {"]
    for va, old, new, why in patches:
        w = why.replace("\\", "\\\\").replace('"', '\\"')
        o.append(f"    {{0x{va - BASE:X}, {len(old)}, {cbytes(old)}, {cbytes(new)}, \"{w}\"}},")
    if not patches:
        o.append("    {0, 0, {0}, {0}, nullptr},")
    o += ["};", "", "}  // namespace pstf::sym::party", ""]
    return "\n".join(o)


def imm_lines(exe, args):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for k in range(0, len(args), 3):
        va, new, why = int(args[k], 16), int(args[k + 1], 0), args[k + 2]
        ins = next(md.disasm(exe.at(va, 16), va, 1))
        if not ins.imm_size:
            raise SystemExit(f"{va:X}: {ins.mnemonic} {ins.op_str}: no immediate")
        b = bytearray(ins.bytes)
        b[ins.imm_offset:ins.imm_offset + ins.imm_size] = (new & ((1 << 8 * ins.imm_size) - 1)).to_bytes(ins.imm_size, "little")
        print(f"{va:X}	{bytes(ins.bytes).hex(' ').upper()}	{bytes(b).hex(' ').upper()}	{why} ({ins.mnemonic} {ins.op_str} -> {new})")


def main(argv):
    exe = Exe(EXE)
    if argv and argv[0] == "--imm":
        imm_lines(exe, argv[1:])
        return 0
    sites = scan(exe)
    patches = load_patches(exe)
    # A patch that covers a whole retargeted instruction supersedes it (its new bytes carry the new
    # field); a partial overlap is an error.
    drop = set()
    for va, old, new, why in patches:
        for s in sites:
            a, z = s[0], s[0] + len(s[1])
            if va < z and a < va + len(old):
                if not (va <= a and z <= va + len(old)):
                    raise SystemExit(f"patch {va:X} ({why}) partly overlaps the retargeted instruction at {a:X}")
                drop.add(a)
    sites = [s for s in sites if s[0] not in drop]
    if "--list" in argv:
        for va, b, off, v, nv, fn, text in sites:
            print(f"{va:X} {fn[:60]:60} {text:45} {v:X}->{nv:X}")
        return 0
    text = render(sites, patches)
    if "--check" in argv:
        if OUT.read_text(encoding="utf-8") != text:
            print("party_sites.h is stale: run python -I tools/party_sites.py")
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8", newline="\n")
    fns = {s[5] for s in sites}
    print(f"{len(sites)} sites in {len(fns)} functions, {len(patches)} patches -> {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
