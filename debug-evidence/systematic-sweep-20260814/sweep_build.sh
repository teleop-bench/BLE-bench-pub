#!/bin/zsh
# Build all sweep hexes. Central configs x 7 intervals + 6 periphs. Fresh dir per build (SDC reliability).
Z54C=$HOME/Desktop/develop/zenoh-pico-ble-test/z54-lat-central
Z54P=$HOME/Desktop/develop/zenoh-pico-ble-test/z54-lat-periph
COCC=/tmp/coc-cen-app; SCOC=/tmp/sdc-coc-cen; COCS=/tmp/coc-sink-app; SCOS=/tmp/sdc-coc-sink
OUT=/tmp/sweep; LOG=/tmp/sweep/build.log; : > $LOG
source $HOME/zephyrproject/.venv/bin/activate 2>/dev/null
source $HOME/zephyrproject/zephyr/zephyr-env.sh 2>/dev/null
B=nrf54l15dk/nrf54l15/cpuapp
bld(){ # $1=name $2=app $3=env(open|sdc) $4=extra $5=units(or -)
  local name=$1 app=$2 env=$3 extra=$4 u=$5 d=/tmp/sb-$1 hx
  rm -rf $d
  local uarg=""; [ "$u" != "-" ] && uarg="-DCONFIG_APP_CONN_INT_UNITS=$u"
  if [ "$env" = open ]; then
    west build -p always -b $B -d $d $app -- -DEXTRA_CONF_FILE="$extra" $uarg >>$LOG 2>&1
    hx=$d/zephyr/zephyr.hex
  else
    ( cd $HOME/ncs && unset ZEPHYR_BASE && nrfutil toolchain-manager launch --ncs-version v3.4.0 -- west build -p always -b $B -d $d $app -- -DEXTRA_CONF_FILE="$extra" $uarg ) >>$LOG 2>&1
    hx="$(ls $d/*/zephyr/zephyr.hex 2>/dev/null | head -1)"
  fi
  if [ -f "$hx" ]; then cp "$hx" $OUT/$name.hex; echo "OK  $name"; else echo "FAIL $name"; fi
}
# periphs (interval-agnostic)
bld p-gsink-open $Z54P open "tput-open.conf;fsu-open.conf" -
bld p-gsink-sdc  $Z54P sdc  "sdc-sel.conf;tput.conf;sdc-6x.conf;sdc-llbuf.conf" -
bld p-gecho-open $Z54P open "tput-echo-open.conf;fsu-open.conf" -
bld p-gecho-sdc  $Z54P sdc  "sdc-sel.conf;tput-echo.conf;sdc-6x.conf;sdc-llbuf.conf" -
bld p-csink-open $COCS open "fsu.conf" -
bld p-csink-sdc  $SCOS sdc  "sdc-sel.conf;sdc-fsu.conf" -
# centrals x intervals
for u in 6 12 20 30 40 60 80; do
  bld gatt-open-fsu-$u $Z54C open "tput-open.conf;fsu-open.conf;hh-fsu52.conf" $u
  bld gatt-open-off-$u $Z54C open "tput-open.conf" $u
  bld gatt-sdc-fsu-$u  $Z54C sdc  "sdc-sel.conf;tput.conf;hh-sdc-fsu.conf" $u
  bld gatt-sdc-off-$u  $Z54C sdc  "sdc-sel.conf;tput.conf;hh-sdc-off.conf" $u
  bld coc-open-fsu-$u  $COCC open "fsu-f52.conf" $u
  bld coc-open-off-$u  $COCC open "fsu-f150.conf" $u
  bld coc-sdc-fsu-$u   $SCOC sdc  "sdc-sel.conf;sdc-fsu-f52.conf" $u
  bld coc-sdc-off-$u   $SCOC sdc  "sdc-sel.conf;sdc-noFSU.conf" $u
  bld lat-open-$u $Z54C open "fsu-open.conf" $u
  bld lat-sdc-$u  $Z54C sdc  "sdc-sel.conf;sdc-6x.conf" $u
done
echo "=== BUILD PHASE DONE: $(ls $OUT/*.hex 2>/dev/null | wc -l) hexes ==="
