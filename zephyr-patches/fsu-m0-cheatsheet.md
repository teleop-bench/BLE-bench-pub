# M0 Phase-0 cheat-sheet: Frame Space Update PDUs & procedure

*2026-08-06. Sources, in trust order: (S1) upstream Zephyr host implementation
in our tree (hci_types.h, hci_core.c — merged, shipping); (S2) closed upstream
controller PR #99473 (author cvinayak, ll_sw_split maintainer, rebasing
kruithofa's #82324; closed STALE 2026-04-05, not rejected — diff archived as
`pr99473-fsu-reference.diff`); (S3) external-review citation of Core 6.2
§5.1.30 (accepted 2026-08-06); (S4) SDC observed behavior from our own bench
logs. Direct verbatim spec-text verification is OUTSTANDING (Core 6.x HTML on
bluetooth.com is one giant page; fetches return only the ToC) — every item
below is tagged with its source; nothing here is spec-quoted directly.*

## LL control PDUs (S2; consistent with S1 field-for-field)

| | opcode | fields (all little-endian) |
|---|---|---|
| LL_FRAME_SPACE_REQ | **0x3B** | fsu_min u16 (µs), fsu_max u16 (µs), phys u8 (bit0=1M,1=2M,2=Coded), spacing_types u16 |
| LL_FRAME_SPACE_RSP | **0x3C** | fsu u16 (µs, the SELECTED value), phys u8, spacing_types u16 |

Spacing-type bits (S2 pdu.h == S1 hci_types.h exactly):
bit0 T_IFS_ACL_CP (central→peripheral), bit1 T_IFS_ACL_PC (peripheral→central),
bit2 T_MCES, bit3 T_IFS_CIS, bit4 T_MSS_CIS. RFU masks applied on decode:
phys & 0x07, spacing_types & 0x1F.

## HCI surface (S1 — in-tree, shipping)

- Command **0x209D** LE Frame Space Update: handle, frame_space_min u16,
  frame_space_max u16, phys u8, spacing_types u16.
- Event **0x35** LE Frame Space Update Complete: status, handle, initiator u8
  (0=local host, 1=local controller, 2=peer), frame_space u16 (SELECTED),
  phys u8, spacing_types u16. Event mask bit 52.
- Feature bit **65** (`BT_LE_FEAT_BIT_FRAME_SPACE_UPDATE`); host gates event
  unmasking on `BT_FEAT_LE_FRAME_SPACE_UPDATE_SET(local features)`
  (hci_core.c:3638) — bit 65 is BEYOND the classic 64-bit LE feature mask
  (the Extended Feature Set page problem; see shim notes).

## Procedure model (S2 structure + S3 timing)

- Two-PDU llcp procedure using the COMMON local/remote state machines
  (lp_comm/rp_comm, like DLE) — **NO instant** (S2 adds PROC_FRAME_SPACE to
  the no-instant list in `proc_with_instant()`; S3: no shared activation
  instant exists for FSU).
- **Selection**: responder SELECTS one value in [min,max]; S2's rule:
  `selected = MAX(requested_min, own_floor)` where own_floor =
  CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US (52 µs default on our silicon). RSP
  carries the selected value only.
- **Adoption timing (S3, to re-verify against spec text when obtainable):**
  responder adopts BEFORE sending its RSP (S2 matches: decode REQ →
  `ull_fsu_update_eff()` → then encode RSP); initiator switches no later
  than SIX anchor points after receiving the RSP (S2 adopts immediately on
  RSP decode — earlier than the deadline, legal); transitional receive
  windows bridge the asymmetric interval (S2 does NOT implement these —
  M0 relies on immediate-adoption-both-ends + reductions-only; flagged as a
  bench simplification, mitigated by the Phase-4 physics gate).
