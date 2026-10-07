#!/usr/bin/env bash
# run-campaigns.sh — run every current matched-arm campaign on the physical rig, unattended.
# (run-all.sh is the separate ~15 min archival smoke test of the August prebuilt one-way matrix.)
#
# Each step is one of the per-result tools documented in REPRODUCE.md; each builds its own firmware
# (matched FSU on/off arms, design gate before running), auto-detects the boards, gates every rep and
# prints its own summary. Steps are independent: a failed step is recorded and the run continues.
#
# Rig:   2x nRF54L15-DK (the link) — every step
#        1x nRF52832 DK (PCA10040, passive observer) — steps observer-smoke and onair only
# Needs: patched Zephyr tree (ZEPHYR_BASE set, `west` on PATH), nrfutil, python3 + pyserial;
#        SDC steps also need an NCS v3.4.0 workspace (NCS_ROOT=... or --ncs-root).
#
# Usage:
#   ./run-campaigns.sh --dry-run                 # print the plan and time estimate, run nothing
#   ./run-campaigns.sh                           # everything (~12 h)
#   ./run-campaigns.sh --smoke                   # one rep per arm, fewer intervals (~1.5 h): a wiring check
#   ./run-campaigns.sh --only gatt-open,lat-open # selected steps
#   ./run-campaigns.sh --skip-sdc --skip-observer
#   options: --out DIR (default campaigns-<date>), --ncs-root DIR
#
# Steps (name: what, tool):
#   gatt-open      GATT echo duplex FSU, open Zephyr, 7.5/15/25 ms        tools/gatt-duplex-fsu.py
#   gatt-sdc       same on Nordic SDC                                     tools/gatt-duplex-fsu.py
#   coc-open       L2CAP CoC duplex FSU, open Zephyr, 7.5/15/25 ms        tools/coc-duplex-fsu.py
#   coc-sdc        same on Nordic SDC                                     tools/coc-duplex-fsu.py
#   lat-open       stop-signal latency under load, FSU on/off             tools/latency-fsu.py
#   lat-mech       same + controller per-event counter (why FSU helps)    tools/latency-fsu.py --diag
#   lat-sdc        stop-signal latency under load on Nordic SDC           tools/latency-fsu.py --stack sdc
#   oneway         one-way FSU, Zephyr vs SDC, GATT + CoC, same session   tools/oneway-fsu.py (needs NCS)
#   observer-smoke on-air FSU gap step at 2M (nRF52 observer)             tools/observer-smoke.sh
#   onair          on-air packets per event on the GATT duplex link       tools/onair-duplex.py
#                  (reuses the gatt-open builds and the observer build)
#
# Absolute KB/s and RTT depend on the RF environment; compare FSU deltas and verdicts with the
# evidence dirs listed in docs/EVIDENCE-INDEX.md, not absolute numbers.
set -u
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

DRY=0; SMOKE=0; ONLY=""; SKIP_SDC=0; SKIP_OBS=0
OUT="campaigns-$(date +%Y%m%d-%H%M)"; NCS_ROOT="${NCS_ROOT:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1 ;;
    --smoke) SMOKE=1 ;;
    --only) ONLY=",$2,"; shift ;;
    --skip-sdc) SKIP_SDC=1 ;;
    --skip-observer) SKIP_OBS=1 ;;
    --out) OUT="$2"; shift ;;
    --ncs-root) NCS_ROOT="$2"; shift ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)"; exit 2 ;;
  esac
  shift
done

STEPS="gatt-open gatt-sdc coc-open coc-sdc lat-open lat-mech lat-sdc oneway observer-smoke onair"
want() {                                   # is step $1 selected?
  case "$1" in *-sdc|oneway) [ $SKIP_SDC = 1 ] && return 1 ;; esac
  case "$1" in observer-smoke|onair) [ $SKIP_OBS = 1 ] && return 1 ;; esac
  [ -z "$ONLY" ] && return 0
  case "$ONLY" in *",$1,"*) return 0 ;; esac
  return 1
}

