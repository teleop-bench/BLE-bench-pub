exec(open('fit_rulec.py').read().split("# duplex (independent test)")[0])
for k in sorted(sdc):
    p=pred1(k,300)
    if abs(D[k]/p-1)>=0.025: print('SDC miss (rule C, m=300):',k,D[k],round(p,1))
op=[k for k in D if k.split('|')[2]=='open']
res=[(sum(1 for k in op if abs(pred1(k,m)/D[k]-1)<0.025),m) for m in range(0,1501,2)]
top=max(r[0] for r in res); ms=[m for ok,m in res if ok==top]
print(f'Zephyr under rule C: best {top}/{len(op)} cells (m in [{ms[0]},{ms[-1]}])  vs rule A: 28/30')
