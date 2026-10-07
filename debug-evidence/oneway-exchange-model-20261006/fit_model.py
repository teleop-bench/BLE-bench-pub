import json, itertools
D=json.load(open('fitdata.json'))
air=lambda payload: (11+payload)*4.0          # 2M: (preamble 2 + AA 4 + hdr 2 + payload + CRC 3) bytes * 4 us
EMPTY, CREDIT = air(0), air(12)               # 44, 92 us
GAP={'open':{'off':150,'on':52},'sdc':{'off':150,'on':None}}
def gap(st,arm,t): return 150 if arm=='off' else (52 if st=='open' else (65 if t=='coc' else 70))
def packets(t):  # repeating data-packet airtimes and SDU bytes per packet
    return ([air(251), air(239)], 240.0) if t=='coc' else ([air(251)], 244.0)
WORST=lambda g: 2*air(251)+2*g
def n_ex(T,t,pol,st,arm,rule,P):
    g=gap(st,arm,t); seq,_=packets(t); rep = CREDIT if (t=='coc' and pol=='seg') else EMPTY
    tt=0.0; n=0; k=0
    while True:
        ex=seq[k%len(seq)]+rep+2*g
        if rule=='A' and tt+ex > T-P: break
        if rule=='B' and tt+WORST(g) > T-P: break
        tt+=ex; n+=1; k+=1
    return n
def pred(key,rule,P):
    t,pol,st,u,arm=key.split('|'); T=int(u)*1250.0
    n=n_ex(T,t,pol,st,arm,rule,P); return n*packets(t)[1]/(T/1e6)/1024
cells=lambda st,tr: [k for k in D if k.split('|')[2]==st and k.split('|')[0]==tr]
for st in ('open','sdc'):
    for rule in ('A','B'):
        best=[]
        for P in range(0,3001,2):
            ok=sum(1 for k in cells(st,'coc') if abs(pred(k,rule,P)/D[k]-1)<0.025)
            best.append((ok,P))
        top=max(b[0] for b in best); Ps=[P for ok,P in best if ok==top]
        Pm=Ps[len(Ps)//2]
        g_ok=sum(1 for k in cells(st,'gatt') if abs(pred(k,rule,Pm)/D[k]-1)<0.025)
        print(f'{st} rule {rule}: CoC fit {top}/{len(cells(st,"coc"))} cells within 2.5% for P in [{Ps[0]},{Ps[-1]}] us (gaps in range possible); at P={Pm}: GATT out-of-sample {g_ok}/{len(cells(st,"gatt"))}')
print()
PM={'open':272,'sdc':1288}
print(f"{'cell':32s} {'meas':>7s} {'model':>7s} {'err%':>6s} {'n_model':>7s}")
for k in sorted(D, key=lambda k:(k.split('|')[2],k.split('|')[0],k.split('|')[1],int(k.split('|')[3]),k.split('|')[4])):
    t,pol,st,u,arm=k.split('|'); P=PM[st]; p=pred(k,'A',P); T=int(u)*1250.0
    print(f"{k:32s} {D[k]:7.1f} {p:7.1f} {100*(D[k]/p-1):+6.1f} {n_ex(T,t,pol,st,arm,'A',P):7d}")
