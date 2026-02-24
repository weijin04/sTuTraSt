# Handoff: sTuTraSt/TuTraSt Equivalence, Production Pilots, and 298K Continuation

Date: 2026-02-22 (local)

This document is intended to let any engineer continue the task from the current state with minimal rediscovery.

## 1. Goal (what the user actually wants)

Primary target:
- Use `sTuTraSt/` (C++ accelerated implementation) as a production replacement for `TuTraSt/` (MATLAB/Octave reference), **provided engineering equivalence is established**.

Current production goal (latest direction):
- Compute diffusion-relevant outputs for `Xe` and `Kr` at `298 K` (user also mentioned atmospheric pressure context; current workflow is single-particle diffusion and does not explicitly take pressure as an input parameter in `input.param`).

Operational constraints:
- Avoid swap pressure and especially prolonged swap use (risk of weird bugs / Linux instability).
- Parallelism is allowed, but memory and swap protection are more important than peak concurrency.

## 2. What has already been completed (important)

### 2.1 C++ vs MATLAB engineering equivalence (core outputs)

This has been completed and documented. The final strict evidence bundle is:
- `sTuTraSt/verification_equivalence_final_20260222/`

Key report:
- `sTuTraSt/verification_equivalence_final_20260222/FULL_REPLACEMENT_ALIGNMENT_20260222.md`

The final report shows alignment on:
- `basis.dat`
- `processes_1000.dat`
- `BT.dat`
- `TS_data.out`
- `Evol_1000.dat`

for:
- synthetic tests (`test1_channel`, `test2_3dpores`)
- quick real case
- real baseline (`OvUUEPvZ`)

### 2.2 Key C++ fixes already implemented

Core logic alignment fixes were made in the C++ code (already committed once; see commit below):
- `sTuTraSt/src/cluster.cpp` (host TS behavior + MATLAB-consistent merge semantics)
- `sTuTraSt/src/tunnel.cpp` and `sTuTraSt/include/tunnel.h` (cross vector/path logic and boundary handling)

### 2.3 Production pilot runs (Xe, 1000K) completed and archived

Five aligned pilot production batches were run successfully (12 cases each), all `OK`, with `swap=0`.

Archived and standardized here:
- `sTuTraSt/archive/20260222_cleanup/production_pilot_xe_temp1000K_grid0p2_cut12p8_estep1p5_ecut20/`

Batch manifest:
- `sTuTraSt/archive/20260222_cleanup/production_pilot_manifest_20260222.csv`

These are **pilot/stability/alignment production runs**, not the final `298 K` production target.

### 2.4 Archive cleanup completed

Intermediate verification/recheck artifacts were moved out of the top level.

Archive manifest:
- `sTuTraSt/archive/20260222_cleanup/ARCHIVE_MANIFEST_20260222.tsv`

Top level was reduced so only the final rigorous verification bundle remains:
- `sTuTraSt/verification_equivalence_final_20260222`

## 3. Git state (important for continuity)

Committed (equivalence fixes + final verification + archive manifests):
- Commit: `6dae838`
- Message: `Align C++ with MATLAB and archive final verification`

Current local modification not yet committed:
- `sTuTraSt/launch_partial_prod_and_repair.sh`

Reason for current local modification:
- Fix 298K (and general non-1000K) file name handling in summary statistics and repair comparison.

## 4. Current active work (where we are now)

We have switched to a `Xe @ 298 K` small-batch validation run to test physically relevant settings before broad production.

### 4.1 298K validation batch (C++ production side)

Run directory:
- `sTuTraSt/production_partial_optimal_Xe_20260222_195945`

Configured parameters (from `config.env`):
- `PROBE=Xe`
- `SIM_TEMP=298`
- `GRID_SPACING=0.2`
- `GRID_CUTOFF=12.8`
- `INPUT_ENERGY_STEP=0.5`
- `INPUT_ENERGY_CUTOFF=25`
- `MAX_JOBS=2`
- `GRID_NJOBS=4`
- `REPAIR_MAX_VOXELS=3000000`
- `REPAIR_SAMPLE_LIMIT=2`
- `MAT_TIMEOUT=2400`

Selected cases:
- `OvUUEPvZ`
- `QupxOnO1`

