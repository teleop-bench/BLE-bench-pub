# z54-gatt-dk — GATT notify throughput blaster (nRF54L15 / BLE 6)

Peripheral that streams 244 B GATT notifications as fast as credits allow (the uplink sender).
Pairs with [`z54-gatt-central`](../z54-gatt-central/) (the counting sink).

**Provenance / status:** the **nRF54L15 (BLE 6)** build of the GATT-notify benchmark; source is
byte-identical to the documented **BLE 5 / nRF52** twin [`apps/z44/z44-gatt-dk`](../../z44/z44-gatt-dk/).
This exact nRF54L15 pair is **not yet cited in any published result** — unpublished until a documented
run exists. Canonical map: [`docs/APP-PROVENANCE.md`](../../../docs/APP-PROVENANCE.md).
