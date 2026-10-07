#!/usr/bin/env bash
# Duplex confidence campaign (hands-off): test1 multi-session repro, test2 reversal control,
# test3 FSU-duplex (25ms steady-state via periph-only, + 7.5ms). Resilient: continues on per-arm
# failure. Results per session/arm dir. NO source/config edits (only -D + existing overlays).
: "${CEN_ID:?}" "${PER_ID:?}" "${CEN_TTY:?}" "${PER_TTY:?}"
: "${B:=nrf54l15dk/nrf54l15/cpuapp}" "${SECS:=30}" "${REPS:=10}" "${SESSIONS:=4}" "${SLEEP_BETWEEN:=9000}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"; ROOT="$(cd "$(dirname "$0")" && pwd)"
DC="$REPO/apps/coc/coc-duplex-central"; DS="$REPO/apps/coc/coc-duplex-sink"
log(){ echo "[$(date '+%m-%d %H:%M:%S')] $*"; }
bc(){ west build -p always -b "$B" "$DC" -d "$DC/build" -- -DEXTRA_CONF_FILE="$2" -DCONFIG_APP_CONN_INT_UNITS="$1" >/dev/null 2>&1 \
   && west flash -d "$DC/build" --dev-id "$CEN_ID" --no-rebuild >/dev/null 2>&1 && log "central u=$1 $2 OK" || log "central u=$1 $2 BUILD/FLASH FAIL"; }
bs(){ west build -p always -b "$B" "$DS" -d "$DS/build" -- -DEXTRA_CONF_FILE=open-fsu.conf >/dev/null 2>&1 \
   && west flash -d "$DS/build" --dev-id "$PER_ID" --no-rebuild >/dev/null 2>&1 && log "sink OK" || log "sink BUILD/FLASH FAIL"; }
rb(){ nrfutil device reset --serial-number "$CEN_ID" >/dev/null 2>&1; nrfutil device reset --serial-number "$PER_ID" >/dev/null 2>&1; }
rp(){ nrfutil device reset --serial-number "$PER_ID" >/dev/null 2>&1; }
rc(){ nrfutil device reset --serial-number "$CEN_ID" >/dev/null 2>&1; }
cap(){ CAP_OUTDIR="$2" python3 "$REPO/tools/capture-tool.py" "$SECS" "$CEN_TTY:$1-cen" "$PER_TTY:$1-per" >/dev/null 2>&1; }
arm_cold(){ local u=$1 d=$2 M=$(( $1*125/100 )); mkdir -p "$d"; for r in $(seq 1 "$REPS"); do rb; sleep 6; cap "cold_${M}ms_r$(printf %02d $r)" "$d"; done; log "cold ${M}ms x$REPS -> ${d#$ROOT/}"; }
arm_iso(){ local d=$2 M=$(( $1*125/100 )); mkdir -p "$d"; rb; sleep 10; for r in $(seq 1 "$REPS"); do rp; sleep 7; cap "iso_${M}ms_r$(printf %02d $r)" "$d"; done; log "iso ${M}ms x$REPS -> ${d#$ROOT/}"; }
arm_rev(){ local d=$2 M=$(( $1*125/100 )); mkdir -p "$d"; rb; sleep 10; for r in $(seq 1 "$REPS"); do rc; sleep 8; cap "rev_${M}ms_r$(printf %02d $r)" "$d"; done; log "rev ${M}ms x$REPS -> ${d#$ROOT/}"; }
log "CAMPAIGN start: SECS=$SECS REPS=$REPS SESSIONS=$SESSIONS SLEEP=${SLEEP_BETWEEN}s"
bs
# Session 0 = full battery
S="$ROOT/S0"; log "== SESSION 0 (battery) =="
bc 6  open-fsu.conf; arm_cold 6  "$S/stallrate"     # test1: 7.5ms cold
bc 20 open-fsu.conf; arm_cold 20 "$S/stallrate"     # test1: 25ms cold
arm_iso 20 "$S/isolation"                            # test1: 25ms steady-state (central still 25/fsu-on)
arm_rev 20 "$S/reversal"                             # test2: 25ms central-only reset
bc 20 open-fsu.conf;   arm_iso 20 "$S/fsu25_on"      # test3: 25ms steady-state FSU on
bc 20 open-nofsu.conf; arm_iso 20 "$S/fsu25_off"     # test3: 25ms steady-state FSU off
bc 6  open-fsu.conf;   arm_cold 6 "$S/fsu7_on"       # test3: 7.5ms FSU on
bc 6  open-nofsu.conf; arm_cold 6 "$S/fsu7_off"      # test3: 7.5ms FSU off
log "session 0 done"
for s in $(seq 1 $((SESSIONS-1))); do
  log "sleeping ${SLEEP_BETWEEN}s"; sleep "$SLEEP_BETWEEN"
  S="$ROOT/S$s"; log "== SESSION $s (repro) =="
  bc 6  open-fsu.conf; arm_cold 6  "$S/stallrate"
  bc 20 open-fsu.conf; arm_cold 20 "$S/stallrate"
  arm_iso 20 "$S/isolation"
  log "session $s done"
done
log "== CAMPAIGN DONE =="
