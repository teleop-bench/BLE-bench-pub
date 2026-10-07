#!/usr/bin/env bash
# run-all.sh — archival August-2026 prebuilt hardware smoke test, NOT a current headline benchmark.
# The old prebuilt matrix includes a subsequently retracted near-zero CoC FSU replay; current
# comparisons require matched resolved configs, FSU-capable sinks, and post-onset windowing.
#
# SCOPE: nRF54L15-DK (Bluetooth 6) ONLY — the FSU-capable boards behind these results. The earlier
# nRF52 (Bluetooth 5) benchmarks are a separate, non-FSU story (nRF52 HW doesn't support FSU) and are
# NOT part of this script; they're catalogued in docs/APP-PROVENANCE.md.
#
# Prerequisites (that's the real bar, not the command):
#   - 2 x Nordic nRF54L15-DK plugged in by USB
#   - `nrfutil` (Nordic) on PATH        (flashing)      https://www.nordicsemi.com/nrfutil
#   - python3 + pyserial (`pip install pyserial`)       (serial capture)
#
# Usage:
#   ./run-all.sh                        # auto-detects the two boards
#   CEN_ID=.. PER_ID=.. CEN_TTY=.. PER_TTY=.. ./run-all.sh   # manual (see `nrfutil device list`)
#
# Time: ~15 min (8 configs x ~90 s). This script aborts on failed capture/gates and deliberately
# prints no FSU/open-vs-SDC deltas. See REPRODUCE.md for current matched campaigns.
set -euo pipefail
REPO="$(cd "$(dirname "$0")" && pwd)"; HEX="$REPO/prebuilt-hexes"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$"
OUT="${OUT:-$REPO/run-all-captures}/$RUN_ID"; SECS="${SECS:-40}"; mkdir -p "$OUT"
say(){ printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
die(){ echo "ERROR: $*" >&2; exit 1; }

# ---- prerequisites ----
command -v nrfutil >/dev/null || die "nrfutil not on PATH (needed to flash)."
command -v python3 >/dev/null || die "python3 not found."
python3 -c "import serial" 2>/dev/null || die "pyserial missing -> pip install pyserial"
[ -f "$HEX/SHA256SUMS" ] || die "missing prebuilt-hexes/SHA256SUMS"
( cd "$HEX" && if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS; else shasum -a 256 -c SHA256SUMS; fi >/dev/null ) || die "prebuilt hex SHA256 mismatch"
echo "prebuilt hex SHA256: OK"
say "ARCHIVAL SMOKE ONLY — no headline deltas; failed gates abort"

# ---- detect the two nRF54L15-DK boards (serial number + VCOM1 console) ----
if [ -z "${CEN_ID:-}" ] || [ -z "${PER_ID:-}" ] || [ -z "${CEN_TTY:-}" ] || [ -z "${PER_TTY:-}" ]; then
  say "Detecting boards (nrfutil device list)"
  DET="$(nrfutil device list 2>/dev/null | python3 -c '
import sys,re
txt=sys.stdin.read()
for blk in re.split(r"\n\s*\n", txt):   # one board per blank-line-separated block
    m=re.match(r"\s*(\d{6,})\s*$", blk.splitlines()[0]) if blk.strip() else None
    if not m or "PCA10156" not in blk: continue
    sn=m.group(1); tty=None
    for ln in blk.splitlines():
        mm=re.search(r"(/dev/tty\S+), vcom: 1", ln)
        if mm: tty=mm.group(1).replace("/dev/tty.", "/dev/cu.")
    if tty: print(sn, tty)
')"
  N=$(printf '%s\n' "$DET" | awk 'NF{n++} END{print n+0}')
  [ "$N" -eq 2 ] || die "found $N nRF54L15-DK boards (need exactly 2). Set CEN_ID/PER_ID/CEN_TTY/PER_TTY by hand (see 'nrfutil device list')."
  CEN_ID=$(printf '%s\n' "$DET" | sed -n 1p | awk '{print $1}'); CEN_TTY=$(printf '%s\n' "$DET" | sed -n 1p | awk '{print $2}')
  PER_ID=$(printf '%s\n' "$DET" | sed -n 2p | awk '{print $1}'); PER_TTY=$(printf '%s\n' "$DET" | sed -n 2p | awk '{print $2}')
fi
echo "  central   = $CEN_ID  $CEN_TTY"
echo "  periph    = $PER_ID  $PER_TTY"
echo "  (roles are arbitrary but consistent; override with CEN_*/PER_* if you care which is which)"

flash(){ nrfutil device program --firmware "$1" --serial-number "$2" >/dev/null 2>&1 \
      && nrfutil device reset --serial-number "$2" >/dev/null 2>&1 || die "flash failed: $1 -> $2 (check nrfutil / board)"; }

# ---- the 8 one-way configs: name | central-hex | periph-hex ----
CONFIGS='GATT|open|on |c-gatt-open-fsu-7p5|p-gatt-sink-open
GATT|open|off|c-gatt-open-off-7p5|p-gatt-sink-open
GATT|SDC |on |c-gatt-sdc-fsu-25|p-gatt-sink-sdc
GATT|SDC |off|c-gatt-sdc-off-25|p-gatt-sink-sdc
CoC |open|on |c-coc-open-fsu-15|p-coc-sink-open-noau
CoC |open|off|c-coc-open-off-15|p-coc-sink-open-noau
CoC |SDC |on |c-coc-sdc-fsu-15|p-coc-sink-sdc-noau
CoC |SDC |off|c-coc-sdc-off-15|p-coc-sink-sdc-noau'

printf '\n%-4s %-4s %-4s %s\n' transport stack fsu "gate"
echo   "---------------------------------------"
printf '%s\n' "$CONFIGS" | while IFS='|' read -r tp st fsu cen per; do
  tp=${tp// /}; st=${st// /}; fsu=${fsu// /}
  tag="${tp}_${st}_fsu-${fsu}"
  [ -f "$HEX/$per.hex" ] && [ -f "$HEX/$cen.hex" ] || die "missing hex for $tag"
  flash "$HEX/$per.hex" "$PER_ID"          # peripheral first (programs + resets)
  flash "$HEX/$cen.hex" "$CEN_ID"
  # Capture from the RESET INSTANT: the FSU confirmation prints once during setup. Open serial
  # first, then reset both boards into it; the per-capture gate checks the held time-series too.
  CAP_OUTDIR="$OUT" python3 "$REPO/tools/capture-tool.py" "$SECS" "$CEN_TTY:$tag-cen" "$PER_TTY:$tag-per" &
  CAPPID=$!
  sleep 1                                  # let capture-tool open the ports
  nrfutil device reset --serial-number "$PER_ID" >/dev/null 2>&1 || die "peripheral reset failed: $tag"
  nrfutil device reset --serial-number "$CEN_ID" >/dev/null 2>&1 || die "central reset failed: $tag"
  wait "$CAPPID" || die "capture failed: $tag"
  [ -s "$OUT/$tag-cen.log" ] && [ -s "$OUT/$tag-per.log" ] || die "empty/missing capture: $tag"
  grep -qE 'PHY.*tx=2' "$OUT/$tag-cen.log" || die "2M PHY missing: $tag"
  if [ "$tp" = CoC ]; then           # the GATT firmware never logs data length; CoC does
    grep -qE 'DLE.*251' "$OUT/$tag-cen.log" "$OUT/$tag-per.log" || die "DLE 251 missing: $tag"
  fi
  if [ "$tp" = GATT ]; then
    if [ "$st" = open ]; then interval=7.5; else interval=25; fi
  else interval=15; fi
  python3 "$REPO/tools/verify_run.py" "$OUT/$tag-per.log" --cen "$OUT/$tag-cen.log" \
    --fsu "$fsu" --stack "$(printf %s "$st" | tr "[:upper:]" "[:lower:]")" --interval-ms "$interval" || die "run gate rejected $tag"
  python3 "$REPO/tools/analyze.py" "$OUT/$tag-per.log"
  printf '%-4s %-4s %-4s %s\n' "$tp" "$st" "$fsu" "PASS (historical smoke only)"
done
echo "Archival smoke complete; captures in: $OUT"
echo "Do not compute current deltas from these binaries. Use REPRODUCE.md's matched campaign."
