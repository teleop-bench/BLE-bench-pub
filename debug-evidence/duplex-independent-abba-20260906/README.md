# Independent (non-echo) CoC-duplex, ±FSU, ABBA — experiment scaffold

**Status: RUN on hardware 2026-09-06 — see [FINDINGS.md](FINDINGS.md).** Resolves two audit flags:

1. **Real per-direction symmetry.** The published "~90+90, ≤0.1% imbalance" came from an *echo*
   rig (the peripheral echoes the forward stream → the two directions are structurally coupled, so
   near-equality is not surprising). This rig blasts **both directions independently** and counts
   each with its **own** cumulative byte counter, so up-vs-down imbalance is a genuine measurement.
2. **Whether duplex-FSU is real.** The "+14% / ~200 KB/s SDC" duplex-FSU figure was an echo-rig
   number that **did not reproduce** head-to-head. This runs FSU on/off ABBA on the independent rig.

## The rig (already implemented — no firmware changes needed for the open arm)
- `apps/coc/coc-duplex-central` — downlink blast thread **+** counts uplink bytes; prints
  `CENRX cum_total=<B>` every 1 s (bytes received from the sink = **uplink**).
- `apps/coc/coc-duplex-sink` — its **own** uplink blast thread + counts downlink bytes; prints
  `SINK rx: <KB/s> ... cum_total=<B> ... fsu=<us>` (bytes received from the central = **downlink**).
- Two independent counters — the **existing** `tools/analyze.py` already reads both:
  `CoC throughput` = downlink (from the sink log), `CoC uplink (CENRX)` = uplink (from the central
  log). Not an echo. (No new parser — analyze.py handles this rig.)

## Matrix
`{25 ms, 50 ms}` × `{FSU on, FSU off}` × `{open [, SDC]}`, **ABBA** (on/off/off/on) per interval,
fresh board reset between every arm (the sink latches `fsu_spacing` — stale-tag gotcha).
- **Open arm:** reuses proven configs (central `open-fsu.conf`/`open-nofsu.conf`; sink
  `open-fsu.conf` serves both — the central requests 52 µs vs 150 µs).
- **SDC arm:** overlays are **ported but NOT yet compiled** (`sdc-sel/sdc-fsu/sdc-nofsu.conf` on the
  central, `sdc-sel/sdc.conf` on the sink). **Verify the SDC build at the bench before trusting it.**

## Run
```
export CEN_ID=1057794857 CEN_TTY=/dev/cu.usbmodem0010577948573   # central,  VCOM1
export PER_ID=1057719509 PER_TTY=/dev/cu.usbmodem0010577195093   # sink,     VCOM1
export STACKS="open"          # add "open sdc" once the SDC build is verified
# west must be on PATH (activate the Zephyr venv; ZEPHYR_BASE = the fsu-m0 tree)
./run.sh                      # builds+flashes+captures each arm to ./captures/
# analyze each arm with the EXISTING tool (tools/analyze.py) — no new tooling:
python3 ../../tools/analyze.py captures/<arm>-per.log   # downlink = "CoC throughput"
python3 ../../tools/analyze.py captures/<arm>-cen.log   # uplink   = "CoC uplink (CENRX)"
# imbalance% = |up-down|/max ;  FSU on/off delta = compare aggregate(up+down) across the ABBA arms.
```

## What to look for / sanity gates (before trusting any number)
- Per arm, confirm on **both** consoles: `PHY tx=2`, `DLE … tx_max=251`, and `GATE conn interval=`
  matches the requested 25/50 ms (peripheral must NOT drift it — `AUTO_UPDATE_CONN_PARAMS=n`).
- FSU-on arms: sink shows `fsu=52`; FSU-off arms: `fsu=150` (or 0). A stale `fsu=52` on an off arm
  means the reset was skipped → discard.
- **Read the `imbal%` column** — that's the real independent-stream symmetry (expect it to be
  *worse* than the echo rig's ≤0.1%; single-channel CoC-duplex tends to favour downlink).
- FSU delta is the ABBA-pooled aggregate on/off %.

## Provenance to capture when you run it
Save both consoles per arm (run.sh does), the resolved `.config` of each build, the fork commit
(`git -C <zephyr> rev-parse fsu-m0`), board dev-ids, and distance. Absolute KB/s are RF-day
specific; the **per-direction imbalance** and the **FSU on/off delta** are the transferable results.
