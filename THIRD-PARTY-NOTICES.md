# Third-party notices

This repository's own source code, documentation and data are licensed under the Apache License 2.0
(see [LICENSE](LICENSE) and [NOTICE](NOTICE)). The compiled firmware images (`*.hex`) in
[`prebuilt-hexes/`](prebuilt-hexes/) and [`debug-evidence/`](debug-evidence/)
also contain third-party code under the licenses below. Their notices are reproduced here as those
licenses require for binary redistribution.

## 1. Nordic SoftDevice Controller and MPSL — `LicenseRef-Nordic-5-Clause` (161 images)

The firmware images listed below are linked against Nordic Semiconductor's proprietary **SoftDevice
Controller** and **Multiprotocol Service Layer (MPSL)** libraries from nRF Connect SDK v3.4.0
(`nrfxlib/softdevice_controller`, `nrfxlib/mpsl`). **These images are not covered by the Apache License
2.0.** Under the license below they may be used only with a Nordic Semiconductor integrated circuit
(condition 4), and the binaries must not be reverse engineered, decompiled, modified or disassembled
(condition 5). Every other image in this repository uses the open-source Zephyr controller instead.

To build these images yourself rather than use the copies here, install nRF Connect SDK v3.4.0 from
[github.com/nrfconnect/sdk-nrf](https://github.com/nrfconnect/sdk-nrf) and follow the SDC recipes in
[REPRODUCE.md](REPRODUCE.md).

Images under this license:

- `debug-evidence/coc-duplex-fsu-interval-20260908/firmware-sdc/` (11): `dupsweep-sdc_off12_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_off20_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_off30_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_off40_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_off6_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_on12_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_on20_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_on30_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_on40_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_on6_coc-duplex-central_zephyr_zephyr.hex`, `dupsweep-sdc_sink_coc-duplex-sink_zephyr_zephyr.hex`
- `debug-evidence/coc-autoupdate-confound-20261005/firmware/` (3): `central-sdc-coc-fsu-7p5.hex`, `sink-sdc-au-off.hex`, `sink-sdc-au-on.hex`
- `debug-evidence/coc-duplex-fsu-matched-20261003/firmware-sdc/` (7): `c-off-12.hex`, `c-off-20.hex`, `c-off-6.hex`, `c-on-12.hex`, `c-on-20.hex`, `c-on-6.hex`, `sink.hex`
- `debug-evidence/duplex-fsu-abba-20260824/sdc-parity-samesession/firmware/` (3): `sdc-c-off.hex`, `sdc-c-on.hex`, `sdc-pgecho.hex`
- `debug-evidence/gatt-duplex-fsu-model-20261003/sdc-15ms/firmware/` (3): `c-off-12.hex`, `c-on-12.hex`, `p-echo.hex`
- `debug-evidence/gatt-duplex-fsu-model-20261003/sdc-7p5-25ms/firmware/` (5): `c-off-20.hex`, `c-off-6.hex`, `c-on-20.hex`, `c-on-6.hex`, `p-echo.hex`
- `debug-evidence/latency-fsu-20261004/firmware-b-sdc/` (7): `c-off-20-naive.hex`, `c-off-6-naive.hex`, `c-off-6-polite.hex`, `c-on-20-naive.hex`, `c-on-6-naive.hex`, `c-on-6-polite.hex`, `periph.hex`
- `debug-evidence/oneway-crossstack-20261005/firmware/` (32, files containing `sdc`): `max-coc-sdc-c-off-12.hex`, `max-coc-sdc-c-off-20.hex`, `max-coc-sdc-c-off-30.hex`, `max-coc-sdc-c-off-40.hex`, `max-coc-sdc-c-off-6.hex`, `max-coc-sdc-c-on-12.hex`, `max-coc-sdc-c-on-20.hex`, `max-coc-sdc-c-on-30.hex`, `max-coc-sdc-c-on-40.hex`, `max-coc-sdc-c-on-6.hex`, `max-coc-sdc-sink.hex`, `max-gatt-sdc-c-off-12.hex`, `max-gatt-sdc-c-off-20.hex`, `max-gatt-sdc-c-off-30.hex`, `max-gatt-sdc-c-off-40.hex`, `max-gatt-sdc-c-off-6.hex`, `max-gatt-sdc-c-on-12.hex`, `max-gatt-sdc-c-on-20.hex`, `max-gatt-sdc-c-on-30.hex`, `max-gatt-sdc-c-on-40.hex`, `max-gatt-sdc-c-on-6.hex`, `max-gatt-sdc-sink.hex`, `std-coc-sdc-c-off-40.hex`, `std-coc-sdc-c-off-6.hex`, `std-coc-sdc-c-on-40.hex`, `std-coc-sdc-c-on-6.hex`, `std-coc-sdc-sink.hex`, `std-gatt-sdc-c-off-40.hex`, `std-gatt-sdc-c-off-6.hex`, `std-gatt-sdc-c-on-40.hex`, `std-gatt-sdc-c-on-6.hex`, `std-gatt-sdc-sink.hex`
- `debug-evidence/coc-credit-policy-20261006/firmware/` (4, files containing `sdc`): `sdc-c-off.hex`, `sdc-c-on.hex`, `sdc-sink-batch.hex`, `sdc-sink-seg.hex`
- `debug-evidence/coc-duplex-fsu-fixed-20261006/firmware-sdc/` (7, files containing `sdc`): `sdc-c-off-12.hex`, `sdc-c-off-20.hex`, `sdc-c-off-6.hex`, `sdc-c-on-12.hex`, `sdc-c-on-20.hex`, `sdc-c-on-6.hex`, `sdc-sink.hex`
- `debug-evidence/oneway-coc-batched-20261006/firmware/` (11, files containing `sdc`): `max-coc-sdc-c-off-12.hex`, `max-coc-sdc-c-off-20.hex`, `max-coc-sdc-c-off-30.hex`, `max-coc-sdc-c-off-40.hex`, `max-coc-sdc-c-off-6.hex`, `max-coc-sdc-c-on-12.hex`, `max-coc-sdc-c-on-20.hex`, `max-coc-sdc-c-on-30.hex`, `max-coc-sdc-c-on-40.hex`, `max-coc-sdc-c-on-6.hex`, `max-coc-sdc-sink.hex`
- `debug-evidence/replication-20261007/firmware/` (18, files containing `sdc`): `duplex-sdc-c-off-12.hex`, `duplex-sdc-c-off-20.hex`, `duplex-sdc-c-off-6.hex`, `duplex-sdc-c-on-12.hex`, `duplex-sdc-c-on-20.hex`, `duplex-sdc-c-on-6.hex`, `duplex-sdc-sink.hex`, `oneway-max-coc-sdc-c-off-12.hex`, `oneway-max-coc-sdc-c-off-20.hex`, `oneway-max-coc-sdc-c-off-30.hex`, `oneway-max-coc-sdc-c-off-40.hex`, `oneway-max-coc-sdc-c-off-6.hex`, `oneway-max-coc-sdc-c-on-12.hex`, `oneway-max-coc-sdc-c-on-20.hex`, `oneway-max-coc-sdc-c-on-30.hex`, `oneway-max-coc-sdc-c-on-40.hex`, `oneway-max-coc-sdc-c-on-6.hex`, `oneway-max-coc-sdc-sink.hex`
- `debug-evidence/replication-20261004/firmware-b-coc-sdc/` (7): `c-off-12.hex`, `c-off-20.hex`, `c-off-6.hex`, `c-on-12.hex`, `c-on-20.hex`, `c-on-6.hex`, `sink.hex`
- `debug-evidence/replication-20261004/firmware-b-gatt-sdc/` (7): `c-off-12.hex`, `c-off-20.hex`, `c-off-6.hex`, `c-on-12.hex`, `c-on-20.hex`, `c-on-6.hex`, `p-echo.hex`
- `debug-evidence/sdc-fsu-corrected-20260908/firmware/` (6): `coc_off_coc-central_zephyr_zephyr.hex`, `coc_on_coc-central_zephyr_zephyr.hex`, `coc_sink_coc-sink_zephyr_zephyr.hex`, `gatt_off_z54-lat-central_zephyr_zephyr.hex`, `gatt_on_z54-lat-central_zephyr_zephyr.hex`, `gatt_sink_z54-lat-periph_zephyr_zephyr.hex`
- `debug-evidence/sdc-fsu-interval-sweep-20260908/firmware/` (22): `sdc_coc_off_coc-central_zephyr_zephyr.hex`, `sdc_coc_on_coc-central_zephyr_zephyr.hex`, `sdc_coc_sink_coc-sink_zephyr_zephyr.hex`, `sdc_gatt_off_z54-lat-central_zephyr_zephyr.hex`, `sdc_gatt_on_z54-lat-central_zephyr_zephyr.hex`, `sdc_gatt_sink_z54-lat-periph_zephyr_zephyr.hex`, `sdcsweep_coc_off_20_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_off_30_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_off_40_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_off_6_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_on_20_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_on_30_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_on_40_coc-central_zephyr_zephyr.hex`, `sdcsweep_coc_on_6_coc-central_zephyr_zephyr.hex`, `sdcsweep_gatt_off_12_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_off_20_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_off_30_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_off_40_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_on_12_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_on_20_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_on_30_z54-lat-central_zephyr_zephyr.hex`, `sdcsweep_gatt_on_40_z54-lat-central_zephyr_zephyr.hex`
- `prebuilt-hexes/` (8): `c-coc-sdc-fsu-15.hex`, `c-coc-sdc-off-15.hex`, `c-gatt-sdc-fsu-25.hex`, `c-gatt-sdc-off-25.hex`, `p-coc-sink-sdc-noau.hex`, `p-coc-sink-sdc.hex`, `p-gatt-echo-sdc.hex`, `p-gatt-sink-sdc.hex`

License text (as distributed in nRF Connect SDK v3.4.0, `nrf/LICENSE`):

```
Copyright (c) 2018, Nordic Semiconductor ASA

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form, except as embedded into a Nordic
   Semiconductor ASA integrated circuit in a product or a software update for
   such product, must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other
   materials provided with the distribution.

3. Neither the name of Nordic Semiconductor ASA nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

4. This software, with or without modification, must only be used with a
   Nordic Semiconductor ASA integrated circuit.

5. Any software provided in binary form under this license must not be reverse
   engineered, decompiled, modified and/or disassembled.

THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

MPSL additionally carries the following attribution (`nrfxlib/mpsl/LICENSE-ATTRIBUTION.txt`):

```
This software product uses functions that are covered by the BSD-3-Clause
license included below:

Copyright (c) 2013 ARM Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the company may not be used to endorse or promote
   products derived from this software without specific prior written
   permission.

THIS SOFTWARE IS PROVIDED BY ARM LTD ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL ARM LTD BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## 2. Nordic nrfx drivers — BSD-3-Clause (all firmware images)

Every firmware image includes Nordic's nrfx peripheral drivers (Zephyr module `hal_nordic`):

```
Copyright (c) 2015 - 2026, Nordic Semiconductor ASA
All rights reserved.

SPDX-License-Identifier: BSD-3-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
 
```

## 3. Zephyr RTOS and modules — Apache-2.0 (all firmware images)

Every firmware image is built on the Zephyr RTOS ([github.com/zephyrproject-rtos/zephyr](https://github.com/zephyrproject-rtos/zephyr)),
licensed under the Apache License 2.0 (see [LICENSE](LICENSE)), including the `fsu-m0` controller patches
described in [NOTICE](NOTICE). Individual Zephyr modules compiled into a given image carry their own
licenses; see the license files of the corresponding module in the Zephyr or nRF Connect SDK
workspace for the version cited in that image's evidence.

## 4. Dual-licensed source

[`apps/misc/l2cap-link-skeleton/`](apps/misc/l2cap-link-skeleton/) is written in the style of
zenoh-pico and is offered under `EPL-2.0 OR Apache-2.0`, matching that project's licensing.
