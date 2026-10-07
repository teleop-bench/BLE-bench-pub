# Zephyr CoC duplex FSU with 20-deep controller queues (the corrected open-stack recipe) (2026-10-06)

**Status: RESOLVED (one session, n=4 per arm; not yet replicated).** Supersedes the Zephyr half of
`coc-duplex-fsu-fixed-20261006`, whose 64-deep controller TX queues caused credit-loop starvation at 25 ms
(`coc-duplex-25ms-diag-20261006`). Prediction (in `coc-duplex-25ms-diag-20261006/PREDICTIONS.md`, step E, written
before this run): 7.5 and 15 ms unchanged from the 64-deep run, 25 ms ~185 → ~205 (~+10%).

Design: `tools/coc-duplex-fsu.py --stack open --intervals 6,12,20 --cen-extra/--sink-extra "-DCONFIG_BT_BUF_ACL_TX_COUNT=20
-DCONFIG_BT_ATT_TX_COUNT=64"` (since this run that setting is in the apps' open overlays; a recipe build is byte-identical
to these images, verified). Fixed apps (`coc-credit-fixes-20261006`), matched arms, held-FSU gate, first connection after
each central flash discarded, peripheral-only resets. Open `v4.4.2-16-gfee9fbc`; close range.

## Result (`results.jsonl`, `run.log`; aggregate KB/s, Welch t95; 24/24 accepted, 0 stalls)
| interval | Zephyr, 20-deep queues | gain | SDC (`coc-duplex-fsu-fixed-20261006`) | gain |
|---|---|---|---|---|
| 7.5 ms | 184.0 → 184.4 | +0.2% ± 0.1 | 183.9 → 183.9 | +0.0% ± 0.3 |
| 15 ms | 184.9 → 189.6 | +2.5% ± 2.3 | 184.3 → 189.3 | +2.8% ± 0.3 |
| 25 ms | 187.4 → 205.3 | +9.6% ± 0.6 | 186.3 → 205.4 | +10.2% ± 0.5 |

Per direction ~1:1 in every cell (e.g. 25 ms on 101.8 / 103.5).

## Reading
- With matched controller queue depth (SDC's controller holds 20 packets) **Zephyr and SDC CoC duplex agree within
  ~1 KB/s in every cell**, and both follow the whole-exchange pattern (3 → 3, 6 → 6, 10 → 11). The 15 ms gain (+2.5 to
  +2.8% on both stacks) sits a little above the model's 0%, the known 1–5 point under-prediction.
- The prediction held at 7.5 and 25 ms; at 15 ms the shallow queue also lifted FSU on (182.8 → 189.6 vs the 64-deep run),
  which was not predicted.
- One session, n=4; not yet replicated on a second day.

Files: `results.jsonl`, `caps/`, `run.log`, `firmware/` (+ SHA256SUMS), `configs/`.