- **Rejection**: LL_REJECT_EXT_IND carrying the REQ opcode +
  BT_HCI_ERR_UNSUPP_FEATURE_PARAM_VAL (S2; S3 concurs there is no "RSP with
  error"). S2 rejects Coded-PHY requests and CIS types without ISO.
- **UNKNOWN_RSP** from a pre-6.0 peer terminates the procedure cleanly and
  unmasks the feature as unsupported (S2 lp_comm path).

## Known defects in the S2 reference diff (fix list for the M0 rebase)

1. `LL_FEAT_BIT_FRAME_SPACE = 0U` placeholder (FIXME in-diff: feature bits
   >64 unhandled) → `feature_fsu()` is always false → local initiation
   no-ops. M0 shim must force/report bit 65 (bench-only, documented).
2. Change-detection comparisons INVERTED (`if (tifs == new) changed=1` —
   should be `!=`) in `ull_fsu_update_eff()`.
3. Debug `printk` left in `llcp_pdu_encode_fsu_rsp()`.
4. `#if defined(CONFIG_BT_PHY_UPDATE)` should be `CONFIG_BT_CTLR_PHY`.
5. NO HCI bridge: command 0x209D unhandled, Complete event 0x35 never
   emitted (the ntf is encoded as a raw FSU_RSP LL PDU; hci.c translation
   missing) = our Phase 3.
6. `tifs_hcto_us` (RX header-complete timeout) is never updated alongside
   tifs_rx_us — stock low-lat mode sets all three; verify at Phase 2.
7. No range validation beyond the floor clamp (fsu_max < fsu_min unchecked;
   no 10 ms upper bound).

## M0 deltas vs the reference diff

- Scope: ACL T_IFS_ACL_CP+PC only, 2M-first, reductions-only, single
  initiator (central) — reject everything else via LL_REJECT_EXT_IND.
- Fix defects 1–4, 6–7; implement Phase 3 (HCI cmd+evt); bench feature shim
  as a separate clearly-labeled patch.
- Floor: 52 µs (CONFIG_BT_CTLR_EVENT_IFS_LOW_LAT_US), the stock knob.

## Phase-4 status (2026-08-07, two nRF54L15-DK)

Negotiation sweep PASSED — fresh connection per step, SELECTED value
HCI-verified, zero disconnects every step: request[100..150]→100,
[70..150]→70, [52..150]→52. Two on-air bugs found+fixed en route (see
fsu-m0-series.patch commit 3): missing transitional receive windows
(link died 0x08 in seconds — responder's RSP flies at the new spacing
before the initiator can know), and the RSP encoding cleared masks
(phys=0/types=0 → initiator committed nothing, silently).

KNOWN M0 DEVIATION: a no-change request (e.g. [150..150] at default
spacing) completes on-air but generates NO HCI Complete event (ntf is
gated on fsu_changed) — the host callback never fires for it. The f150
control cell documents this; spec-correct always-report is M1 work.
PHYSICS GATE (2026-08-07, redesigned twice under external review —
full chronology in debug-evidence/fsu-m0-20260807/PROVENANCE.txt):
the original 2M completion-floor sweep was NON-IDENTIFYING (observed ~5
pairs/event put every arm in the same realized quantization bin; pump
ceiling; batched completion gaps). Final design: 1M, 53.75 ms interval
(21 vs 22 pairs robust across 0-800 us event overhead), 150-vs-100 us,
AB BA BA AB, deep buffers, asserts-off (-ECANCELED fallthrough;
cancellation rates arm-matched 17-18/s and gate-checked). RESULT: the
indirect physics discriminator PASSES on both registered observables —
throughput +5.72% [1.13, 10.31] n=4 (model +4.76% in-CI; cancel medians
17 vs 17 /s window-matched) and within-run cgap step -102..-103 us on
4/4 100-arms vs registered -100 +/- 15 (150-arms flat; absolute pair
times within ~5 us of model at both spacings; zero in-window reboots,
pairs 1-2 non-truncated). Strong evidence that negotiated 150->100 us
spacing changes on-air packet pacing at 1M; direct physical verification
remains pending. NOT covered physically: the
52 us floor (1M + tIFS<=60 is a historically failing region of the stock
controller; 100 us survived the measurement-length trials) and 2M
spacings. Qualified on-air capture remains the formal physical gate.
Floor soak (2M/52, 780 s + reconnect probe): PASS on liveness/stability
(2x spacing=52 confirms incl. post-reconnect renegotiation; 0 disc/
timeouts/cancels; 10.4 min at floor) — with one OPEN OBSERVATION:
serialized-RTT means elevated/jittery (med 12.6 ms vs 11.9) from before
FSU adoption, minima still 11.914 ms; mechanism undetermined (needs an
interleaved A/B latency soak before quoting 2M/52 latency). FSU-off
regression: PASS all behavioral bounds (11.915 ms median, zeros across
the board; hash-identity unavailable by design). HEADLINE: Phase-4
execution is complete. Negotiation, indirect pacing evidence, 2M/52 us
liveness and FSU-off regression passed; the preregistered RTT sanity
gate FAILED on the soak run, and the follow-up counterbalanced latency
A/B (8/8 valid cells): +28 us [-62,+119] — inside the +/-150 us
equivalence band; all eight cell median-of-minima values reported as
11911 us, every paired difference zero. No practically meaningful
reproducible 52-us effect on fresh-run serialized RTT; a persistent
effect of the soak's ~700 us magnitude is excluded, but the isolated
soak anomaly's cause remains unidentified (stateful/duration-dependent
interaction not excluded). Direct on-air
verification remains the only pending Phase-4 item.
