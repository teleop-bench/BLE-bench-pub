# TX-staging investigation firmware (2026-08-26)

Firmware that produced `debug-evidence/tx-staging-20260826/` and resolved the "refill wall"
as **tIFS airtime, not host staging** (`tx-staging-investigation.md`, `coc-technical-overview.md §11.1`).

Board: nRF54L15-DK (`nrf54l15dk/nrf54l15/cpuapp`). Central dev-id 1057794857 (port …948573),
peripheral 1057719509 (port …195093). Flash: `west flash -d <anydir> --dev-id <SN> --no-rebuild
--hex-file <name>.hex`; reset both, settle 6 s; capture with
`debug-evidence/latency-under-load-20260813/capture-tool.py`.

**The `_BTTESTING_pullctr` / `_BTTESTING_*` hexes require the patched Zephyr** (fsu-m0 @ **ea6c334d132**
= host-side `l2cap_pull_pdus` counter gated on `CONFIG_BT_TESTING`). All others build on fsu-m0 as-is.

| hex | app + `-D` recipe | role | used by |
|-----|-------------------|------|---------|
| coc-cen_480_int6_deep_diag  | coc-central, SDU=480, INT=6,  ACL_TX=64, CONN_TX=64, `FSU_EVENTFILL_DIAG=y` | central | Stage 1 A1 (7.5 ms) |
| coc-cen_480_int12_deep_diag | …INT=12 | central | Stage 1 A2 (15 ms) + FSU-off B/D |
| coc-cen_480_int20_deep_diag | …INT=20 | central | Stage 1 A3 (25 ms) |
| coc-cen_480_int12_shallow8_diag | …INT=12, ACL_TX=8, CONN_TX=8 | central | Stage 1 B1 (shallow control) |
| coc-cen_480_int12_deep_diag_FSUon | …INT=12 + `EXTRA_CONF_FILE=open-fsu.conf` | central | Stage 1b FSU-on A/C |
| coc-sink_autoupd-n | coc-sink, `BT_GAP_AUTO_UPDATE_CONN_PARAMS=n` | periph | Stage 1 + 2 sink |
| coc-sink_FSUcapable_autoupd-n | coc-sink + FSU controller feature + autoupd-n | periph | Stage 1b sink |
| coc-cen_480_int12_deep_diag_BTTESTING_pullctr | s2-cen: …INT=12 + `BT_TESTING=y` | central | Stage 2 Run A (host_pulls) |
| coc-duplex-central_BTTESTING | coc-duplex-central + `BT_TESTING=y` | central | Stage 2b duplex |
| coc-duplex-sink_BTTESTING_DLE251 | coc-duplex-sink + `BT_TESTING=y` (default DLE=251) | periph | Stage 2b stall probe |

Exact driver scripts (build + flash + capture + parse): `debug-evidence/tx-staging-20260826/stage1.sh`,
`stage1b.sh`, `stage2.sh`, `stage2b.sh`, `parse_stage1.py`. SHA256SUMS in this dir.
