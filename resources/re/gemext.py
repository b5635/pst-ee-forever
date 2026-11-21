import json,struct,pstres,re,os
g=pstres.gemrb()
rows=json.load(open('prodiff.json')); beh=json.load(open('psbehav.json'))
def rr(d,o): return d[o:o+8].split(bytes(1))[0].decode('latin1').upper()
def spl(n):
    d=g.res(n,'spl')
    if not d or d[:4]!=b'SPL ': return None
    eho,ehc,fxo,cfi,cfc=struct.unpack_from('<IHIHH',d,0x64)
    out=[]
    for h in range(ehc):
        nfx,fi=struct.unpack_from('<HH',d,eho+h*0x28+0x1E)
        out.append([(struct.unpack_from('<H',d,fxo+(fi+i)*0x30)[0], rr(d,fxo+(fi+i)*0x30+0x14)) for i in range(nfx)])
    return (os.path.basename(g.where(n,'spl')), out[-1] if out else [])
res=[]
for r in rows:
    p=r['gemrb_pro']; d=g.res(p,'pro') if p else None
    if not d: res.append(dict(r, ext=None)); print(r['spl'],r['cls'],'NO PRO'); continue
    ext=[x for x in (rr(d,0x44),rr(d,0x4C)) if x]
    sp={s:spl(s) for s in ext}
    res.append(dict(r, ext=sp, ee_ops=[o for o,_ in beh[r['cls']]['opcodes']], ee_feats=beh[r['cls']]['features']))
    print(r['spl'], r['cls'][8:], p, sp, '| EE code ops:', [o for o,_ in beh[r['cls']]['opcodes']])
json.dump(res,open('gemext.json','w'),indent=1)