C++ run status:
- Completed `2/2` with `grid_status=OK`, `cpp_status=OK`
- `summary.csv` is present: `sTuTraSt/production_partial_optimal_Xe_20260222_195945/summary.csv`

Important caveat:
- In this batch `summary.csv` reports `proc_lines=0` due a script bug (fixed locally now).
- Actual C++ outputs are nonzero and valid:
  - `QupxOnO1`: `processes_298.dat` has `630` lines
  - `OvUUEPvZ`: `processes_298.dat` has `302` lines

### 4.2 298K repair re-run (MATLAB/C++ comparison, corrected script)

The first repair run inside the production batch produced false `DIFF`s because:
1. MATLAB was terminated before writing all outputs (tool session ended; `mat_exit=124`)
2. the script incorrectly compared `processes_1000.dat` / `Evol_1000.dat` even when `SIM_TEMP=298`

To fix this, a clean repair-only rerun was launched with the corrected script.

Repair rerun directory:
- `sTuTraSt/repair_rerun_298K_batch195945_20260222_2008`

Current status snapshot (time of writing):
- `repair_summary.csv` still header-only (first case still running)
- Currently running case: `OvUUEPvZ`
- MATLAB log path:
  - `sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair/OvUUEPvZ/matlab/stdout.log`
- Latest observed progress in log:
  - `level:9(4.5 kJ/mol)` after `Time level 8: 2.7708`, total elapsed `12.8239 s`
- `swap=0`, `MemAvailable ~60 GB`

## 5. New 298K script bug fix (local, not yet committed)

File:
- `sTuTraSt/launch_partial_prod_and_repair.sh`

### 5.1 Fixed `proc_lines` counting in `summary.csv`

Before:
- counted only `processes_1000.dat`

Now:
- counts `processes_${SIM_TEMP}.dat`, with fallback to `processes_1000.dat`

Relevant lines:
- `sTuTraSt/launch_partial_prod_and_repair.sh:240`

### 5.2 Fixed repair comparison file names

Before:
- compared `processes_1000.dat` and `Evol_1000.dat` unconditionally

Now:
- compares `processes_${SIM_TEMP}.dat` and `Evol_${SIM_TEMP}.dat`

Relevant lines:
- `sTuTraSt/launch_partial_prod_and_repair.sh:289`
- `sTuTraSt/launch_partial_prod_and_repair.sh:294`
- `sTuTraSt/launch_partial_prod_and_repair.sh:297`

## 6. Why the earlier 298K repair showed `mat_exit=124` and `DIFF`

Bad run (keep for audit, but do not trust for final conclusion):
- `sTuTraSt/production_partial_optimal_Xe_20260222_195945/repair_summary.csv`

Observed symptoms:
- `mat_exit=124`
- `basis/processes/BT/TS` reported `DIFF`
- comparator output shows many MATLAB files were `MISSING`

Root cause:
- MATLAB/Octave process was terminated before completion (tool PTY session lifecycle issue)
- comparison script also used wrong temperature-specific filenames

Important:
- MATLAB logs from that failed run still prove the cases were physically active and had breakthrough behavior.
- They do **not** prove final equality/inequality because the outputs were incomplete.

## 7. Expected remaining time (practical estimate)

### 7.1 Current repair rerun (2 cases, 298K, step 0.5, cutoff 25)

This is slower than prior repair checks because:
- `energy_step=0.5` (vs `1.5` or `2.0`) increases the number of energy levels substantially
- `energy_cutoff=25` implies up to ~50 levels (0.5, 1.0, ..., 25.0 kJ/mol)

Empirical observations from this work:
- At 298K/0.5 on these real cases, runtime grows nonlinearly after merges and breakthrough onset.
- Previous interrupted attempts reached:
  - `OvUUEPvZ` around level 18 at ~40 s
  - `QupxOnO1` around level 24 at ~38.5 s

Reasonable estimate from the current state:
- Remaining time for the **current `OvUUEPvZ` rerun**: roughly `5-20 min`
- Then **`QupxOnO1` rerun**: roughly `5-20 min`
- Total to finish both comparisons (if uninterrupted): roughly `10-40 min`

This is intentionally a wide range because per-level merge cost can jump once cluster topology changes.

## 8. How to continue from exactly here

### 8.1 Monitor the current repair rerun

