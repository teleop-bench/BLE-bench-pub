# FSU-M0 build provenance (corrected — tIFS floor configured/resolved to 52 µs)

Resolved build artifacts after applying the `fsu-m0-series` patch set, with the
overlay fix that unhides the low-latency menu (`CONFIG_BT_CTLR_ADVANCED_FEATURES=y`).
The build proves the configuration RESOLVES to a 52 µs tIFS floor; whether it takes
physical effect on air is exactly what Q3 determines — not asserted here. Archived as
textual artifacts (`.config`, `build_info.yml`) so the build is auditable without
re-running west. HEX/ELF hashes are this-host provenance (west artifacts are not
bit-reproducible across environments) — re-pin at smoke time.

## Toolchain / environment
- Zephyr base: `fsu-m0` HEAD `9999e040446` (post-apply tree `84b2d6a831a1…`; base `v4.4.1`).
- Toolchain: Zephyr SDK **1.0.1** (`$HOME/zephyr-sdk-1.0.1`).
- Board: `nrf54l15dk/nrf54l15/cpuapp`.
- Application repo: **`zenoh-pico-ble-test`**. The overlays used are committed in the
  SAME changeset as this file (the runner-enforced-config-assertion commit, on top of
  `7f0ce91`); the working tree was dirty at build time only because these overlay
  edits + this provenance were pending that commit. The exact overlay contents built
  are therefore those recorded at this file's commit.

## Build commands
```
# central (request arm)
west build -b nrf54l15dk/nrf54l15/cpuapp -d <bd> q2-central -p always -- -DEXTRA_CONF_FILE=fsu-f100.conf
# peripheral (responder + on-chip bench)
west build -b nrf54l15dk/nrf54l15/cpuapp -d <bd> q2-periph  -p always -- -DEXTRA_CONF_FILE=fsu.conf
```

## Config assertion (hard gate, runner-enforced)
`assert_fsu_config.py <.config> --arm {f100|f150|periph}` PASSES for all three arms:
advanced features + low-latency interval + **tIFS = 52** + host/controller FSU +
`FSU_BENCH_FORCE_FEAT` + 1M-only (`PHY_2M`/`AUTO_PHY_UPDATE` off); the registered
request params (f100 = 100/150, f150 = 150/0) + Q3 mode on the central arms; on-chip
bench on (periph) / off (central); in-event echo off. It also requires `BT_CENTRAL=y`
(f100/f150) / `BT_PERIPHERAL=y` (periph). `q2_run.py` runs this on BOTH endpoint
`.config` files for every `--q3` run BEFORE ANY flashing (observer + endpoints) and
QUARANTINES (`q3-fsu-config-invalid`) on any failure, recording the result + this
script's hash in `manifest.json`.

## Hashes

### central f100 (q2-central + fsu-f100.conf) — arm=f100 (request 100→150)
```
59ad238bfe5a3a998359a254b07a55a692dcd9e37750380d12dfbd2cd2dbcfa2  central-fsu-f100.config
5b6a06ded9b4ae908e6ff9e159933ef627be91d90ee7bd60c369e4788393f063  central-fsu-f100.build_info.yml
00a6513eec121bd7065712c9506e9e956367a1ec5210a5949e1eb0a8a1094e0d  zephyr.hex (re-pin at smoke)
ed395929e296fa04d8303c231c3501faf87836dc0c42f47831788b968931d203  zephyr.elf (re-pin at smoke)
```

### central f150 (q2-central + fsu-f150.conf) — arm=f150 (control, request 150→0)
```
d3d4e4489225b1cd76327f2ea79f48b6d5445154642cecf1a51fec94959cd06f  central-fsu-f150.config
9db948450b2d890765d7e7165ebd8b99ac4cf2a6c4a4be17a4d80baaff821c92  central-fsu-f150.build_info.yml
a287df7b3c44d35994031f88a8db0a2a87a648f7fd364d373748137d3c124fb2  zephyr.hex (re-pin at smoke)
9835f73e6a985917a9b7e382d8e0153a262ce72bd8b41b7b6395e4e874ed09be  zephyr.elf (re-pin at smoke)
```

### peripheral (q2-periph + fsu.conf) — arm=periph (responder + on-chip bench)
```
7fb69b69dfb7252cddc13aef2b414f1b6026ab83391fe9c01aca052ba350950f  periph-fsu.config
dfa20ffde305bd5b87791eaab1a6e24d32dc8afabed1cc1ec168c1cb0c8667a1  periph-fsu.build_info.yml
398894a8886122b8eeada4bd3f0fb0f57c1c36c91ac21c990ebc49f4bcfa2e14  zephyr.hex (re-pin at smoke)
6962b070a7f233520583cd27632fcbb4bbaa2a66ff6e5cb8c47c52b10b73c827  zephyr.elf (re-pin at smoke)
```

> Supersedes the earlier hash-only provenance (`.config` `9db55a34…`), which was
> built BEFORE the `ADVANCED_FEATURES` fix and therefore still had the 150 µs floor.
>
> The F-time `Q3PHASESNAP` (`M`) handler AND the smoke-1 controller fixes (patches 0010
> responder notification, 0011/0012 on-chip instrument revs) were added AFTER these builds; they
> change HEX/ELF but NOT `.config` (all code, not Kconfig), so the archived `.config`
> hashes + the assertion still hold. HEX/ELF re-pin at the next smoke regardless.
