import re, sys, glob, os, statistics
exec(open(os.path.join(os.path.dirname(sys.argv[0]),'retx.py')).read().split("for p in sorted")[0])
for p in sorted(glob.glob(sys.argv[1]+'/*-obs.log')):
    R=recs(p); ev=[]; cur=[]
    for r in R:
        if cur and ((r[1]-cur[-1][2])&0xFFFFFFFF)*TICK>4000: ev.append(cur); cur=[]
        cur.append(r)
    if cur: ev.append(cur)
    span=((R[-1][2]-R[0][1])&0xFFFFFFFF)*TICK/1e6
    dur=[((e[-1][2]-e[0][1])&0xFFFFFFFF)*TICK for e in ev if len(e)>=10]
    starts=[e[0][1] for e in ev]
    d=[((b-a)&0xFFFFFFFF)*TICK/1000 for a,b in zip(starts,starts[1:])]   # ms between consecutive channel-10 events
    u=25 if 'coc20' in p else 15
    k=[round(x/u) for x in d]
    print(f"{os.path.basename(p)[:20]:20s} events {len(ev):4d} in {span:5.1f}s = {len(ev)/span:5.2f}/s | event duration median {statistics.median(dur) if dur else 0:7.0f} us max {max(dur) if dur else 0:7.0f} | spacing in intervals: median {statistics.median(k) if k else 0}")
