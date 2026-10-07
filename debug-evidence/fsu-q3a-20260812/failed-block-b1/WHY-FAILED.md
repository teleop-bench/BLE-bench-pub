# ABBA block b1 (campaign ABBA-fsu-m0-9999e040-20260812-b1) — FAILED, preserved

seq0 f150 -> ACCEPT (150us plateau; RF 3049t; on-chip one 150 bin).
seq1 f100 -> QUARANTINED. Two independent problems, both real:

1. RUNNER SEQUENCING BUG (definite, fixable): the steady-f100 path takes the START
   snapshots BEFORE the pre-window FSU + 2.5s settle, so the snapshot->CAPTURE-START gap
   is ~2.7s. validate_common's "combined slop <= 3% of capture" gate (ceil 900ms)
   REJECTS at C=2791ms/P=2813ms. START snaps must be taken AFTER the settle, right
   before GO. This alone blocks steady-f100 acceptance.

2. FSU-REDUCED tIFS NOT PRESENT in the steady window (RF + on-chip AGREE):
   - central+periph both logged Q3FSU-DONE spacing=100 at ~10031ms (3.1s after Q2CONN
     at 6898ms);
   - yet the RF observer median gap_proxy = 3047t (150.5us, n=292, zero spread), and the
     on-chip histogram = ONE 150 bin (n=600, med=140), ZERO 100 bins.
   => the negotiated 100us spacing did not take effect on air during the window.

   Contrast: the two mid-step f100 primary cells (accepted) DO show the reduction — RF
   step 800/802t and on-chip BOTH 150 and 100 bins — when the FSU is triggered ~8s into
   a stable connection DURING capture. In steady the FSU is triggered ~immediately after
   connection and measured later, and the reduction is absent.

Candidate causes to investigate (NOT resolved here): early-connection FSU may not
persist/apply the way a mid-connection FSU does; or something between F and the window
(settle, 'K' on-chip clear, arming) reverts tIFS. Needs controller-level investigation
+ a reviewed runner-sequencing fix BEFORE re-attempting ABBA. Not auto-retried (a fresh
block would fail identically) and the frozen rev-5 runner was NOT modified mid-collection.
