#!/bin/bash
# Clean GATT FSU re-build: matched off-arm (keeps FSU feature package, requests 150us) + FSU-capable
# sinks. Open = flat config; SDC = sysbuild (config/hex nested under z54-lat-central|z54-lat-periph).
# Archives resolved .config per cell (closes reproducibility open-item) and runs the matched-pair
# design gate at the end (ABORT if any on/off pair is confounded).
set -u
REPO=<REPO>
CEN=$REPO/apps/nrf54l15/z54-lat-central
PER=$REPO/apps/nrf54l15/z54-lat-periph
B=nrf54l15dk/nrf54l15/cpuapp
EV=$REPO/debug-evidence/gatt-fsu-clean-20260908
GC=/tmp/gattclean
mkdir -p "$EV/configs"
say(){ echo "[$(date +%H:%M:%S)] $*"; }
ob(){ # open build:  $1=outdir $2=EXTRA_CONF $3=units
  ( cd <ZEPHYR_WS> && source .venv/bin/activate 2>/dev/null
    west build -p always -b $B "$4" -d "$GC/$1" -- -DEXTRA_CONF_FILE="$2" -DCONFIG_APP_CONN_INT_UNITS=$3 ) \
    > "$GC/$1.buildlog" 2>&1 && say "  built $1" || { say "  FAILED $1"; tail -4 "$GC/$1.buildlog"; }
}
sb(){ # sdc build (NCS launcher, sysbuild): $1=outdir $2=EXTRA_CONF $3=units $4=app
  ( cd <NCS_WS>
    nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
      west build -p always -b $B "$4" -d "$GC/$1" -- -DEXTRA_CONF_FILE="$2" -DCONFIG_APP_CONN_INT_UNITS=$3 ) \
    > "$GC/$1.buildlog" 2>&1 && say "  built $1" || { say "  FAILED $1"; tail -4 "$GC/$1.buildlog"; }
}
# SINKS: the peripheral app has NO APP_CONN_INT_UNITS symbol (interval is central-driven) and the build
# aborts on an undefined-symbol assignment — so sink builds must OMIT that flag.
obk(){ ( cd <ZEPHYR_WS> && source .venv/bin/activate 2>/dev/null
    west build -p always -b $B "$3" -d "$GC/$1" -- -DEXTRA_CONF_FILE="$2" ) \
    > "$GC/$1.buildlog" 2>&1 && say "  built $1" || { say "  FAILED $1"; tail -4 "$GC/$1.buildlog"; }
}
sbk(){ ( cd <NCS_WS>
    nrfutil toolchain-manager launch --ncs-version v3.4.0 -- \
      west build -p always -b $B "$3" -d "$GC/$1" -- -DEXTRA_CONF_FILE="$2" ) \
    > "$GC/$1.buildlog" 2>&1 && say "  built $1" || { say "  FAILED $1"; tail -4 "$GC/$1.buildlog"; }
}
UNITS="6 12 20 30 40"

say "=== OPEN central arms (on=hh-fsu52, off=hh-fsu150; both keep tput-open+fsu-open) ==="
for u in $UNITS; do
  ob "open/on_$u"  "tput-open.conf;fsu-open.conf;hh-fsu52.conf"  $u "$CEN"
  ob "open/off_$u" "tput-open.conf;fsu-open.conf;hh-fsu150.conf" $u "$CEN"
done
say "=== OPEN sink (FSU-capable responder, shared by both arms) ==="
obk "open/sink" "tput-open.conf;fsu-open.conf" "$PER"

say "=== SDC central arms (on=hh-sdc-fsu, off=hh-sdc-nofsu) ==="
for u in $UNITS; do
  sb "sdc/on_$u"  "sdc-sel.conf;tput.conf;hh-sdc-fsu.conf"   $u "$CEN"
  sb "sdc/off_$u" "sdc-sel.conf;tput.conf;hh-sdc-nofsu.conf" $u "$CEN"
done
say "=== SDC sink (FSU-capable responder) ==="
sbk "sdc/sink" "sdc-sel.conf;tput.conf;sdc-6x.conf;sdc-llbuf.conf;sdc-fsu.conf" "$PER"

# resolved-config path differs: open flat, SDC nested under app name
ocfg(){ echo "$GC/$1/zephyr/.config"; }
scfg(){ echo "$GC/$1/z54-lat-central/zephyr/.config"; }
say "=== archive resolved .config + run DESIGN GATE (matched-pair) ==="
GATE_OK=1
for u in $UNITS; do
  cp "$(ocfg open/on_$u)"  "$EV/configs/open_on_$u.config"  2>/dev/null
  cp "$(ocfg open/off_$u)" "$EV/configs/open_off_$u.config" 2>/dev/null
  cp "$(scfg sdc/on_$u)"   "$EV/configs/sdc_on_$u.config"   2>/dev/null
  cp "$(scfg sdc/off_$u)"  "$EV/configs/sdc_off_$u.config"  2>/dev/null
  python3 $REPO/tools/check_matched_pair.py "$(ocfg open/on_$u)" "$(ocfg open/off_$u)" \
    --allow CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US --quiet && say "  GATE open $u: MATCHED" || { say "  GATE open $u: FAIL"; GATE_OK=0; }
  python3 $REPO/tools/check_matched_pair.py "$(scfg sdc/on_$u)" "$(scfg sdc/off_$u)" \
    --allow CONFIG_APP_FSU_MIN_US,CONFIG_APP_FSU_MAX_US --quiet && say "  GATE sdc  $u: MATCHED" || { say "  GATE sdc  $u: FAIL"; GATE_OK=0; }
done
# responder-capability + auto-update-off on the sinks
for s in "open/sink/zephyr/.config" "sdc/sink/z54-lat-periph/zephyr/.config"; do
  f="$GC/$s"
  # a disabled bool is written "# CONFIG_X is not set", NOT "CONFIG_X=n" — accept either as OFF
  grep -qE "CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=n|# CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS is not set" "$f" 2>/dev/null && say "  SINK $s: auto-update OFF ok" || { say "  SINK $s: auto-update NOT off"; GATE_OK=0; }
  grep -qE "SDC_ENABLE_LOWEST_FRAME_SPACE=y|EVENT_IFS_LOW_LAT_US=52" "$f" 2>/dev/null && say "  SINK $s: FSU floor ok" || { say "  SINK $s: FSU floor MISSING (on-arm would be understated)"; GATE_OK=0; }
done
if [ $GATE_OK -eq 1 ]; then say "BUILD+GATE COMPLETE: all pairs MATCHED, sinks ok"; else say "DESIGN GATE FAILED — refusing the run (exit 1)"; exit 1; fi
