import re, sys, glob, os, collections, statistics
exec(open(os.path.join(os.path.dirname(sys.argv[0]),'retx.py')).read().split("for p in sorted")[0])
for arm in ('coc20_off','coc20_on','coc12_off','coc12_on'):
    H=collections.Counter(); ends=collections.Counter(); gaps_short=[]
    for p in sorted(glob.glob(sys.argv[1]+f'/onair{arm}_*-obs.log')):
        R=recs(p); ev=[]; cur=[]
        for r in R:
            if cur and ((r[1]-cur[-1][2])&0xFFFFFFFF)*TICK>4000: ev.append(cur); cur=[]
            cur.append(r)
        if cur: ev.append(cur)
        for e in ev:
            if len(e)>24 or len(e)<1: continue          # merged consecutive same-channel events
            good=[x for x in e if x[4]==1]
            ex=len(e)//2
            H[ex]+=1
            if ex<9 and 'coc20' in arm:
                last=e[-1]; md=[(x[5]>>4)&1 for x in e[-3:]]
                ig=[round(((b[1]-a[2])&0xFFFFFFFF)*TICK) for a,b in zip(e,e[1:])]
                ends[f"lastCRC={'ok' if last[4]==1 else 'bad'} MD(last3)={md} maxgap={max(ig) if ig else 0}"]+=1
    print(arm, 'exchanges/event histogram:', dict(sorted(H.items())), ' mean', round(sum(k*v for k,v in H.items())/sum(H.values()),2))
    for k,v in ends.most_common(8): print('   short-event end:', v, k)
