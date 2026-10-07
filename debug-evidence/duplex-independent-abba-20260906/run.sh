#!/usr/bin/env bash
# Independent (non-echo) CoC-duplex, ±FSU, ABBA — bench runner.
#
# WHAT THIS MEASURES (see README.md): the coc-duplex apps blast BOTH directions
# independently (central downlink thread + sink uplink thread, NOT an echo), and each
# direction is counted by a separate cumulative byte counter:
#   uplink   KB/s = slope of central's   "CENRX cum_total=<B>"      (bytes rx'd from peripheral)
#   downlink KB/s = slope of sink's       "SINK rx: ... cum_total=<B>" (bytes rx'd from central)
# So this yields a REAL per-direction symmetry number (unlike the echo rig, where the
# reverse stream is structurally coupled to the forward one) AND a clean FSU on/off delta.
#
# NOT TESTED ON HARDWARE — this is scaffolding. Verify the SDC arm builds at the bench
# (the SDC overlays are ported, not yet compiled). The open arm reuses proven configs.
#
# Fill these in (or export before running):
: "${B:=nrf54l15dk/nrf54l15/cpuapp}"     # board
: "${CEN_ID:?set CEN_ID to the central board dev-id (nrfutil device list)}"
: "${PER_ID:?set PER_ID to the peripheral/sink board dev-id}"
: "${CEN_TTY:?set CEN_TTY e.g. /dev/cu.usbmodemXXXX3 (central VCOM1)}"
: "${PER_TTY:?set PER_TTY e.g. /dev/cu.usbmodemYYYY3 (sink VCOM1)}"
: "${SECS:=60}"                          # capture seconds per arm
: "${STACKS:=open}"                      # "open" or "open sdc"
: "${INTERVALS:=20 40}"                  # units*1.25ms: 20=25ms, 40=50ms
: "${NCS:=v3.4.0}"                       # NCS version for the SDC arm
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${CAP_OUTDIR:=$REPO/debug-evidence/duplex-independent-abba-PREP/captures}"
mkdir -p "$OUT"
DC="$REPO/apps/coc/coc-duplex-central"; DS="$REPO/apps/coc/coc-duplex-sink"
ms() { echo $(( $1 * 125 / 100 )); }     # units -> ms (integer; 20->25, 40->50)

build() {  # build <appdir> <builddir> <extra_conf_csv> <sdc?> [extra -D...]
  local app="$1" bd="$2" conf="$3" sdc="$4"; shift 4
  local cmd=(west build -p always -b "$B" "$app" -d "$bd" -- -DEXTRA_CONF_FILE="$conf" "$@")
  if [ "$sdc" = sdc ]; then
    ( unset ZEPHYR_BASE && cd "$REPO" && nrfutil toolchain-manager launch --ncs-version "$NCS" -- "${cmd[@]}" )
  else
    "${cmd[@]}"
  fi
}
flash() { west flash -d "$1" --dev-id "$2" --no-rebuild; }
reset_settle() { nrfutil device reset --serial-number "$CEN_ID"; nrfutil device reset --serial-number "$PER_ID"; sleep 6; }
capture() { CAP_OUTDIR="$OUT" python3 "$REPO/tools/capture-tool.py" "$SECS" "$CEN_TTY:$1-cen" "$PER_TTY:$1-per"; }

for stack in $STACKS; do
  # --- sink: one build+flash per stack (config doesn't change between FSU arms) ---
  if [ "$stack" = open ]; then sconf="open-fsu.conf"; ssdc=open
  else                         sconf="sdc-sel.conf;sdc.conf"; ssdc=sdc; fi
  echo "### building+flashing SINK ($stack)"
  build "$DS" "$DS/build" "$sconf" "$ssdc"
  flash "$DS/build" "$PER_ID"

  for u in $INTERVALS; do
    M=$(ms "$u")
    # ABBA over FSU: on / off / off / on  (drift-cancelled)
    i=0
    for fsu in on off off on; do
      i=$((i+1))
      if [ "$stack" = open ]; then
        [ "$fsu" = on ] && cconf="open-fsu.conf" || cconf="open-nofsu.conf"; csdc=open
      else
        cconf="sdc-sel.conf;$([ "$fsu" = on ] && echo sdc-fsu.conf || echo sdc-nofsu.conf)"; csdc=sdc
      fi
      tag="${stack}_${M}ms_fsu-${fsu}_r${i}"
      echo "### ARM $tag  (central $cconf, interval ${M}ms)"
      build "$DC" "$DC/build" "$cconf" "$csdc" -DCONFIG_APP_CONN_INT_UNITS="$u"
      flash "$DC/build" "$CEN_ID"
      reset_settle
      capture "$tag"
      echo "    captured -> $OUT/$tag-{cen,per}.log"
    done
  done
done
echo "### DONE. Analyze each arm with the EXISTING tool: python3 tools/analyze.py <arm>-per.log (downlink) and <arm>-cen.log (uplink=CENRX)"
