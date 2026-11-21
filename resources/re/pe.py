import struct
class PE:
    def __init__(s, path):
        s.d = open(path, "rb").read(); d = s.d
        pe, = struct.unpack_from("<I", d, 0x3C); ns, = struct.unpack_from("<H", d, pe + 6); opt, = struct.unpack_from("<H", d, pe + 20)
        s.base, = struct.unpack_from("<I", d, pe + 24 + 28); s.secs = []
        so = pe + 24 + opt
        for i in range(ns):
            name, vs, va, rs, ra = struct.unpack_from("<8sIIII", d, so + i*40); s.secs.append((va, max(vs, rs), ra))
    def off(s, va):
        rva = va - s.base
        for v, sz, ra in s.secs:
            if v <= rva < v + sz: return rva - v + ra
    def bytes(s, va, n): o = s.off(va); return s.d[o:o+n]
