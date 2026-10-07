#!/usr/bin/env bash
# observer-smoke.sh — build and run an on-air FSU observer smoke test on the physical rig:
# 1x nRF52832 DK (PCA10040, the passive raw-radio observer) + 2x nRF54L15-DK (PCA10156, the link).
# It builds the three images, flashes them, connects the nRF54 pair, requests FSU mid-capture, and
# measures the inter-frame gap on air before/after (cross-checked against the peripheral's on-chip
# timer). Runs in --smoke mode: the verdict is always QUARANTINED by design (never an accepted cell);
# the result to read is the measured step. See REPRODUCE.md "On-air FSU observer".
#
# Usage:  tools/observer-smoke.sh [2m] [outdir]
#         2M only: the 1M path cannot be analyzed with current tooling (its frozen calibration is bound
#         to pre-2M-port analyzer versions; a new 1M calibration campaign would be needed).
# Needs:  the patched Zephyr tree (ZEPHYR_BASE set, `west` on PATH), nrfutil, python3 + pyserial.
# Env:    OBS_ID/OBS_TTY CEN_ID/CEN_TTY PER_ID/PER_TTY override board detection;
#         BUILD_DIR (default build/observer-smoke-<phy>); SKIP_BUILD=1 reuses existing builds;
#         ATTEMPTS (default 3): retries a retention shortfall (the known observer limit) or a missing
#         Q2CONN line (the central's log buffer dropped it) with a fresh rep.
set -euo pipefail
PHY="${1:-2m}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$REPO/observer-captures/$(date -u +%Y%m%dT%H%M%SZ)-$PHY}"
BUILD_DIR="${BUILD_DIR:-$REPO/build/observer-smoke-$PHY}"
die(){ echo "ERROR: $*" >&2; exit 1; }
say(){ printf '\n== %s ==\n' "$*"; }

case "$PHY" in
  2m) RUNNER=q2_run_2m.py; CEN_APP=apps/misc/q2-central-2m; PER_APP=apps/misc/q2-periph-2m
      CEN_CONF=fsu-f52.conf; PER_CONF=fsu.conf; ARM=f52; DWELL=6.0
      OBS_DEFS="-DQ2=1 -DAIRTIME_MIN_TICKS=256"
      CALIB=debug-evidence/q3-2m-corroboration-20260906/gap-proxy-calibration-2m.json ;;
  1m) die "1M is not supported: the frozen 1M calibration is bound to pre-2M-port analyzer versions, so current tooling rejects it (see REPRODUCE.md, On-air FSU observer)" ;;
  *) die "usage: $0 [2m] [outdir]" ;;
esac

command -v west >/dev/null || die "west not on PATH (activate your Zephyr venv)"
[ -n "${ZEPHYR_BASE:-}" ] || die "ZEPHYR_BASE not set (point it at the patched fsu-m0 Zephyr tree)"
command -v nrfutil >/dev/null || die "nrfutil not on PATH"
python3 -c 'import serial' 2>/dev/null || die "python3 pyserial missing (pip install pyserial)"

if [ -z "${OBS_ID:-}${CEN_ID:-}${PER_ID:-}" ]; then
  say "Detecting boards (nrfutil device list)"
  # one board per blank-line-separated block; observer console = vcom 0, nRF54 console = vcom 1
  DET="$(nrfutil device list 2>/dev/null | python3 -c '
import sys, re
for blk in re.split(r"\n\s*\n", sys.stdin.read()):
    lines = blk.strip().splitlines()
    if not lines or not re.match(r"\s*\d{6,}\s*$", lines[0]): continue
    sn = lines[0].strip()
    kind = "obs" if "PCA10040" in blk else ("nrf54" if "PCA10156" in blk else None)
    if not kind: continue
    want = "0" if kind == "obs" else "1"
    for ln in lines:
        m = re.search(r"(/dev/tty\S+), vcom: " + want, ln)
        if m: print(kind, sn, m.group(1).replace("/dev/tty.", "/dev/cu.")); break
')"
  NOBS=$(printf '%s\n' "$DET" | awk '$1=="obs"{n++} END{print n+0}')
  N54=$(printf '%s\n' "$DET" | awk '$1=="nrf54"{n++} END{print n+0}')
  [ "$NOBS" -eq 1 ] && [ "$N54" -eq 2 ] || die "need exactly 1 nRF52 DK (PCA10040) + 2 nRF54L15-DK (PCA10156); found $NOBS + $N54. Set OBS_ID/OBS_TTY CEN_ID/CEN_TTY PER_ID/PER_TTY by hand."
  OBS_ID=$(printf '%s\n' "$DET" | awk '$1=="obs"{print $2}'); OBS_TTY=$(printf '%s\n' "$DET" | awk '$1=="obs"{print $3}')
  CEN_ID=$(printf '%s\n' "$DET" | awk '$1=="nrf54"{print $2}' | sed -n 1p); CEN_TTY=$(printf '%s\n' "$DET" | awk '$1=="nrf54"{print $3}' | sed -n 1p)
  PER_ID=$(printf '%s\n' "$DET" | awk '$1=="nrf54"{print $2}' | sed -n 2p); PER_TTY=$(printf '%s\n' "$DET" | awk '$1=="nrf54"{print $3}' | sed -n 2p)
