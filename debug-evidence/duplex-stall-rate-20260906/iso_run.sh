#!/usr/bin/env bash
# ISOLATION: reset ONLY the peripheral each rep -> central stays booted -> its reconnect is a
# SUBSEQUENT same-boot connection (steady-state), NOT a first-CoC-since-boot. Disambiguates the
# 25ms cold-reset stall: if it balances here -> cold-reset stall was the first-boot reconnect wedge;
# if it still stalls -> steady-state head-of-line starvation. 25ms only (the discriminating interval).
: "${CEN_ID:?}" "${PER_ID:?}" "${CEN_TTY:?}" "${PER_TTY:?}"
: "${B:=nrf54l15dk/nrf54l15/cpuapp}" "${SECS:=30}" "${REPS:=12}" "${U:=20}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"; OUT="$(cd "$(dirname "$0")" && pwd)/captures-iso"; mkdir -p "$OUT"
DC="$REPO/apps/coc/coc-duplex-central"; DS="$REPO/apps/coc/coc-duplex-sink"
echo "### build+flash sink & central (25ms, FSU-on)"
west build -p always -b "$B" "$DS" -d "$DS/build" -- -DEXTRA_CONF_FILE=open-fsu.conf && west flash -d "$DS/build" --dev-id "$PER_ID" --no-rebuild
west build -p always -b "$B" "$DC" -d "$DC/build" -- -DEXTRA_CONF_FILE=open-fsu.conf -DCONFIG_APP_CONN_INT_UNITS="$U" && west flash -d "$DC/build" --dev-id "$CEN_ID" --no-rebuild
echo "### boot both; throwaway first-CoC-since-boot (NOT captured)"
nrfutil device reset --serial-number "$CEN_ID" >/dev/null; nrfutil device reset --serial-number "$PER_ID" >/dev/null; sleep 10
for r in $(seq 1 "$REPS"); do
  nrfutil device reset --serial-number "$PER_ID" >/dev/null   # PERIPH ONLY -> central auto-reconnects (subsequent)
  sleep 7
  tag="iso25_rep$(printf %02d "$r")"
  CAP_OUTDIR="$OUT" python3 "$REPO/tools/capture-tool.py" "$SECS" "$CEN_TTY:$tag-cen" "$PER_TTY:$tag-per" >/dev/null 2>&1
  echo "  $tag done"
done
echo "### DONE"
