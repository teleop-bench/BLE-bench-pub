# Proposed upstream Zephyr documentation contributions (from this benchmark)

Each item is a documentation gap identified in the 2026-08-26 doc read
([`zephyr-doc-references.md`](./zephyr-doc-references.md) §"Doc gaps"), drafted here in a form that
could become an upstream docs PR or a Kconfig help-text patch. All numbers are **measured** on this
repo's rig (2× nRF54L15-DK, open `ll_sw_split` Zephyr v4.4.1 + fsu-m0, 2M PHY, DLE 251, 10 cm) and
carry that scoping — upstream text would generalize the *model* and present our figures as a worked
example. Evidence lives in `debug-evidence/tx-staging-20260826/` and `coc-technical-overview.md`.

Ordered by value / defensibility. Items 7–8 are trivial, immediately-submittable Kconfig fixes.

---

## 1. The per-connection-event airtime throughput model ★ (biggest gap)
**Target:** LE Controller architecture (`bluetooth-ctlr-arch.html`), or a new "BLE connection
throughput" tuning page. **Gap:** no Zephyr page states how many PDUs fit in a connection event, a
KB/s ceiling, or an airtime model. The only trace is `BT_CTLR_RX_BUFFERS` help ("18 packets @ 1 B,
7.5 ms, 2M") — a 1-byte-payload RX-sizing note, not a throughput model.

**Draft text:**
> On a single ACL connection the `ll_sw_split` controller does **not** use SoftDevice-style
> connection-event-length extension. An event airs PDUs back-to-back (each side's More-Data bit;
> `lll_conn.c`) until the TX queue drains or the connection-interval boundary arrives, whichever
> comes first. So in a saturating configuration (data always available, deep buffers) the packet count
> per event **can be airtime-bound** — a worked example, measured on nRF54L15/2M below, **not a guarantee
> that every build/config is airtime-bound** (a shallow pool, credit limits, or a slow producer can bind
> first):
>
>     PDUs/event ≈ (interval − reservation_margin) / t_cycle
>     t_cycle    = t_pdu + tIFS + t_ack + tIFS
>
> where, at 2M PHY, a max-size (251-octet) data PDU is `t_pdu ≈ 1.048 ms` of airtime, the peer's empty
> acknowledging PDU is `t_ack ≈ 44 µs`, and `tIFS = 150 µs` (BLE ≤5.4). The event uses ~85–90 % of the
> interval (the remainder is scheduling reservation margin).
>
> | interval | model PDUs/event | measured (251 B, 2M) | goodput |
> |----------|------------------|----------------------|---------|
> | 7.5 ms   | ~5.4             | 4.87                 | ~157 KB/s |
> | 15 ms    | ~10.8            | 9.64                 | ~152 KB/s |
> | 25 ms    | ~18              | 16.30                | ~158 KB/s |
> | 50 ms    | ~36              | ~32                  | ~145 KB/s |
>
> Because `t_cycle` is dominated by `t_pdu` + two `tIFS`, the practical L2CAP/GATT one-way ceiling on
> this stack is ~150–170 KB/s regardless of interval; longer intervals raise PDUs/event but not
> goodput. The only levers are PHY (2M halves `t_pdu`), payload packing (fill each PDU to DLE), and —
> on BLE 6.0 silicon — Frame Space Update, which cuts `tIFS` (150→52 µs) and raises PDUs/event by the
> corresponding ratio (measured +18.6 % at 15 ms).

**Evidence:** Stage 1/1b, `debug-evidence/tx-staging-20260826/FINDINGS.md`. **Form:** docs PR (new
subsection + table). Scope caveat: figures are nRF54L15/2M; the *model* is platform-neutral.

---

## 2. TX buffer sizing rule vs interval × PHY airtime ★
**Target:** LE Host buffers section (`bluetooth-le-host.html`) and/or `BT_BUF_ACL_TX_COUNT` /
`BT_L2CAP_TX_BUF_COUNT` Kconfig help. **Gap:** no rule ties the TX pool depth to how many PDUs an
event airs; readers over- or under-size blindly. No `K_FOREVER`-exhaustion warning in `net_buf`.

**Draft text:**
> For sustained streaming, size the outgoing pools to the **per-event PDU count** for your interval
> and PHY (item 1). If `BT_L2CAP_TX_BUF_COUNT` (PDU pool) or `BT_BUF_ACL_TX_COUNT` (host→controller
> in-flight window) is **below** that count, throughput is buffer-bound *below* the airtime ceiling;
> once both exceed it, extra depth is slack. Consequently deep buffers help at **long** intervals
> (a long event can drain a shallow pool and then idle) but do nothing at the short-interval
> throughput peak, where airtime binds first. The default of 3 is below the ~10 PDUs a 15 ms / 2M /
> 251 B event airs. Keep the application's own TX `net_buf` pool **separate** from the stack's
> `BT_L2CAP_TX_BUF_COUNT` pool: a producer that `net_buf_alloc(K_FOREVER)`s from the stack's pool can
> self-deadlock under exhaustion.

**Evidence:** deep(64)≡shallow(8) at 15 ms; buffer recovery only at 50 ms (§11.1, §13). **Form:**
docs PR + one added sentence to each Kconfig help.

---

## 3. seg_recv credit cadence and multi-segment throughput ★
**Target:** `BT_L2CAP_SEG_RECV` Kconfig help + L2CAP CoC API page. **Gap:** seg_recv is documented as
an API ("bypasses the fixed-function reassembler/credit issuer") with **no throughput rationale**.

**Draft text:**
> The default `recv` + `alloc_buf` receive path re-grants L2CAP credits only at **SDU boundaries**
> (a burst sized to receive the rest of the current SDU), so a multi-segment SDU stream hiccups once
> per SDU while the peer waits for the next credit grant. `seg_recv` bypasses the fixed-function
> issuer: the application holds a **steady credit pool** via `bt_l2cap_chan_give_credits()` and the
> peer streams segments back-to-back. For multi-segment SDUs this removes the per-SDU stall — measured
> **+28 %** goodput (480 B SDU / 2 segments) versus the standard path, bringing multi-segment
> throughput up to the single-segment airtime ceiling.

**Evidence:** ZEPHYR-4x-REVISIT (+28% seg_recv). **Form:** Kconfig help addition + one API-doc paragraph.

---

## 4. HCI Number-of-Completed-Packets window vs airtime capacity
**Target:** LE Host flow-control section / `BT_BUF_ACL_TX_COUNT` help. **Gap:** the interaction between
the in-flight window and per-event airtime capacity is undocumented.

**Draft text:**
> `BT_BUF_ACL_TX_COUNT` is the number of packets the host may have outstanding in the controller before
> it must wait for the HCI Number-of-Completed-Packets (NoCP) event. NoCP throttles throughput **only
> when this window is smaller than the per-event airtime capacity** (item 1); set it comfortably above
> that count and NoCP pacing is slack — the host then hands the controller exactly as many PDUs as air
> per event (measured 1:1). The window is **shared across all connections and both directions**, so on
> a busy or duplex link raise it proportionally.

**Evidence:** Stage 2 host-handed counter (`host_pulls/ev ≈ aired/ev`, 1:1). **Form:** docs PR.

---

## 5. GATT≈CoC one-way throughput + head-of-line behavior for a bulk-plus-control mix
**Target:** L2CAP/GATT comparison guidance + `BT_EATT` docs. **Gap:** no doc quantifies GATT-notify vs
CoC throughput, nor the duplex/head-of-line behavior, nor EATT's benefit for mixed traffic.

**Draft text:**
> GATT notify / write-without-response and L2CAP CoC both allocate from the same outgoing ACL pool and
> hit the same NoCP window and per-event airtime wall, so their **one-way throughput is comparable**
> (matched 244 B / 15 ms: CoC ~158 vs GATT ~156 KB/s here, single run per cell). They differ under a
> *mixed* load: on a single ATT bearer, a small latency-sensitive write queues behind bulk
> notifications (head-of-line blocking). `BT_EATT` opens multiple concurrent ATT bearers so the control
> signal can ride its own bearer; in our measurements this reshaped the latency tail but did not hold a
> 30 ms budget under saturation, whereas completion-pacing the bulk sender did.

**Evidence:** matched CoC-vs-GATT one-way (`debug-evidence/coc-vs-gatt-rangetest-20260825/`);
latency-under-load head-of-line ([coc-technical-overview.md](../overviews/coc-technical-overview.md) §11.2, §11.4). **Form:**
docs PR (comparison note + EATT use-case paragraph). *(EATT arm: first run 2026-08-26 was invalid
(seq-correlation defect); **fixed + valid ABBA rerun 2026-08-27**. Nuanced result — EATT lowers the
severe tail and halves the saturated median, but slightly raises the >30 ms rate and **does not meet a
30 ms budget**; so the honest contribution is "EATT reshapes the tail, is not a budget fix under
saturation," not "EATT gives a control lane." Bearer assignment still uninstrumented (mechanism inferred).)*

---

## 6. Latency-under-load / head-of-line design guidance
**Target:** application-development guide (design-guidance note). **Gap:** no guidance on stop-signal /
control-message latency when a shared channel saturates.

**Draft text:**
> When a latency-sensitive message shares a channel with saturating bulk traffic, its round-trip tail
> grows sharply at saturation — this is **head-of-line queueing, not packet loss** (the bulk fills the
> per-event airtime and the control message waits behind it). Mitigations, in order of cleanliness:
> a dedicated bearer (EATT bearer, or a separate L2CAP CoC channel with its own credits); or
> completion-pacing the bulk sender to a shallow outstanding depth so the queue never deepens; or
> simply not saturating. Throughput and control-latency are in tension on a shared bearer.

**Evidence:** latency-under-load campaign (RTT tail vs saturation; separate-channel/pacing fixes).
**Form:** docs PR (design-guidance callout).

---

## 7. Kconfig help fix — `BT_CTLR_ADVANCED_FEATURES` is a visibility switch (QUICK WIN)
**Target:** `Kconfig.ll_sw_split` `BT_CTLR_ADVANCED_FEATURES` help. **Gap:** widely misread as
"enable advanced controller features"; it only **unhides** sub-options and has no functional effect,
so `FORCE_MD_*` assignments are silently dropped unless it is set first. **Form:** one-line Kconfig
help clarification — trivial, immediately submittable.

## 8. Kconfig fix — FORCE_MD arm threshold + `BT_CONN_TX_MAX` deprecation note (QUICK WIN)
**Target:** `BT_CTLR_FORCE_MD_COUNT` and `BT_CONN_TX_MAX` help. **Gaps:** (a) `FORCE_MD` arms only
when `trx_cnt ≥ BT_BUF_ACL_TX_COUNT − 1`, so a *deep* TX pool silently disarms it — undocumented, a
classic throughput foot-gun; (b) `BT_CONN_TX_MAX` is marked `[DEPRECATED]` but is in fact
**referenced nowhere in the v4.4.1 code** (the completion array is sized by `BT_BUF_ACL_TX_COUNT`) —
the help should say it is inert so users don't tune it expecting an effect. **Form:** two Kconfig
help clarifications — trivial.

---

## Notes on contributing upstream
- Items **7–8** are the fastest path to a first accepted contribution (Kconfig help text; no code,
  no measurement dispute).
- Items **1–4** are the highest-value but need platform-neutral framing (present nRF54L/2M figures as
  a worked example, keep the model general) and a maintainer sanity-check on the airtime constants.
- Item **5**'s EATT claim and item **6** would be strongest paired with a **valid** (re-run) **EATT
  latency-under-load** arm — running that first would let us document it from measurement, not inference.
- We have filed **no** Zephyr issues from the TX-staging work (the "refill wall" was airtime, not a
  bug); these are **documentation** contributions, tracked here until shaped into PRs.