Progress summary:
```bash
tail -f sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair_summary.csv
```

MATLAB log tails:
```bash
find sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair -maxdepth 3 -name stdout.log -print | sort
tail -f sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair/OvUUEPvZ/matlab/stdout.log
```

Resource check:
```bash
grep -E 'MemAvailable|SwapTotal|SwapFree' /proc/meminfo
```

### 8.2 When the rerun completes, verify equality results

Expected target:
- `mat_exit=0`
- `all_ok=1` for both cases (or only `tunnel=SKIP`/format differences)

Check:
```bash
cat sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair_summary.csv
```

If anything is `DIFF`, inspect directly:
```bash
python3 sTuTraSt/compare_outputs.py \
  sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/repair/OvUUEPvZ/matlab \
  sTuTraSt/production_partial_optimal_Xe_20260222_195945/cases/OvUUEPvZ/cpp \
  processes_298.dat
```

Repeat for:
- `basis.dat`
- `BT.dat`
- `TS_data.out`
- `Evol_298.dat`

### 8.3 Recommended next experiment (if equality holds)

Because user’s real target is `Xe/Kr @ 298K` diffusion coefficients:

Next small batch (recommended):
- `SIM_TEMP=298`
- `INPUT_ENERGY_STEP=0.5`
- `INPUT_ENERGY_CUTOFF=30` (more conservative than 25 for diffusion windows)

Suggested command (Xe):
```bash
CASE_LIMIT=2 CASE_OFFSET=0 PROBE=Xe SIM_TEMP=298 \
GRID_SPACING=0.2 GRID_CUTOFF=12.8 \
INPUT_ENERGY_STEP=0.5 INPUT_ENERGY_CUTOFF=30 \
MAX_JOBS=2 GRID_NJOBS=4 \
REPAIR_MAX_VOXELS=3000000 REPAIR_SAMPLE_LIMIT=2 \
SWAP_THRESHOLD_KB=65536 \
bash sTuTraSt/launch_partial_prod_and_repair.sh launch
```

Then repeat for `Kr` by changing only:
- `PROBE=Kr`

## 9. Operational caveats (important)

### 9.1 PTY/session lifecycle can kill child jobs

Observed earlier:
- A MATLAB repair process got terminated around ~40 s despite `MAT_TIMEOUT=2400`.
- Root cause was not the script’s timeout value; it was the lifecycle of the interactive execution session used to launch/observe the process.