# per-step command and time estimate (minutes, full / smoke)
IV="6,12,20"; [ $SMOKE = 1 ] && IV="20"
SM=""; [ $SMOKE = 1 ] && SM="--smoke"
LATCFG=""; [ $SMOKE = 1 ] && LATCFG="--configs 6:naive"
B="$OUT/builds"
cmd() {
  case "$1" in
    gatt-open) echo "python3 tools/gatt-duplex-fsu.py --stack open --intervals $IV $SM --builds $B/gatt-open --out $OUT/gatt-open" ;;
    gatt-sdc)  echo "python3 tools/gatt-duplex-fsu.py --stack sdc --ncs-root ${NCS_ROOT:-<NCS_ROOT>} --intervals $IV $SM --builds $B/gatt-sdc --out $OUT/gatt-sdc" ;;
    coc-open)  echo "python3 tools/coc-duplex-fsu.py --stack open --intervals $IV $SM --builds $B/coc-open --out $OUT/coc-open" ;;
    coc-sdc)   echo "python3 tools/coc-duplex-fsu.py --stack sdc --ncs-root ${NCS_ROOT:-<NCS_ROOT>} --intervals $IV $SM --builds $B/coc-sdc --out $OUT/coc-sdc" ;;
    lat-open)  echo "python3 tools/latency-fsu.py $LATCFG $SM --builds $B/lat-open --out $OUT/lat-open" ;;
    lat-mech)  echo "python3 tools/latency-fsu.py --diag ${LATCFG:---configs 6:naive,6:polite} $SM --builds $B/lat-mech --out $OUT/lat-mech" ;;
    lat-sdc)   echo "python3 tools/latency-fsu.py --stack sdc --ncs-root ${NCS_ROOT:-<NCS_ROOT>} $LATCFG $SM --builds $B/lat-sdc --out $OUT/lat-sdc" ;;
    oneway)    echo "python3 tools/oneway-fsu.py --coc-credit-batch --ncs-root ${NCS_ROOT:-<NCS_ROOT>} $SM --builds $B/oneway --out $OUT/oneway" ;;
    observer-smoke) echo "BUILD_DIR=$B/observer tools/observer-smoke.sh 2m $OUT/observer-smoke" ;;
    onair)     echo "python3 tools/onair-duplex.py --builds $B/gatt-open --obs-build $B/observer/obs --intervals $IV --out $OUT/onair" ;;
  esac
}
mins() {
  if [ $SMOKE = 1 ]; then
    case "$1" in gatt-*|coc-*) echo 8 ;; lat-*) echo 18 ;; oneway) echo 25 ;; observer-smoke) echo 12 ;; onair) echo 10 ;; esac
  else
    case "$1" in gatt-open|coc-open) echo 35 ;; gatt-sdc|coc-sdc) echo 45 ;; lat-open|lat-sdc) echo 100 ;;
                 lat-mech) echo 65 ;; oneway) echo 270 ;; observer-smoke) echo 12 ;; onair) echo 30 ;; esac
  fi
}

# ---- plan ----
total=0; plan=""
for s in $STEPS; do
  want "$s" || continue
  m=$(mins "$s"); total=$((total + m)); plan="$plan $s"
done
[ -z "$plan" ] && { echo "no steps selected"; exit 2; }
echo "== run-campaigns: $( [ $SMOKE = 1 ] && echo SMOKE || echo FULL ) plan, output in $OUT/"
for s in $plan; do printf '  %-15s ~%3s min   %s\n' "$s" "$(mins "$s")" "$(cmd "$s")"; done
printf '  total ~%d h %02d min\n' $((total / 60)) $((total % 60))
case " $plan " in *" onair "*) case " $plan " in *" gatt-open "*) ;; *) echo "  note: onair needs $B/gatt-open from a gatt-open step (same --out)";; esac ;; esac
[ $DRY = 1 ] && exit 0

# ---- prerequisites ----
die() { echo "ERROR: $*"; exit 1; }
[ -n "${ZEPHYR_BASE:-}" ] || die "ZEPHYR_BASE is not set (patched fsu-m0 tree; see REPRODUCE.md)"
command -v west >/dev/null || die "west not on PATH"
command -v nrfutil >/dev/null || die "nrfutil not on PATH"
python3 -c 'import serial' 2>/dev/null || die "python3 pyserial missing (pip install pyserial)"
case " $plan " in *-sdc*|*oneway*) [ -n "$NCS_ROOT" ] && [ -d "$NCS_ROOT" ] || die "SDC steps need --ncs-root (NCS v3.4.0) or --skip-sdc" ;; esac
mkdir -p "$OUT"
# keep a laptop host awake for the whole run (an idle sleep mid-capture kills a step; docs/LESSONS.md)
if command -v caffeinate >/dev/null; then caffeinate -dimsu -w $$ & fi

# ---- run ----
LOG="$OUT/run-campaigns.log"; : > "$OUT/status.txt"
for s in $plan; do
  c=$(cmd "$s"); echo "### $s  $(date '+%F %T')  $c" | tee -a "$LOG"
  start=$(date +%s)
  if bash -c "$c" >> "$LOG" 2>&1; then st=PASS
  else
    rc=$?; st="FAIL(rc=$rc)"
    # the tools exit 1 when any rep was rejected by a gate; that is a finished run, not a crash
    r="$OUT/$s/results.jsonl"
    [ -s "$r" ] && grep -q '"reject": \[\]' "$r" && st="DONE-WITH-REJECTS(rc=$rc)"
  fi
  el=$(( ($(date +%s) - start) / 60 ))
  echo "$s $st ${el}min" | tee -a "$OUT/status.txt" "$LOG"
done

echo; echo "== run-campaigns summary ($OUT/status.txt; full output $LOG)"
cat "$OUT/status.txt"
echo "PASS = every rep accepted; DONE-WITH-REJECTS = finished, some reps rejected by a gate (read that"
echo "step's summary in $LOG); FAIL = crashed or nothing accepted. Compare verdicts with docs/EVIDENCE-INDEX.md."
grep -q FAIL "$OUT/status.txt" && exit 1 || exit 0