fi
echo "  observer = $OBS_ID $OBS_TTY"
echo "  central  = $CEN_ID $CEN_TTY"
echo "  periph   = $PER_ID $PER_TTY"

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  say "Building observer + $PHY endpoints (into $BUILD_DIR)"
  mkdir -p "$BUILD_DIR"
  # shellcheck disable=SC2086
  west build -p always -b nrf52dk/nrf52832 "$REPO/apps/nrf52/pca10040-radio-observer" -d "$BUILD_DIR/obs" -- $OBS_DEFS >"$BUILD_DIR.obs.log" 2>&1 \
    || die "observer build failed (see $BUILD_DIR.obs.log)"
  west build -p always -b nrf54l15dk/nrf54l15/cpuapp "$REPO/$CEN_APP" -d "$BUILD_DIR/central" -- -DEXTRA_CONF_FILE="$CEN_CONF" >"$BUILD_DIR.central.log" 2>&1 \
    || die "central build failed (see $BUILD_DIR.central.log)"
  west build -p always -b nrf54l15dk/nrf54l15/cpuapp "$REPO/$PER_APP" -d "$BUILD_DIR/periph" -- -DEXTRA_CONF_FILE="$PER_CONF" >"$BUILD_DIR.periph.log" 2>&1 \
    || die "peripheral build failed (see $BUILD_DIR.periph.log)"
fi

ATTEMPTS="${ATTEMPTS:-3}"
cd "$REPO"; mkdir -p "$(dirname "$OUT")"
n=1
while :; do
  DIR="$OUT-a$n"
  say "Running the $PHY observer smoke, attempt $n/$ATTEMPTS (flash 3 boards, capture ~30 s, analyze)"
  set +e
  python3 "$REPO/apps/misc/q2-central/$RUNNER" \
    --obs-port "$OBS_TTY" --central-port "$CEN_TTY" --periph-port "$PER_TTY" \
    --obs-devid "$OBS_ID" --central-devid "$CEN_ID" --periph-devid "$PER_ID" \
    --outdir "$DIR" --mode symmetric --calib "$CALIB" \
    --obs-app "$REPO/apps/nrf52/pca10040-radio-observer" --board nrf52dk/nrf52832 \
    --obs-prebuilt-build "$BUILD_DIR/obs" --central-build "$BUILD_DIR/central" --periph-build "$BUILD_DIR/periph" \
    --smoke --q3 --q3-arm "$ARM" --baseline-dwell-s "$DWELL" 2>&1 | tee "$DIR.runner.log"
  RC=${PIPESTATUS[0]}
  set -e
  A="$DIR/analysis.txt"
  if [ ! -f "$A" ]; then
    # The central's deferred log buffer can drop the Q2CONN line at connection time ("--- N messages
    # dropped ---", ~2% of boots); the runner then stops before capturing. A fresh connection fixes it.
    if grep -q 'no Q2CONN' "$DIR.runner.log" && [ "$n" -lt "$ATTEMPTS" ]; then
      echo "the central's Q2CONN line was not logged (log buffer drop) — retrying with a fresh connection"
      n=$((n+1)); continue
    fi
    die "no analysis.txt (runner exit $RC) — see the runner output above"
  fi
  say "Result (attempt $n)"
  grep -E 'whole-cell retention|PRE  median|POST median|PRIMARY step|cross-val|INCOMPLETE|REJECT' "$A" || true
  if grep -q 'METRICS-OK' "$A"; then
    echo "SMOKE PASS on attempt $n: on-air step measured and matches on-chip (verdict QUARANTINED is expected for --smoke)."
    echo "captures: $DIR (all attempts kept under $OUT-a*)"
    exit 0
  fi
  # Only a retention shortfall is retried: it is the known single-antenna observer limit, and the
  # protocol's answer is a fresh rep (every attempt is preserved; nothing is relaxed).
  if grep -qE 'INCOMPLETE: .*retention' "$A" && [ "$n" -lt "$ATTEMPTS" ]; then
    echo "retention shortfall (< 95%) — the known observer limit; retrying with a fresh rep"
    n=$((n+1)); continue
  fi
  echo "SMOKE DID NOT REACH METRICS-OK after $n attempt(s) (runner exit $RC). Retention shortfalls are the"
  echo "known observer limit: move the observer roughly equidistant from both nRF54 boards and re-run."
  echo "captures: $OUT-a*"
  exit 1
done
