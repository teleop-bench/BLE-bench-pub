#!/usr/bin/env python3
"""Regression for check_matched_pair.py — the confounded-A/B design gate.

Fixtures (real resolved .config, tools/matched-pair-fixtures/):
  confounded_{on,off}: the GATT pair that SHIPPED (off dropped the whole FSU package) -> must REJECT (exit 1)
  matched_{on,off}:    the clean re-run pair (differ only in APP_FSU_MIN/MAX_US) -> must PASS (exit 0)
  wrong-path/empty:    variable absent from both (e.g. read the sysbuild top-level config) -> must ERROR (exit 2)
Run: python3 tools/test_check_matched_pair.py
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from check_matched_pair import check

HERE = os.path.dirname(__file__)
FX = os.path.join(HERE, 'matched-pair-fixtures')
ALLOW = ['CONFIG_APP_FSU_MIN_US', 'CONFIG_APP_FSU_MAX_US']

def run():
    cases = []
    # 1. confounded pair -> offenders present (would exit 1)
    off, intended = check(f'{FX}/confounded_on.config', f'{FX}/confounded_off.config', ALLOW)
    cases.append(('confounded pair REJECTS', len(off) > 0, f'{len(off)} offenders'))
    cases.append(('confounded flags the event-buffer', any(k == 'CONFIG_BT_BUF_EVT_RX_SIZE' for k, _, _ in off), 'BUF_EVT_RX_SIZE'))
    # 2. matched pair -> no offenders, intended var differs (would exit 0)
    off, intended = check(f'{FX}/matched_on.config', f'{FX}/matched_off.config', ALLOW)
    cases.append(('matched pair PASSES', len(off) == 0, f'{len(off)} offenders'))
    cases.append(('matched pair has the intended diff', len(intended) > 0, f'intended={intended}'))
    # 3. false-MATCH guard: comparing a config to itself with the allow-vars present-but-equal -> intended empty
    off, intended = check(f'{FX}/matched_on.config', f'{FX}/matched_on.config', ALLOW)
    cases.append(('identical configs -> no intended diff (main() would exit 2)', len(intended) == 0, f'intended={intended}'))
    ok = True
    for name, passed, detail in cases:
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}  ({detail})")
        ok = ok and passed
    print('ALL PASS' if ok else 'FAILURES')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(run())
