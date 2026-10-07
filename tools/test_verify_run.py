#!/usr/bin/env python3
"""Regression test for verify_run.py — the gate must itself be guarded, or it silently rots.

Runs the verifier against curated, LABELED real captures in tools/verify-fixtures/ and asserts the
expected verdict. Each fixture is a real failure/success mode we actually hit:
  - revert_coc_on      : FSU reverted mid-run (188->160)      -> must REJECT (this is THE bug)
  - held_coc_on        : FSU held to end-of-run               -> must PASS
  - rampup_gattsdc_on  : slow ramp-UP to plateau (not a drop) -> must PASS  (regression: ramp != revert)
  - off_coc            : FSU-off arm                          -> must PASS
  - transient_dip_gatt_on : 4 s RF dip that RECOVERS to plateau -> must PASS (a revert never recovers)
Run: python3 tools/test_verify_run.py   (exit 0 = all pass). Wire into CI / pre-commit.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_run import verify, verify_duplex

FX = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'verify-fixtures')

# duplex cases (call verify_duplex): (name, per, cen, kwargs, expect_pass, mention)
DUPLEX_CASES = [
    ('duplex_held',  'duplex_held-per.log',  'duplex_held-cen.log',  dict(fsu='on', interval_ms=15), True,  None),
    ('duplex_stall', 'duplex_stall-per.log', 'duplex_stall-cen.log', dict(fsu='on', interval_ms=15), False, 'stall'),
]

# (name, per, cen, kwargs, expect_pass, must_mention_in_reject)
CASES = [
    ('revert_coc_on',     'revert_coc_on-per.log',     None,
        dict(fsu='on', interval_ms=25), False, 'hold'),
    ('held_coc_on',       'held_coc_on-per.log',       'held_coc_on-cen.log',
        dict(fsu='on', interval_ms=15), True,  None),
    ('rampup_gattsdc_on', 'rampup_gattsdc_on-per.log', 'rampup_gattsdc_on-cen.log',
        dict(fsu='on', interval_ms=7.5), True, None),   # ramp-up must NOT be mistaken for a revert
    ('off_coc',           'off_coc-per.log',           None,
        dict(fsu='off', interval_ms=15), True, None),
    ('sdc_held_coc_on',   'sdc_held_coc_on-per.log',   'sdc_held_coc_on-cen.log',
        dict(fsu='on', interval_ms=15, stack='sdc'), True, None),   # SDC uses 65/70us, not 52 — must not require a 52 token
    ('transient_dip_gatt_on', 'transient_dip_gatt_on-per.log', 'transient_dip_gatt_on-cen.log',
        dict(fsu='on', interval_ms=7.5), True, None),   # recovered dip (t=24-27 s, disc=0) is not a revert
]

def main():
    fails = 0
    for name, per, cen, kw, expect_pass, mention in CASES:
        pp = os.path.join(FX, per)
        if not os.path.exists(pp):
            print(f"  SKIP {name} (fixture missing)"); continue
        r = verify(pp, os.path.join(FX, cen) if cen else None, **kw)
        ok = (r['pass'] == expect_pass)
        if ok and mention:
            ok = any(mention in m.lower() for m in r['rejects'])
        status = 'ok ' if ok else 'FAIL'
        if not ok: fails += 1
        print(f"  [{status}] {name}: pass={r['pass']} (expected {expect_pass})  "
              f"fsu_held={r.get('fsu_held','n/a')}  rejects={r['rejects'] or '-'}")
    for name, per, cen, kw, expect_pass, mention in DUPLEX_CASES:
        pp = os.path.join(FX, per)
        if not os.path.exists(pp):
            print(f"  SKIP {name} (fixture missing)"); continue
        r = verify_duplex(pp, os.path.join(FX, cen), **kw)
        ok = (r['pass'] == expect_pass)
        if ok and mention:
            ok = any(mention in m.lower() for m in r['rejects'])
        if not ok: fails += 1
        print(f"  [{'ok ' if ok else 'FAIL'}] {name}: pass={r['pass']} (expected {expect_pass})  "
              f"DL={r['downlink']} UL={r['uplink']} agg={r['aggregate']}  rejects={r['rejects'] or '-'}")
    total = len(CASES) + len(DUPLEX_CASES)
    print(f"\n{'ALL PASS' if fails == 0 else str(fails)+' FAILED'} ({total} cases)")
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
