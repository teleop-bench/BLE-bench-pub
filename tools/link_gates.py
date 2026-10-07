"""Per-rep link gates shared by the campaign tools (added 2026-10-06 after the CoC credit bugs and the audit).

Each gate takes the log lines of ONE measured connection (from its connect line to the end of the capture) and
returns a list of reject strings (empty = pass). They check what the build-config gates cannot see:

  phy_dle(lines)                 last PHY line is 2M both ways; last data-length line (if the app prints one) is
                                 251/251. A link that never left 1M or 27-octet PDUs gives plausible-but-wrong rates.
  interval_held(lines, units)    every interval the app logs after connect equals the pinned interval (catches a
                                 connection-parameter update mid-run, the August auto-update confound).
  credit_window(cen, per, w)     CoC: the peripheral's initial TX credit window equals the designed w, and neither side's
                                 running TX credit balance ever exceeds w (catches credit leaks: a balance that grows
                                 across reconnects, and 0 initial credits, which trips the Zephyr 4.4 host stall).

Line formats matched: CoC apps "PHY: tx=2 rx=2" / "PHY updated: tx=2 rx=2", "DLE: tx_max=.. rx_max=.." /
"DLE updated: ...", "GATE conn: interval=N" (1.25 ms units), "L2CAP connected: ... tx.credits=N",
"CoC up ... txcred=N", "CENRX ... txcred=N", "RPT occ: ... txcred=N", "SINK rx: ... txcred=N";
GATT apps "PHY tx=2 rx=2", "GAP connected interval=<us>", ">>> interval now N (units)".
"""
import re


def _last(lines, pat):
    v = None
    for l in lines:
        m = re.search(pat, l)
        if m: v = m.groups()
    return v


def phy_dle(lines, duplex=True):
    """duplex=False (one-way sender's log): only the TX data length must be 251; RX may stay at 27."""
    rej = []
    phy = _last(lines, r'PHY(?: updated)?:? tx=(\d) rx=(\d)')
    if phy is None: rej.append('no PHY line after connect')
    elif phy != ('2', '2'): rej.append(f'PHY not 2M both ways (tx={phy[0]} rx={phy[1]})')
    dle = _last(lines, r'DLE(?: updated)?: tx_max=(\d+) rx_max=(\d+)')
    if dle is not None and (dle[0] != '251' or (duplex and dle[1] != '251')):
        rej.append(f'data length not {"251/251" if duplex else "251 TX"} (tx_max={dle[0]} rx_max={dle[1]})')
    return rej


def interval_held(lines, units):
    seen = []
    for l in lines:
        m = re.search(r'GATE conn: interval=(\d+)', l) or re.search(r'>>> interval now (\d+)', l)
        if m: seen.append(int(m.group(1))); continue
        m = re.search(r'GAP connected interval=(\d+)', l)
        if m: seen.append(round(int(m.group(1)) / 1250))
    if not seen: return ['no interval logged after connect']
    bad = sorted({v for v in seen if v != units})
    return [f'interval left the pinned {units} units: saw {bad}'] if bad else []


def credit_window(cen, per, window=64):
    # Only the peripheral's initial value is checked: it is printed after the host applies the central's initial
    # window (the value that matters for the 0-initial-credit stall). The central's "L2CAP connected ... tx.credits="
    # is printed in the connect callback before the peer's credits are applied, so it always reads 0.
    rej = []
    init_p = _last(per, r'CoC up.*txcred=(-?\d+)')
    if init_p is not None and int(init_p[0]) != window:
        rej.append(f'peripheral initial TX credits {init_p[0]} != {window}')
    for side, lines, pat in (('central', cen, r'(?:CENRX|RPT occ).*txcred=(-?\d+)'),
                             ('peripheral', per, r'SINK rx:.*txcred=(-?\d+)')):
        vals = [int(m.group(1)) for l in lines for m in [re.search(pat, l)] if m]
        if vals and max(vals) > window:
            rej.append(f'{side} TX credit balance reached {max(vals)} > window {window} (credit leak)')
    return rej
