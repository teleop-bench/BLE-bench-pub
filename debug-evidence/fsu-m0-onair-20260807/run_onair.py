#!/usr/bin/env python3
"""Hardened on-air capture runner (for the next SDC-positive-control session).
Guarantees a clean single-session pcap: refuses if another sniffer is running,
writes to a UNIQUE nonexistent pcap, and rejects a capture that ends up
non-monotonic (hand off to analyze_onair.py which re-checks).
Usage: run_onair.py <sniffer_port> <target_addr> <periph_sn> <secs> <out_prefix>
"""
import subprocess, time, os, sys, termios, select, glob
os.environ['PATH']=os.path.expanduser('~/.nrfutil/bin')+':'+os.environ['PATH']
port,target,periph_sn,secs,prefix = sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4]),sys.argv[5]
# single-sniffer guard
if 'ble-sniffer sniff' in subprocess.run(['pgrep','-af','nrfutil'],capture_output=True,text=True).stdout:
    sys.exit('REFUSE: a nrfutil ble-sniffer process is already running.')
# unique nonexistent pcap
i=0
while True:
    pcap=f'{prefix}-{i}.pcap'
    if not os.path.exists(pcap): break
    i+=1
sn=subprocess.Popen(['nrfutil','ble-sniffer','sniff','--port',port,'--follow',target,
    '--output-pcap-file',pcap],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(3)
subprocess.run(['nrfutil','device','reset','--serial-number',periph_sn],capture_output=True)
time.sleep(secs)
sn.terminate()
try: sn.wait(timeout=5)
except: sn.kill()
# monotonicity self-check
ts=[float(x) for x in subprocess.run(['tshark','-r',pcap,'-T','fields','-e','frame.time_relative'],
    capture_output=True,text=True).stdout.split() if x]
bad=sum(1 for a,b in zip(ts,ts[1:]) if b<a-1e-6)
print(f'{pcap}: {len(ts)} frames, {"MONOTONIC OK" if not bad else "NON-MONOTONIC (%d) -> reject"%bad}')
