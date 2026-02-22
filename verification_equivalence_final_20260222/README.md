# Final Rigorous Equivalence Bundle (2026-02-22)

This bundle keeps only the final, strict evidence set used to establish engineering equivalence between MATLAB `TuTraSt` and C++ `sTuTraSt`.

Included artifacts
- `FULL_REPLACEMENT_ALIGNMENT_20260222.md`: final alignment report (basis/processes/BT/TS/Evol match on synthetic + real cases).
- `realcase_quick_cutoff45_20260221_033440/`: quick real-case MATLAB/C++ reference workspace (contains MATLAB reference outputs).
- `realcase_quick_cutoff45_fullalign2_20260222_020623/`: final C++ quick real-case rerun with full alignment.
- `repair_long_20260220_115603/`: long repair run containing MATLAB OvUUEPvZ baseline reference outputs.
- `recheck_OvUUEPvZ_fullalign2_20260222_020623/`: final C++ OvUUEPvZ rerun aligned to MATLAB baseline.
- `recheck_synth_fullalign2_20260222_020650/`: final synthetic regression rerun (`test1_channel`, `test2_3dpores`).

Notes
- Intermediate debugging/recheck artifacts were moved to `archive/20260222_cleanup/verification_pre_final_intermediates/`.
- Paths in the report reflect original run names; this bundle preserves those original directory names under one folder for traceability.
