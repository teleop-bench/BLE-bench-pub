# z54-gatt-central — GATT notify throughput SINK (nRF54L15 / BLE 6)

Central that subscribes to the peripheral's GATT notifications and counts application goodput
(uplink, peripheral→central). Pairs with [`z54-gatt-dk`](../z54-gatt-dk/) (the notify blaster).

**Provenance / status:** this is the **nRF54L15 (BLE 6)** build of the GATT-notify uplink benchmark.
Its source is byte-identical to the documented **BLE 5 / nRF52** twin
[`apps/z44/z44-gatt-central`](../../z44/z44-gatt-central/), whose result *is* documented
(`apps/nrf52/nrf52-l2cap-echo/UPLINK-RECONNECT-FINDINGS.md`, GATT-vs-CoC section). This exact nRF54L15
notify pair, however, is **not yet cited in any published result** — treat its numbers as unpublished
until a documented run exists. See the canonical map: [`docs/APP-PROVENANCE.md`](../../../docs/APP-PROVENANCE.md).

(The published nRF54L15 GATT *throughput/FSU* result comes from `z54-lat-central`, not this app —
follow the provenance chain, not the app name.)
