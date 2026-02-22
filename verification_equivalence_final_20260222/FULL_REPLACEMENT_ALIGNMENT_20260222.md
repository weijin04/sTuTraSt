# Full Replacement Alignment (2026-02-22)

## Goal
Push C++ (`sTuTraSt`) to engineering-level replacement of MATLAB (`TuTraSt`) on deterministic outputs.

## Final code changes in this round

1. `src/cluster.cpp`
- `grow_clusters()` now checks host `TS_matrix` inside neighbor loop (`src/cluster.cpp:220`).
  - Prevents post-marking over-processing that previously produced one extra TS in a real case.
- Merge-group removal semantics changed to MATLAB-compatible swap-with-last behavior instead of stable erase:
  - `merge_clusters()` (`src/cluster.cpp:523`)
  - `merge_only_merge_groups()` (`src/cluster.cpp:643`)

2. `src/tunnel.cpp`, `include/tunnel.h`
- Added MATLAB-style path cross-vector engine:
  - `calculate_cross_vector_path(...)` (`src/tunnel.cpp:286`)
  - declarations in `include/tunnel.h:34-40`
- Process cross-vector generation now follows MATLAB process-stage logic:
  - only compute when either cluster is boundary (`src/tunnel.cpp:132-133`)
  - path sets built from non-TS cluster points + TS group points (`src/tunnel.cpp:138-157`)
- Boundary detection aligned to MATLAB `finC.info` behavior (exclude TS-marked points):
  - `is_cluster_boundary(...)` (`src/tunnel.cpp:232-252`)

## Validation summary

### A) Real quick benchmark
- MATLAB ref: `realcase_quick_cutoff45_20260221_033440/matlab`
- C++ new: `realcase_quick_cutoff45_fullalign2_20260222_020623`
- Result:
  - `basis.dat`: MATCH
  - `processes_1000.dat`: MATCH
  - `BT.dat`: MATCH
  - `TS_data.out`: MATCH
  - `Evol_1000.dat`: MATCH

### B) Real repair baseline (OvUUEPvZ)
- MATLAB ref: `repair_long_20260220_115603/repair/OvUUEPvZ/matlab`
- C++ new: `recheck_OvUUEPvZ_fullalign2_20260222_020623`
- Result:
  - `basis.dat`: MATCH
  - `processes_1000.dat`: MATCH
  - `BT.dat`: MATCH
  - `TS_data.out`: MATCH
  - `Evol_1000.dat`: MATCH
- Runtime/resource:
  - `elapsed_s=1.58`, `maxrss_kb=337684`
  - swap unchanged before/after (`SwapTotal == SwapFree`)

### C) Synthetic regression
- C++ recheck run: `recheck_synth_fullalign2_20260222_020650`
- Against MATLAB baselines (`test1_channel`, `test2_3dpores`):
  - `basis/processes/BT/TS/Evol`: all MATCH

## Remaining note
- `tunnel_info.out` remains a format-level difference (MATLAB verbose narrative text vs C++ structured summary).
- Core deterministic engineering outputs used downstream are now aligned.
