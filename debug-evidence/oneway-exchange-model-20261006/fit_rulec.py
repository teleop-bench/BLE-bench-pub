exec(open('fit_model.py').read().split("cells=lambda")[0])
def n_C(T,txseq,rep,g,m):
    # start exchange k only if its own TX + a max-length reply + 2 gaps ends >= m before the next event
    tt=0.0;n=0;k=0
    while True:
        tx=txseq[k%len(txseq)]
        if tt+tx+air(251)+2*g > T-m: break
        tt+=tx+rep(k)+2*g; n+=1; k+=1
    return n
def pred1(key,m):
    t,pol,st,u,arm=key.split('|'); T=int(u)*1250.0; g=gap(st,arm,t); seq,pay=packets(t)
    rep=(lambda k:CREDIT) if (t=='coc' and pol=='seg') else (lambda k:EMPTY)
    return n_C(T,seq,rep,g,m)*pay/(T/1e6)/1024
sdc=[k for k in D if k.split('|')[2]=='sdc']
res=[]
for m in range(0,1501,2):
    res.append((sum(1 for k in sdc if abs(pred1(k,m)/D[k]-1)<0.025),m))
top=max(r[0] for r in res); ms=[m for ok,m in res if ok==top]; m0=ms[len(ms)//2]
print(f'SDC one-way, rule C: {top}/{len(sdc)} cells within 2.5% for m in [{ms[0]},{ms[-1]}]; using m={m0}')
# duplex (independent test): GATT echo (244+244 B per exchange, both 251-B LL payloads) and CoC duplex (downlink 480-B SDU
# packets 251/239 B, uplink 244-B SDU = 250-B LL payload), SDC, measured aggregates
dup={('gatt',6,'off'):187.0,('gatt',6,'on'):187.6,('gatt',12,'off'):187.5,('gatt',12,'on'):187.1,('gatt',20,'off'):185.9,('gatt',20,'on'):206.0,
     ('coc',6,'off'):183.9,('coc',6,'on'):183.9,('coc',12,'off'):184.3,('coc',12,'on'):189.3,('coc',20,'off'):186.3,('coc',20,'on'):205.4}
for (t,u,arm),meas in dup.items():
    T=u*1250.0; g=gap('sdc',arm,t)
    if t=='gatt': seq=[air(251)]; rep=lambda k: air(251); pay=488
    else: seq=[air(251),air(239)]; rep=lambda k: air(250); pay=240+244
    n=n_C(T,seq,rep,g,m0); p=n*pay/(T/1e6)/1024
    nA=0; tt=0.0
    while tt+seq[nA%len(seq)]+rep(0)+2*g <= T-1288: tt+=seq[nA%len(seq)]+rep(0)+2*g; nA+=1
    print(f'  SDC duplex {t:4s} {u*1.25:5.1f} ms {arm:3s}: measured {meas:6.1f}  rule C n={n} -> {p:6.1f} ({100*(meas/p-1):+5.1f}%)   rule A(1288) n={nA} -> {nA*pay/(T/1e6)/1024:6.1f}')
