# CoC + FSU at 37-channel (open controller): does FSU convert to goodput on L2CAP CoC? — YES, +14.4%

**2026-08-13.** Answers the same-regime question the GATT observer rig could not: the observer
forces a 2-channel-pinned link; here the CoC link runs the **real 37-channel adaptive-hopping**
regime — the same regime as the charted "open Zephyr · CoC · 157". No observer (its pinned-channel
requirement is incompatible with 37-channel); FSU application is proven by the **peer's** own
frame-space-updated completion.

## Reproducibility — exact setup

**Hardware:** 2× Nordic nRF54L15-DK.
- Central (CoC driver / sender): J-Link SN **1057794857**, console `/dev/cu.usbmodem0010577948573`.
- Sink (CoC seg_recv / receiver): J-Link SN **1057719509**, console `/dev/cu.usbmodem0010577195093`.
- No observer board used. Bench-top, ambient 2.4 GHz (Wi-Fi present — see caveat on absolute level).

**Software:** Zephyr **v4.4.1-14-g9999e0404460**, `west`/`nrfutil` toolchain from `~/zephyrproject`.
Open software controller `BT_LL_SW_SPLIT` + the in-tree FSU patches (this is plain Zephyr, NOT NCS;
there is no SoftDevice here). Board target `nrf54l15dk/nrf54l15/cpuapp`.

**Firmware** (sources + hexes + one full .config in `firmware/`; sha256 in `PROVENANCE.txt`):
- Central `/tmp/coc-cen-app` (`central-main.c`): CoC L2CAP client, 244 B SDUs, 2M PHY, DLE-251,
  50 ms interval (`BT_LE_CONN_PARAM(40,40,0,400)`), K_FOREVER pool alloc (pool backpressure = the
  saturation source). Auto-requests FSU once the CoC channel is up (`CONFIG_APP_AUTO_FSU`).
  Two builds, identical except the requested spacing:
  - `fsu-f52.conf`  → `APP_FSU_MIN_US=52`  → **f52 hex** (`coc-f52.hex`): reduces tIFS to 52 µs.
  - `fsu-f150.conf` → `APP_FSU_MIN_US=150` → **f150 hex** (`coc-f150.hex`): requests [150..150];
    the controller no-ops a request for the default 150, so **no peer FSU completion fires and the
    link stays at the native 150 µs tIFS** — i.e. f150 is a genuine FSU-OFF baseline (see caveat).
- Sink `/tmp/coc-sink-app` (`sink-main.c`): CoC seg_recv sink, 64 credits, counts `cum_total` bytes,
  prints `SINK rx: … fsu=<us> … cum_total=<B>` every 1 s. `sink-fsu.conf` enables FSU + the IFS
  clamp (both sides must set `EVENT_IFS_LOW_LAT_US=52` for <150 to be granted).

**Procedure** (`coc_arm.sh`, one arm): flash sink FRESH (clean advertise state), flash the central
hex, then `capture-tool.py` (pyserial, DTR asserted) records both consoles 75 s. The central
connects a few seconds in; FSU (if any) negotiates once; the sink logs goodput for the rest.
**ABBA order f150, f52, f52, f150** (cancels linear drift). Goodput = least-squares slope of the
sink `cum_total` over the steady window (drop first 25 %), `coc_gp.py` — receiver-delivered, the
same method as the GATT rig.

## Result (drift-cancelled ABBA)

| pos | arm  | tIFS (peer proof)                 | occupancy (pkts/ev) | goodput KB/s |
|-----|------|-----------------------------------|---------------------|--------------|
|  1  | f150 | 150 µs (no FSU; no peer callback) | mean 28, max 34     | 130.4 |
|  2  | f52  | **52 µs** (`Q3FSU-DONE role=P spacing=52`) | mean 34, max 40 | 150.5 |
|  3  | f52  | **52 µs** (peer proof)            | mean 30, max 40     | 149.9 |
|  4  | f150 | 150 µs (no FSU)                   | mean 28, max 34     | 132.2 |

- **FSU-off mean 131.3 vs FSU-on mean 150.2 KB/s → +18.9 KB/s = +14.4 %.**
- ABBA (A={1,4}, B={2,3}, both centered at pos 2.5) cancels linear drift.
- **Strict non-overlap:** min FSU-on 149.9 > max FSU-off 132.2.
- **0 mid-run disconnects** in all 4 arms; FSU application proven on the PEER (sink `role=P`) both
  f52 arms.
- **Conversion mechanism directly observed:** FSU raised packets/event (mean 28→32, max 34→40) —
  the shorter tIFS lets more DATA packets fit the same event. So the prior "open+FSU CoC: no 2M
  gain" was WRONG; CoC events here are partly airtime-bound and FSU converts.

## Firmware bug found + fixed (relevant to reproducibility)
The sink called `bt_le_adv_start()` directly inside the `disconnected` callback; it returns
**-EAGAIN (-12)** there, leaving the sink un-discoverable after the first disconnect (every arm
after the first failed to connect). Fixed by deferring the re-advertise to a work item that retries
until it succeeds (`adv_work_fn` in `sink-main.c`). Without this fix the ABBA cannot be run.

## Honest caveats (do NOT overclaim)
1. **Absolute baseline ≠ the charted 157.** FSU-off here is ~131 KB/s, not 157. The +14.4 % is a
   solid WITHIN-RIG same-regime effect, but the absolute level is depressed vs the charted 157 —
   cause not yet reconciled (candidate: today's ambient 2.4 GHz, or a config delta such as SDU size
   / interval vs the original 157 run). **This gap must be reconciled before the +14.4 % is mapped
   onto the chart's 157 bar.**
2. **f150 control is "native 150", not "run-the-procedure-grant-150".** The [150..150] request
   no-ops (no peer callback), so f150 = FSU inactive at the default tIFS. Valid as an FSU-OFF
   baseline; it is not an in-band "procedure ran but granted 150" control.
3. **One ABBA block (n=2 per arm).** Strong (non-overlap + drift-cancel) but a small sample; a 2nd
   block tightens the interval.
4. Scope: open controller only (`BT_LL_SW_SPLIT` + FSU). Says nothing about SDC+FSU on CoC.
