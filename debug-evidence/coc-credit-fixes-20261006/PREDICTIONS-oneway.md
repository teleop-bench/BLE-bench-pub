# Pre-registered (2026-10-06, before the run): is "CoC FSU ~0% at 7.5 ms" caused by per-segment credit returns?
Audit finding: the one-way CoC recipe sink (apps/coc/coc-sink) calls bt_l2cap_chan_give_credits(chan, 1) per received
segment, and the host sends one credit PDU per call, so most sink replies are 12-byte credit PDUs (~92 us) instead of
empty packets (~44 us). With 52 us gaps a 6th exchange per 7.5 ms event then no longer fits (5 -> 5), whereas GATT
goes 5 -> 6 (157 -> 189 KB/s).
Test: one-way 7.5 ms, central coc-duplex-central (v3) FSU on (52) vs off (150) (matched pair: only FSU_MIN differs),
sink = coc-duplex-sink with APP_UPLINK=n (batches credits: returns ~41 at once when < 24 of 64 remain), ABBA x2, n=4.
Prediction if the audit is right: FSU off ~157, FSU on ~185-190 KB/s (+18-20%, like GATT).
Falsified if FSU on stays ~157 (then per-segment credits are not the cause).
Downlink computed from the sink's cumulative byte counter (cum_total slope), window from FSU onset + 2 s.
