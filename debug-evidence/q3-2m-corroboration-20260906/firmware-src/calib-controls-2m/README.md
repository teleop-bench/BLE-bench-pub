# Calib-control firmware (2M baseline controls that produced `gap-proxy-calibration-2m.json`)

These are the endpoint configs used for the **plain-Q2 2M baseline controls** (`../../calib-controls/`)
whose combine produced the ESTABLISHED calib (`../../gap-proxy-calibration-2m.json`, frozen 2791 t).
They are the SAME 2M apps as `../central-2m` / `../periph-2m` (same `main.c`, `fsu-f52.conf`/`fsu.conf`,
20 ms interval) **except the TX power**, captured here because plain-Q2 cells do not snapshot their own
`.config` and the calib json pins only the log files by hash — so without this the calib was not
rebuildable.

## ⚠️ These TX values are RIG-SPECIFIC, not a preset
- `central-prj.conf`: `CONFIG_BT_CTLR_TX_PWR_PLUS_1=y` (+1 dBm)
- `periph-prj.conf`:  `CONFIG_BT_CTLR_TX_PWR_PLUS_8=y` (+8 dBm)

The symmetric-calibration gate needs the observer to hear both endpoints within **≤6 dB**. On THIS
bench the observer's path to the central was ~7 dB better than to the peripheral, so central was set
7 dB *lower* to equalize received power → RSSI sep 4 dB, controls ACCEPT. **A different rig / board
placement needs different values** — the reusable rule is "balance *received* power at the observer,"
not "use +1/+8". See `docs/LESSONS.md`.

## Note on the two firmware states in this campaign
- **Mid-step reps** (`../../rep{1,2,3}/`) used the snapshot TX **central +8 / periph −20** (archived in
  `../central-2m` / `../periph-2m`). Mid-step has no RSSI-symmetry gate, so that was fine.
- **Calib controls** used the balanced **central +1 / periph +8** here.
- Applying the controls' calib to the reps is valid: the calib freezes a *timing* baseline
  (gap_proxy = 2791 t), which is TX-independent.