Practical workaround:
- Keep a persistent session open and poll it (what is being done now), or
- Use a detached process manager (`nohup`, `tmux`, `screen`, systemd-run`) if running manually.

### 9.2 Swap safety

User specifically requested avoiding swap churn.

Current proven-safe settings:
- `MAX_JOBS=2..3`
- `GRID_NJOBS=4`
- `SWAP_THRESHOLD_KB=65536`

Observed in all recent runs:
- `swap=0` throughout

## 10. Quick file map (most relevant paths)

Final equivalence proof bundle:
- `sTuTraSt/verification_equivalence_final_20260222/`

Archive manifests:
- `sTuTraSt/archive/20260222_cleanup/ARCHIVE_MANIFEST_20260222.tsv`
- `sTuTraSt/archive/20260222_cleanup/production_pilot_manifest_20260222.csv`

Current 298K production validation batch (C++ outputs):
- `sTuTraSt/production_partial_optimal_Xe_20260222_195945/`

Current 298K repair rerun (MATLAB/C++ compare, corrected script):
- `sTuTraSt/repair_rerun_298K_batch195945_20260222_2008/`

Current script requiring commit after verification:
- `sTuTraSt/launch_partial_prod_and_repair.sh`

Last committed baseline:
- `git -C sTuTraSt rev-parse --short HEAD` -> `6dae838`

## 10.1 Incremental update (latest debugging on `QupxOnO1`, 298K / step=0.5)

This section is appended in-place (no new handoff file), per user request.

### Confirmed status

- `298K / step=1.0 / cutoff=25` (`OvUUEPvZ`) still passes MATLAB/C++ repair (`MATCH` for basis/processes/BT/TS/Evol).
- `298K / step=0.5 / cutoff=25` (`QupxOnO1`) still has a **real** mismatch (not timeout artifact):
  - MATLAB: `basis=75`, `processes_298=652`, `TS_data=34530`
  - C++: `basis=73`, `processes_298=630`, `TS_data=34177`
  - `BT` and `Evol_298.dat` match.

### Important new findings (high value)

1. `restart2.mat` is ASCII (Octave text), not binary
- Path:
  - `sTuTraSt/repair_rerun_298K_qupx_timeout21600_20260222/repair/QupxOnO1/matlab/restart2.mat`
- This enables offline extraction of MATLAB intermediate data without re-running long MATLAB jobs.

2. Raw MATLAB `TS_list_all` extracted successfully and compared
- Extracted from `restart2.mat`: `TS_list_all` (`34530 x 10`)
- C++ raw pre-compact TS dump (added debug output) has `34177` rows.
- Difference is already present in **pre-compact raw TS list**, so the mismatch is **not** caused only by TS grouping/organize/output stages.

3. Earliest raw TS coordinate-level mismatch appears around `level 21` (10.5 kJ/mol)
- Raw TS comparison (`coord+level`) shows first diff at level `21`.
- This matches the region where the case first breaks through and complexity spikes.

4. `energy_step` sensitivity is extreme (diagnostic)
- On the same grid/case, changing only `energy_step` by `1e-9` changes outputs:
  - `0.500000001` -> same as current C++ (`73/630/34177`)
  - `0.499999999` -> different (`76/634/34348`)
- Strong evidence that a threshold/timing-sensitive branch (`dE < energy_step` and/or event ordering around it) is involved.

### Root-cause fixes found and applied during debugging

1. MATLAB `cube2xsfdat.m` behavior missing in C++ parser (important)
- MATLAB shifts cube energies before anything else:
  - `shift_cube_data=(cube_data-min(cube_data))*conv2kJmol`
- C++ previously did unit conversion but **did not** apply the global min shift in `CubeParser`.
- Fixed in:
  - `sTuTraSt/src/cube_parser.cpp`

2. MATLAB max-value replacement semantics in grid preprocessing (important)
- MATLAB replaces `pot_data == max(pot_data)` before `isinf`, so when max is `Inf`, only `Inf` entries are clipped.
- C++ behavior was not identical in effect after parser differences.
- `Grid::initialize()` was updated toward MATLAB order/semantics in:
  - `sTuTraSt/src/grid.cpp`

3. Validation of parser/grid alignment fix (confirmed)
- `level_matrix` head values now match MATLAB exactly for the known divergent region:
  - e.g. `... 1630, 501, 501, 501, 501, 501, ...` (previously C++ had `554` here)
- This proves the parser/grid preprocessing mismatch was real and fixed.

### What changed vs what did not (after parser/grid fix)

- `level_matrix` alignment improved (confirmed).
- However, `QupxOnO1 @ 298K / step=0.5` final mismatch **still remains** at current state:
  - `basis=73`, `processes=630`, `TS_data=34177` (C++)
- So there is at least one additional mismatch source in cluster/TS search event ordering or merge/TS timing.

### Debug tooling added (local, uncommitted unless explicitly committed later)

1. Octave ASCII matrix extractor (reusable)
- `sTuTraSt/extract_octave_ascii_matrix.py`
- Purpose: extract matrices like `TS_list_all` from `restart*.mat` text dumps.

2. C++ debug dumps (env-gated, no effect unless enabled)
- `TUTRAST_DEBUG_DUMP_TS_LIST=1`
  - writes `debug_ts_list_all_precompact.dat`
  - writes `debug_ts_list_all_postcompact.dat`
- `TUTRAST_DEBUG_DUMP_MINIDC=1`
  - writes `debug_minID_C_precompact_flat.txt`
  - writes `debug_level_matrix_flat.txt`

3. Local trace switch for the `QupxOnO1` level-21 mismatch region (diagnostic)
- `TUTRAST_TRACE_QUPX21=1`
- Logs selected `merge` / `diff-cluster-ts` decisions near the observed mismatch boxes.

### Notes on attempted hypotheses (and results)

- Rewriting TS organize/grouping to closer MATLAB style (`transition_state.cpp`): **no change** to `QupxOnO1` mismatch (experiment reverted).
- Recomputing cluster minima on every `dE` check (instead of cached `cluster.min_energy`): **no change** (experiment reverted).
- Fixing `new_points` TS-flag marking in same-cluster PBC path (`cluster.cpp`): logical correctness improvement retained, but **did not** resolve this case.

### Current best next step (recommended)

Use the new offline MATLAB dump + C++ raw dumps to compare **cluster state / raw TS events around levels 20-23** and find the first concrete branch divergence in `grow_clusters()`:
- likely class: event ordering around cluster merge + TS marking near breakthrough onset
- not just postprocessing / output formatting

## 11. Minimal handoff checklist (for the next person)

1. Let `sTuTraSt/repair_rerun_298K_batch195945_20260222_2008` finish.
2. Confirm `repair_summary.csv` shows valid `mat_exit=0` and compare results (not `MISSING`).
3. If OK, commit the 298K filename fix in `launch_partial_prod_and_repair.sh`.
4. Launch next small-batch test with `INPUT_ENERGY_CUTOFF=30` at `298K`.
5. If `Xe` is satisfactory, repeat for `Kr`.
6. Start batched production with the same swap-safe scheduler settings.

## 12. Incremental update (2026-02-23): `QupxOnO1 @ 298K / step=0.5` repaired to full MATCH

### Summary of what was fixed this round

The remaining real mismatch on:
- `Xe`, `QupxOnO1`, `298 K`, `energy_step=0.5`, `energy_cutoff=25`

is now resolved to full semantic match (except `tunnel_info.out` format, which is intentionally skipped).

Validated compare result (MATLAB vs C++):
- `basis.dat`: `MATCH`
- `processes_298.dat`: `MATCH`
- `BT.dat`: `MATCH`
- `TS_data.out`: `MATCH`
- `Evol_298.dat`: `MATCH`

### Root cause 1 (major): `grow_clusters()` did not match MATLAB `filled` semantics

Observed via target-point trace:
- Earliest occupancy mismatch voxel `(16,41,1)` (MATLAB 1-based) was assigned in C++ by:
  - `grow-add`, `level=16`, cluster `5`
  - **`grow-pass=5`**
- MATLAB `TuTraSt_main.m` uses a per-cluster `filled` array inside the level growth loop:
  - clusters that fail to grow in a pass are not revisited later that level
- C++ previously used a global `while(any_growth)` loop and revisited all clusters until no growth anywhere
  - this allowed extra late-pass growth and caused upstream `minID` / TS / process divergence

Fix applied:
- `sTuTraSt/src/cluster.cpp`
  - `grow_clusters()` rewritten to MATLAB-style per-cluster `filled` loop semantics

Immediate effect on `QupxOnO1 @ 298K/0.5`:
- counts aligned from
  - C++ old: `basis=73`, `processes=630`, `TS_data=34177`
- to
  - C++ new: `basis=75`, `processes=652`, `TS_data=34530`
  - matching MATLAB counts exactly

### Root cause 2 (residual process-vector mismatch): wrong no-path fallback semantics

After the `filled` fix, only `processes_298.dat` still differed in some `cross/ts_cross` vectors.

Key diagnosis:
- C++ path finder (`calculate_cross_vector_path`) hit non-self path fallback **16 times** on this case
- Fallbacks included the exact mismatched basis pairs (e.g. `5->45`, `21->29`, `40->49`, `43->44`)
- MATLAB `get_TS_cross_vector.m` behavior when no path is found:
  - returns initialized `[0 0 0]`
  - does **not** use a geometric/simple cross fallback
- C++ previously returned `calculate_cross_vector_simple(start,end)` in this case
  - this produced incorrect nonzero `process_cross` on a subset of processes

Fix applied:
- `sTuTraSt/src/tunnel.cpp`
  - `calculate_cross_vector_path(..., self_tunnel=false)` now returns zero vector on no-path (MATLAB semantics)

Result:
- `processes_298.dat` now `MATCH`

### Additional correctness alignment (kept)

These changes were also applied during the same debugging cycle:

1. Export `minID_L` debug dump (diagnostic only)
- `sTuTraSt/src/main.cpp`
- `TUTRAST_DEBUG_DUMP_MINIDC=1` now also writes:
  - `debug_minID_L_precompact_flat.txt`

2. Cluster minimum coordinate selection excludes TS points (MATLAB `finC.min` semantics)
- `sTuTraSt/src/tunnel.cpp` (`get_cluster_min_coord`)
- `sTuTraSt/src/output_writer.cpp` (`write_basis`)
- behavior:
  - prefer min-energy point with `ts_flag==0`
  - fallback to all points only if cluster has no non-TS points

Note:
- This min-point TS filtering is a correctness alignment but was not the decisive fix for the residual process mismatch in this case.

### Regression checks after the fixes (all passed)

1. `QupxOnO1 @ 298K / step=0.5 / cutoff=25` (the previously failing case)
- MATLAB dir:
  - `sTuTraSt/repair_rerun_298K_qupx_timeout21600_20260222/repair/QupxOnO1/matlab`
- C++ dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260222_195945/cases/QupxOnO1/cpp`
- compare:
  - `basis/processes_298/BT/TS/Evol = MATCH`

2. `OvUUEPvZ @ 298K / step=1.0` (previously passing 298K validation)
- MATLAB dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260223_030925/repair/OvUUEPvZ/matlab`
- C++ dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260223_030925/cases/OvUUEPvZ/cpp`
- compare:
  - `basis/processes_298/BT/TS/Evol = MATCH`

3. `OvUUEPvZ @ 1000K` real baseline (previously full-aligned)
- MATLAB dir:
  - `sTuTraSt/verification_equivalence_final_20260222/repair_long_20260220_115603/repair/OvUUEPvZ/matlab`
- C++ dir:
  - `sTuTraSt/verification_equivalence_final_20260222/recheck_OvUUEPvZ_fullalign2_20260222_020623`
- compare:
  - `basis/processes_1000/BT/TS/Evol = MATCH`

### Debug env switches used/added this round (env-gated)

- `TUTRAST_TRACE_PROCESS_FALLBACK=1`
  - logs non-self process/TS path BFS failures in `calculate_cross_vector_path`

Temporary local `TUTRAST_TRACE_MINID_TARGET` instrumentation was used during diagnosis of the earliest
`minID` divergence and then removed before cleanup/commit.

These debug switches are diagnostic only and inactive unless explicitly enabled.

## 13. Incremental update (2026-02-24): `Xe@298K step=0.5 cutoff=30` production resumed; new repair DIFFs found

### Production progress after `QupxOnO1` fix

`Xe @ 298 K`, `energy_step=0.5`, `energy_cutoff=30`, `grid_spacing=0.2` production was resumed.

Completed batches:

1. `offset=0`
- Run dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260223_124818`
- Production:
  - `12/12` cases `grid_status=OK`, `cpp_status=OK`
- Repair sampling:
  - `31ukFeYP`: `MATCH`
  - `2hwaT5Ut`: `MATCH`
  - `repair_summary.csv` all sampled cases `all_ok=1`
- Swap:
  - `0` throughout

2. `offset=12`
- Run dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260224_012113`
- Production:
  - `12/12` cases `grid_status=OK`, `cpp_status=OK`
- Repair sampling (2 cases run before stop/check):
  - `EMGLKnk7`: `DIFF` (`basis/processes/TS`), `BT/Evol MATCH`
  - `5I1F0W9u`: `DIFF` (`basis/processes/TS`), `BT/Evol MATCH`
- Swap:
  - `0` throughout

### New observed mismatch pattern (post-`QupxOnO1` fix)

For both sampled DIFF cases in `offset=12`:
- `BT.dat`: `MATCH`
- `Evol_298.dat`: `MATCH`
- `basis.dat`: `DIFF` (line count mismatch)
- `processes_298.dat`: `DIFF` (line count mismatch)
- `TS_data.out`: `DIFF` (line count mismatch)

Examples:

1. `EMGLKnk7`
- MATLAB vs C++:
  - `basis.dat`: `282` vs `296`
  - `processes_298.dat`: `858` vs `874`
  - `TS_data.out`: `3712` vs `3794`

2. `5I1F0W9u`
- MATLAB vs C++:
  - `basis.dat`: `164` vs `188`
  - `processes_298.dat`: `406` vs `432`
  - `TS_data.out`: `2607` vs `2743`

This is the same high-level symptom class as the earlier `QupxOnO1` issue:
- final breakthrough and volume evolution agree
- intermediate topology / TS grouping / process network is over-generated in C++

### Immediate implication

Do **not** assume the `QupxOnO1` fixes fully solved all `298K step=0.5` cases.
They fixed at least one important class (`filled` + process path fallback), but more branch-order/cluster-growth edge cases likely remain.

### Recommended next step (current priority)

Use `EMGLKnk7` as the new small reproducer for differential debugging because:
- it is already generated in production + repair outputs
- mismatch is moderate size (faster than the largest cases)
- same symptom signature as previous case, likely exposing another nearby semantics gap

## 14. Incremental update (2026-02-24): `EMGLKnk7` / `5I1F0W9u` mismatch repaired

### Key diagnosis (different from `QupxOnO1`)

For `EMGLKnk7`:
- `minID_matrix` (`L/C`) matched MATLAB exactly
- raw pre-organize `TS_list_all` (`coord+level`) matched MATLAB exactly (`3794` rows)

So the divergence was **not** in cluster growth / raw TS generation.
It was in the later tunnel/process reconstruction path.

### Root cause

C++ was under-populating `tunnel_cluster` compared with MATLAB:
- MATLAB `check_neighbors.m` appends to `tunnel_cluster` not only in same-cluster PBC cases,
  but also when different clusters are already in the same merge group and a nonzero cross-vector is found.
- C++ was adding those cross-vectors to `tunnel_list` but **not** adding the current cluster to `tunnel_cluster`.

This caused downstream tunnel/process reconstruction mismatches on some 298K/0.5 cases:
- extra or missing tunnel eligibility for TS groups
- `basis/processes/TS_data` mismatches while `BT/Evol` still matched

### Fixes applied

1. `tunnel_cluster` synchronization during cluster merges
- `sTuTraSt/src/cluster.cpp`
- `merge_clusters()` now remaps `tunnel_cluster` entries `cluster2_id -> cluster1_id`

2. Missing MATLAB-equivalent `tunnel_cluster` updates for same-merge-group cross events
- `sTuTraSt/src/cluster.cpp`
- when `same_merge_group` and `idiff/jdiff/kdiff != 0`, C++ now updates:
  - `tunnel_list`
  - `tunnel_cluster`
  - `tunnel_cluster_dim`
  matching MATLAB behavior

3. MATLAB-style tunnel/process/TS output filtering (kept)
- `sTuTraSt/src/tunnel.cpp`
  - tunnel creation restricted to merge groups intersecting `tunnel_cluster`
  - TS group tunnel attachment uses `cluster1` membership semantics
- `sTuTraSt/src/output_writer.cpp`
  - `TS_data.out` writes only TS groups attached to tunnels (MATLAB `TS_tunnel_list` behavior)

### Validation after fix

New previously failing sampled cases now match:

1. `EMGLKnk7` (`Xe@298K`, `step=0.5`, `cutoff=30`)
- MATLAB dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260224_012113/repair/EMGLKnk7/matlab`
- C++ recheck dir:
  - `sTuTraSt/tmp_recheck_EMGL_after_tunnelclusterfix`
- Result:
  - `basis/processes_298/BT/TS/Evol = MATCH`

2. `5I1F0W9u` (`Xe@298K`, `step=0.5`, `cutoff=30`)
- MATLAB dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260224_012113/repair/5I1F0W9u/matlab`
- C++ recheck dir:
  - `sTuTraSt/tmp_recheck_5I1_after_tunnelclusterfix`
- Result:
  - `basis/processes_298/BT/TS/Evol = MATCH`

### Regression checks (post-fix)

All still pass:

- `QupxOnO1 @ 298K / step=0.5`
  - `sTuTraSt/tmp_recheck_Qupx_after_tunnelclusterfix`
  - `MATCH`
- `OvUUEPvZ @ 298K / step=1.0`
  - `sTuTraSt/tmp_recheck_OvUUE_298_step1_after_tunnelclusterfix`
  - `MATCH`
- `OvUUEPvZ @ 1000K`
  - `sTuTraSt/tmp_recheck_OvUUE_1000_after_tunnelclusterfix`
  - `MATCH`

### Note on batch metadata

`sTuTraSt/production_partial_optimal_Xe_20260224_012113/repair_summary.csv` still records the earlier DIFF results
because the batch C++ outputs were generated before this fix.
Use the recheck directories above as the corrected audit evidence for those sampled cases.
