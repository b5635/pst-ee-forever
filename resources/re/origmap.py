import re,collections,sys
size={l.split()[1]:int(l.split()[2]) for l in open('funcs.txt')}
by=collections.defaultdict(set)
for l in open('strings.txt',encoding='utf-8'):
    p=l.rstrip('\n').split('\t')
    if len(p)<3: continue
    m=re.search(r'([A-Za-z0-9_]+)[.]cpp$',p[1])
    if m:
        for f in p[2].split(','):
            if f: by[m.group(1)].add(f)
with open('srcmap.txt','w') as o:
    for k in sorted(by):
        o.write("%s\t%s\n"%(k,",".join(sorted(by[k], key=lambda f:-size.get(f,0)))))
for k in sorted(by, key=lambda k:-max(size.get(f,0) for f in by[k]))[:30]:
    big=sorted(by[k], key=lambda f:-size.get(f,0))[:3]
    print(k, len(by[k]), [(f,size.get(f)) for f in big])
