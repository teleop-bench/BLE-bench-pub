# Consistency checks & surprise findings (pre-share gate)

Run these before sharing the benchmark externally. They exist because a config/recipe can look right and
still hand a reader plausible-but-wrong numbers — several did (see the surprise ledger below). This is the
"no mess-ups" checklist.

## Automated — `tools/repro-check.py`
```
python3 tools/repro-check.py     # expect: "problems: 0"
```
Statically checks **every** `west build` recipe in `REPRODUCE.md`:
- the app dir exists,
- every `EXTRA_CONF_FILE` overlay exists in that app,
- every `-DCONFIG_APP_*` symbol is **defined** by that app's Kconfig (an undefined one HARD-ABORTS the
  build — this is how the duplex `-DCONFIG_APP_SDU_SIZE=480` bug was caught),
- every ``c-``/``p-`` prebuilt hex named in the doc exists somewhere under `prebuilt-hexes/`.

## Manual — the checks a script can't make
1. **FSU actually engaged — capture from BOOT.** The confirmation prints ONCE at connection setup
   (`Q3FSU-DONE … spacing=52` on the CoC sink/central; `FSU: updated … spacing=52 us` on the GATT central).
   A mid-stream capture never sees it. `run-all.sh` now captures from the reset instant for exactly this.
   Both link ends must carry the floor — a sink built without `open-fsu.conf` silently sits at `spacing=150`.
2. **Both ends carry the FSU floor** (open controller): `EVENT_IFS_LOW_LAT_US=52` + `CONN_INTERVAL_LOW_LATENCY=y`
   + `ADVANCED_FEATURES=y` + `FRAME_SPACE_UPDATE=y`. Every sink/peripheral gets its `open-fsu.conf` overlay.
3. **Map results by provenance, not app name.** Use `docs/APP-PROVENANCE.md`. The headline GATT result comes
   from `z54-lat-central`, not `z54-gatt-*`.
4. **FSU is nRF54L15 (BLE 6) only.** nRF52 (BLE 5) can't do it — a floor-less nRF52 config is expected, not a bug.
5. **Clean A/B isolation.** The off arm should differ from the on arm by ONE variable. True for CoC
   (`open-fsu` vs `open-nofsu`, MIN/MAX only); the GATT off arm drops the whole overlay — quote the CoC delta.
6. **Fork pinned immutably** for build-from-source (`--mr <commit>`), because the `fsu-m0` branch may be
   repointed v4.4.1→v4.4.2.

## Surprise findings ledger — 2026-09-07 config/reproduction audit
Full detail + hardware proofs: `debug-evidence/fsu-config-audit-20260907/FINDINGS.md`.

| # | surprise | class | status |
|---|---|---|---|
| 1 | `coc-sink/open-fsu.conf` missing `CONN_INTERVAL_LOW_LATENCY` → FSU silent 150 | silent-wrong-data | FIXED (a27ed8a) + HW-verified `spacing=52` |
| 2 | duplex recipe: `-DCONFIG_APP_SDU_SIZE=480` undefined → build abort; sink built floorless | build-abort + silent-150 | FIXED + HW-verified |
| 3 | `coclat-sink` + `coclat2-sink` recipes built floorless → latency-under-load ran at tIFS=150 | silent-wrong-data | FIXED + HW-verified (both) |
| 4 | `run-all.sh` opened serial mid-stream → FSU column blank on a good run | can't-verify | FIXED (capture from boot) |
| 5 | sanity check demanded a `spacing=` line the GATT responder never prints | misleading | FIXED (scoped CoC vs GATT) |
| 6 | GATT-SDC responder missing `SDC_ENABLE_LOWEST_FRAME_SPACE` → SDC-GATT FSU delta understated | conservative-wrong | fragment added; re-measure (needs NCS+HW) |
| 7 | `--mr fsu-m0` pins a mutable branch slated to move to v4.4.2 (changes host gatt.c/att.c) | latent blocker | FIXED (immutable-pin recipe) |
| 8 | fork HEAD SHA stated 3 ways (one matched nothing) | trust-erosion | FIXED |
| 9 | GATT "only the gap differs" isolation claim false; GATT-SDC hex `@25ms` vs `units=6` recipe | misleading | FIXED |
| 10 | audit keyed on app NAME picked the wrong GATT app (`z54-gatt-*` vs `z54-lat-central`) | process | FIXED (APP-PROVENANCE + provenance rule) |
| 11 | `python3 analyze.py` (no `tools/`), EATT payload ≤245 vs true ≤242, stale on-air-FSU "open item" | nits | FIXED |

**Not bugs (verified, so we don't cry wolf):** the EATT arm at IFS=150 is correct (it toggles EATT, not FSU);
nRF52 apps having no FSU is a hardware boundary; the `ADVANCED_FEATURES` "visible if" menu comment is
inaccurate but harmless (no real Kconfig dep clamps the floor symbols).
