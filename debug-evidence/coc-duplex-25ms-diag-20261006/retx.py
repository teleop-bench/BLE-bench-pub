import re, sys, glob, os, collections, statistics
TICK=1/16.0
def recs(p):
    out=[]
    for l in open(p,errors='ignore'):
        l=re.sub(r'^HOSTMS \d+ ','',l)
        if l.startswith('REC '):
            d=dict(re.findall(r'(\w+)=(-?\w+)',l))
            try: out.append((int(d['oseq']),int(d['addr']),int(d['end']),int(d['len']),int(d['crc']),int(d['s0'],16)))
            except: pass
    return sorted(out)
for p in sorted(glob.glob(sys.argv[1]+'/*-obs.log')):
    R=recs(p); ev=[]; cur=[]
    for r in R:
        if cur and ((r[1]-cur[-1][2])&0xFFFFFFFF)*TICK>4000: ev.append(cur); cur=[]
        cur.append(r)
    if cur: ev.append(cur)
    allg=[((b[1]-a[2])&0xFFFFFFFF)*TICK for e in ev for a,b in zip(e,e[1:])]; med=statistics.median(allg); gate=1.5*med+22
    comp=[e for e in ev if len(e)>=2 and all(((b[1]-a[2])&0xFFFFFFFF)*TICK<=gate for a,b in zip(e,e[1:]))]
    st=collections.Counter()
    for e in comp:
        body=e[:-1] if e[-1][4]!=1 else e
        for side in (0,1):
            pk=[x for i,x in enumerate(body) if i%2==side]
            for a,b in zip(pk,pk[1:]):
                sn_a=(a[5]>>3)&1; sn_b=(b[5]>>3)&1
                key='C' if side==0 else 'P'
                st[key+'_pkts']+=1
                if sn_a==sn_b: st[key+'_retx']+=1
                if b[3]==0: st[key+'_empty']+=1
            # NESN of the other side not acking: count packets whose NESN didn't advance relative to peer SN
        st['events']+=1; st['crcbad']+=sum(1 for x in e if x[4]!=1)
    tag=os.path.basename(p).replace('-obs.log','')
    f=lambda k: f"{st[k+'_retx']}/{st[k+'_pkts']} retx ({100*st[k+'_retx']/max(1,st[k+'_pkts']):.1f}%), empty {st[k+'_empty']}"
    print(f"{tag:22s} events {st['events']:3d}  crc-bad {st['crcbad']:3d} | central {f('C')} | periph {f('P')}")
