# Post-FSU / Student-t re-analysis (derived, read-only) — 2026-09-08

Persists the corrected statistics the technical report cites, which were previously computed ad-hoc. This
dir **reads** sibling evidence dirs and **does not modify them** (repo rule: `debug-evidence/**` is
immutable once archived).

- **CoC one-way FSU** (`fsu-interval-sweep` / `sdc-fsu-interval-sweep`) recomputed over a **post-FSU window
  (last 12 s)** with **Student-t 95%** CIs. The original summaries in those dirs use whole-run medians +
  1.96·SE and are left as-is; this is the derived view the report §3 CoC rows quote.
- **CoC duplex FSU** (`coc-duplex-fsu-interval`) recomputed with **Student-t** (the archived harness prints
  1.96·SE; the corrected CI lives here rather than by editing that harness). Only SDC-duplex 7.5 ms has
  enough accepted paired rounds: **+0.5% ± 1.7% (t, n=3)**.

`reanalyze.py` regenerates `summary.txt`. Results match `docs/investigations/fsu-benchmark-technical-report.md`
§3 (CoC) and §5 (duplex). Absolute KB/s are RF-day-specific; single session.
