#!/usr/bin/env bash
# Quantify the intermittent independent-duplex uplink-stall RATE. NO source/config change:
# build once per interval, then reset+capture REPS times (each reset re-rolls the connection).
: "${CEN_ID:?}" "${PER_ID:?}" "${CEN_TTY:?}" "${PER_TTY:?}"
: "${B:=nrf54l15dk/nrf54l15/cpuapp}" "${SECS:=30}" "${REPS:=12}" "${INTERVALS:=6 20}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"; OUT="$(cd "$(dirname "$0")" && pwd)/captures"; mkdir -p "$OUT"
DC="$REPO/apps/coc/coc-duplex-central"; DS="$REPO/apps/coc/coc-duplex-sink"
ms(){ echo $(( $1*125/100 )); }
echo "### sink build+flash"; west build -p always -b "$B" "$DS" -d "$DS/build" -- -DEXTRA_CONF_FILE=open-fsu.conf
west flash -d "$DS/build" --dev-id "$PER_ID" --no-rebuild
for u in $INTERVALS; do M=$(ms "$u")
  echo "### central build+flash (${M}ms, FSU-on)"
  west build -p always -b "$B" "$DC" -d "$DC/build" -- -DEXTRA_CONF_FILE=open-fsu.conf -DCONFIG_APP_CONN_INT_UNITS="$u"
  west flash -d "$DC/build" --dev-id "$CEN_ID" --no-rebuild
  for r in $(seq 1 "$REPS"); do
    nrfutil device reset --serial-number "$CEN_ID" >/dev/null; nrfutil device reset --serial-number "$PER_ID" >/dev/null; sleep 6
    tag="${M}ms_rep$(printf %02d "$r")"
    CAP_OUTDIR="$OUT" python3 "$REPO/tools/capture-tool.py" "$SECS" "$CEN_TTY:$tag-cen" "$PER_TTY:$tag-per" >/dev/null 2>&1
    echo "  $tag done"
  done
done
echo "### DONE"
