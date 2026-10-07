#!/usr/bin/env python3
"""Matched-pair experiment linter — prevents the confounded-A/B pitfall.

A per-capture gate (verify_run.py) validates the SIGNAL (did FSU hold, is the link alive). It is
structurally blind to a DESIGN flaw: if the FSU-on and FSU-off builds differ in more than the one
variable under test, each arm still produces a perfectly clean, held signal — and the resulting
"delta" is not attributable to the variable. That exact confound (the GATT off-arm dropped the whole
FSU feature package, not just the requested spacing) sat documented-but-unenforced in REPRODUCE.md
and reached a report. This linter turns that prose caveat into a hard gate.

Usage:
    check_matched_pair.py <on/.config> <off/.config> --allow CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US
Exit 0 (MATCHED) only if the two resolved configs differ EXCLUSIVELY in the whitelisted symbols.
Any other differing symbol -> exit 1 (CONFOUNDED) with the offenders listed.
"""
import sys, argparse

def parse_config(path):
    """Resolved Kconfig -> {symbol: value}. 'CONFIG_X=y' -> {'CONFIG_X':'y'};
    '# CONFIG_X is not set' -> {'CONFIG_X':'n'} so an on/off toggle is a value diff, not a presence diff."""
    cfg = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith('# CONFIG_') and line.endswith(' is not set'):
                cfg[line[2:-11]] = 'n'
            elif line.startswith('CONFIG_') and '=' in line:
                k, v = line.split('=', 1)
                cfg[k] = v
    return cfg

def check(on_path, off_path, allow):
    on, off = parse_config(on_path), parse_config(off_path)
    allow = set(allow)
    offenders = []
    for k in sorted(set(on) | set(off)):
        if on.get(k) != off.get(k) and k not in allow:
            offenders.append((k, on.get(k, '<absent>'), off.get(k, '<absent>')))
    intended = sorted(k for k in allow if on.get(k) != off.get(k))
    return offenders, intended

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('on'); ap.add_argument('off')
    ap.add_argument('--allow', default='', help='comma-separated symbols permitted to differ (the variable under test)')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    allow = [s for s in a.allow.split(',') if s]
    offenders, intended = check(a.on, a.off, allow)
    if allow and not intended:
        # A valid A/B REQUIRES the variable under test to be present and differ. "Differs in nothing"
        # means the overlays didn't apply or the wrong .config path was read (e.g. sysbuild nests the
        # app config under <dir>/<app>/zephyr/.config, not <dir>/zephyr/.config) — a false MATCHED.
        print(f"ERROR: the intended variable(s) {allow} do not differ between the two configs.")
        print("  Either the overlays didn't apply or a wrong/empty .config path was read (sysbuild nests")
        print("  the app config under <build>/<app>/zephyr/.config). Refusing to certify as matched.")
        return 2
    if offenders:
        print(f"CONFOUNDED: {len(offenders)} symbol(s) differ beyond the whitelist {allow}:")
        for k, ov, fv in offenders:
            print(f"  {k}: on={ov} off={fv}")
        print("  -> not a clean isolation; refuse to attribute any delta to the intended variable.")
        return 1
    if not a.quiet:
        print(f"MATCHED: arms differ ONLY in {intended} (the variable under test). Clean isolation.")
    return 0

if __name__ == '__main__':
    sys.exit(main())
