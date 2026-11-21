# Minimal Infinity Engine resource reader for comparing PST data sets (original, EE, GemRB unhardcoded).
import os, struct, glob, zlib

EXT2T = {"bmp":1,"mve":2,"wav":4,"wfx":5,"plt":6,"bam":1000,"wed":1001,"chu":1002,"tis":1003,"mos":1004,
	"itm":1005,"spl":1006,"bcs":1007,"ids":1008,"cre":1009,"are":1010,"dlg":1011,"2da":1012,"gam":1013,
	"sto":1014,"wmp":1015,"eff":1016,"bs":1017,"chr":1018,"vvc":1019,"vef":1020,"pro":1021,"bio":1022,
	"wbm":1023,"fnt":1024,"gui":1026,"sql":1027,"pvrz":1028,"glsl":1029,"menu":1032,"lua":1033,"ttf":1034,"png":1035,
	"ini":2050,"src":1035}
T2EXT = {v:k for k,v in EXT2T.items()}

class Game:
	def __init__(self, path, tlk=None, override=True, extra=()):
		self.path = path
		key = None
		for n in ("chitin.key","CHITIN.KEY"):
			if os.path.exists(os.path.join(path,n)): key = os.path.join(path,n)
		k = open(key,"rb").read()
		nbif, nres, obif, ores = struct.unpack_from("<IIII", k, 8)
		self.bifs = []
		for i in range(nbif):
			ln, fo, fl, loc = struct.unpack_from("<IIHH", k, obif + i*12)
			self.bifs.append(k[fo:fo+fl].rstrip(b"\0").decode().replace("\\", "/"))
		self.index = {}
		for i in range(nres):
			rn, rt, loc = struct.unpack_from("<8sHI", k, ores + i*14)
			self.index[(rn.rstrip(b"\0").decode("latin1").upper(), rt)] = loc
		self.dirs = list(extra) + ([os.path.join(path,"override")] if override else [])
		self.dirfiles = {}
		for d in self.dirs:
			m = {}
			if os.path.isdir(d):
				for f in os.listdir(d):
					m[f.upper()] = os.path.join(d,f)
			self.dirfiles[d] = m
		self.bifcache = {}
		tlk = tlk or os.path.join(path,"dialog.tlk")
		self.tlk = open(tlk,"rb").read() if os.path.exists(tlk) else None

	def _bif(self, i):
		if i in self.bifcache: return self.bifcache[i]
		bp = self.bifs[i]
		cands = [os.path.join(self.path, bp)] + glob.glob(os.path.join(self.path, "**", os.path.basename(bp)), recursive=True)
		data = None
		for c in cands:
			if os.path.exists(c):
				data = open(c,"rb").read(); break
		if data and data[:8] == b"BIFCV1.0":
			ulen, = struct.unpack_from("<I", data, 8+4+len(data)) if False else (0,)
			nlen, = struct.unpack_from("<I", data, 8)
			o = 12 + nlen
			ulen, clen = struct.unpack_from("<II", data, o)
			data = zlib.decompress(data[o+8:o+8+clen])
		elif data and data[:8] == b"BIF V1.0":
			o = 12; out = b""
			while o < len(data):
				ul, cl = struct.unpack_from("<II", data, o); out += zlib.decompress(data[o+8:o+8+cl]); o += 8+cl
			data = out
		self.bifcache[i] = data
		return data

	def names(self, ext, with_override=True):
		t = EXT2T[ext]
		s = {n for (n,rt) in self.index if rt == t}
		if with_override:
			for d,m in self.dirfiles.items():
				for f in m:
					if f.endswith("." + ext.upper()): s.add(f[:-len(ext)-1])
		return sorted(s)

	def where(self, name, ext):
		fn = (name + "." + ext).upper()
		for d in self.dirs:
			if fn in self.dirfiles[d]: return d
		if (name.upper(), EXT2T[ext]) in self.index: return "bif"
		return None

	def res(self, name, ext, bif_only=False):
		fn = (name + "." + ext).upper()
		if not bif_only:
			for d in self.dirs:
				p = self.dirfiles[d].get(fn)
				if p: return open(p,"rb").read()
		loc = self.index.get((name.upper(), EXT2T[ext]))
		if loc is None: return None
		b = self._bif(loc >> 20)
		if not b: return None
		nf, nt, off = struct.unpack_from("<III", b, 8)
		if ext in ("tis",):
			toff = off + nf*16
			for j in range(nt):
				l,o,c,s,t = struct.unpack_from("<IIIIH", b, toff + j*20)
				if (l >> 14) == ((loc >> 14) & 0x3F): return b[o:o+c*s]
			return None
		for j in range(nf):
			l,o,s,t = struct.unpack_from("<IIIH", b, off + j*16)
			if (l & 0x3FFF) == (loc & 0x3FFF): return b[o:o+s]
		return None

	def string(self, ref):
		if not self.tlk or ref < 0 or ref == 0xFFFFFFFF: return ""
		cnt, toff = struct.unpack_from("<II", self.tlk, 0x0A)
		if ref >= cnt: return ""
		fl, snd, v, p, o, l = struct.unpack_from("<H8sIIII", self.tlk, 0x12 + ref*26)
		return self.tlk[toff+o:toff+o+l].decode("cp1252","replace")

def text(data):
	"""2DA/IDS text, decrypting 0xFFFF-prefixed files"""
	if data is None: return None
	if data[:2] == b"\xff\xff":
		key = b"88a88fa974e8ada31b6d5a2c8d58a96e5a2c8d58d6a5f4e5bd9e7b2adb3c5da0"
		key = bytes.fromhex("88A88FBA8AD3B9F5EDB1CFEAAAE4B5FBEB82F990CAC9B5E7DCAAE4B5FB8E8BF2")
		data = bytes(c ^ key[i % len(key)] for i,c in enumerate(data[2:]))
	return data.decode("latin1")

ORIG = r"C:\Program Files (x86)\Black Isle\Torment"
EE = r"D:\GOG Games\Planescape Torment - Enhanced Edition"
GEMRB_UNHARD = r"D:\gemrb\gemrb\unhardcoded\pst"
GEMRB_SHARED = r"D:\gemrb\gemrb\unhardcoded\shared"

def orig(): return Game(ORIG)
def ee(): return Game(EE, tlk=os.path.join(EE,"lang","en_US","dialog.tlk"))
def gemrb():
	"""what GemRB sees: its unhardcoded data first, then the original game's override+bifs"""
	return Game(ORIG, extra=(GEMRB_UNHARD, GEMRB_SHARED))
