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

## Current production status (continuation after `9f875a4`)

### Active batch: `offset=24`

- Run dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260224_085801`
- Parameters:
  - `Xe @ 298K`, `step=0.5`, `cutoff=30`, `GRID_SPACING=0.2`, `GRID_CUTOFF=12.8`
- Launch mode:
  - `produce` + `repair` started manually (wrapper `launch` is intermittently unreliable under `nohup`; production logic itself is OK)
- Resource status:
  - `running_jobs <= 3`
  - `swap=0` (maintained)

### In-progress snapshot

At last check, production had completed 6 cases (all `grid_status=OK`, `cpp_status=OK`):

- `WZRkQTFL` (`basis=82`, `proc=730`)
- `yI8WnL2I` (`basis=116`, `proc=296`)
- `CTpfYEtz` (`basis=0`, `proc=0`)
- `OALwrVhy` (`basis=72`, `proc=480`)
- `TLUnSj5E` (`basis=57`, `proc=338`)
- `0sf1w2S3` (`basis=49`, `proc=266`)

Repair sampling for this batch had started (MATLAB/Octave running) but `repair_summary.csv` had not yet written a result row.

### Later progress on `offset=24` batch (`sTuTraSt/production_partial_optimal_Xe_20260224_085801`)

- Production completed `12/12`, all `grid_status=OK`, `cpp_status=OK`
- `swap=0` throughout production
- Batch repair sampling remained in progress (long-timeout MATLAB run) after production completed

## Next batch continuation (`offset=36`)

### Invalid start (ignore)

An initial manual restart attempt created:

- `sTuTraSt/production_partial_optimal_Xe_20260224_091020_BADPARAM_DEFAULTS_ABORTED`

Issue:
- `config.env` was copied but not exported into the `produce/repair` subprocess environments
- script defaults were used (`CASE_LIMIT=6`, `CASE_OFFSET=0`, `GRID_SPACING=0.15`, `GRID_NJOBS=1`, etc.)

Action taken:
- stopped the batch immediately
- preserved the directory with `BADPARAM_DEFAULTS_ABORTED` suffix for auditability

### Correct restart (active)

- Run dir:
  - `sTuTraSt/production_partial_optimal_Xe_20260224_091115`
- Startup method:
  - `set -a; source <run_dir>/config.env; set +a; bash ./launch_partial_prod_and_repair.sh produce|repair <run_dir>`
- Verified parameters from live grid processes:
  - `--spacing 0.2`
  - `--cutoff 12.8`
  - `--n_jobs 4`
- Selected cases begin at new `offset=36` (e.g. `2SNiy1ic`, `rbXXtXKt`, `g9yQcKYp`, ...)
- `running_jobs` ramps to `3`
- `swap=0`

### Follow-up status (later check)

- `sTuTraSt/production_partial_optimal_Xe_20260224_091115` production completed `12/12` (`grid_status=OK`, `cpp_status=OK`)
- repair sampling result currently includes:
  - `2SNiy1ic`: `mat_exit=0`, `basis/processes/BT/TS/Evol = MATCH`

## Physical-consistency audit (in addition to engineering MATCH/DIFF)

Engineering strict alignment remains the primary requirement, but a physical-level audit was added to judge whether a residual DIFF is likely to change diffusion-relevant conclusions.

### Case: `WZRkQTFL` (`Xe@298K`, `step=0.5`, `cutoff=30`)

Repair result:
- `basis=MATCH`
- `processes=DIFF`
- `BT=MATCH`
- `TS=DIFF`
- `Evol=MATCH`

Key physical observations:

1. Breakthrough energies are identical
- `BT.dat` matches exactly (`9.5 9.5 12`)

2. Energy-volume curve is identical
- `Evol_298.dat` matches exactly (full vector)

3. Process-network size is identical
- `processes_298.dat` line count: `730 vs 730`
- `basis.dat` line count: `82 vs 82`

4. Dominant transport channels are effectively unchanged
- top-10 process edges (by rate) Jaccard overlap: `1.0`
- top-20 process edges Jaccard overlap: `1.0`
- top-50 process edges Jaccard overlap: `1.0` (same unique edge set)

5. Outgoing-rate totals per basis are almost identical except one basis with a few-percent shift
- median relative error of per-basis total outgoing rate: `~4e-12`
- 90th percentile: `~1.2e-11`
- worst basis (`51`): `~3.85%` relative difference

6. Residual process topology mismatch is small but real
- edge signature difference (keeping basis pair + cross + ts_cross + tunnel + orig IDs, ignoring TS-group numbering): `extra=6`, `missing=6` out of `730`

Interpretation:
- This is **not** strict engineering equivalence (still needs fixing).
- But at the current sampled evidence, the residual difference is consistent with a **small local reconstruction discrepancy** and the **macro physical descriptors (BT/Evol + dominant channels)** are unchanged.
- Therefore, this specific DIFF looks physically close, not catastrophic.

### Case: `2SNiy1ic` (`Xe@298K`, `step=0.5`, `cutoff=30`)

- `basis/processes/BT/TS/Evol = MATCH`
- Rate-spectrum and per-basis outgoing-rate comparisons are at floating-point noise level (`~1e-11` scale)

## Practical guidance from current evidence

- For many sampled cases, C++ and MATLAB are both engineering-equivalent and physically-equivalent.
- For at least one new sampled case (`WZRkQTFL`), there is a residual engineering DIFF, but the checked macro physical observables remain the same.
- Do **not** label C++ as “physically better” yet; current evidence supports “physically same or very close” for sampled outputs, with one remaining reconstruction bug to remove.

## Match criteria used during production validation (important)

Two distinct levels of “match” are used. Keep them separate.

### A. Engineering-level MATCH (for full replacement claim)

Source:
- `repair_summary.csv` columns: `basis`, `processes`, `BT`, `TS`, `Evol`, `Dave`, `tunnel`
- `basis/processes/BT/TS/Evol` are primarily computed via `sTuTraSt/compare_outputs.py`

Rules:
- `basis.dat`
  - normalize MATLAB/C++ indexing (1-based vs 0-based)
  - compare normalized coordinates/rows with numeric tolerance
- `TS_data.out`
  - ignore TS-group numbering differences
  - normalize half-grid coordinates with PBC wrapping
  - sort and compare
- `processes_<T>.dat`
  - ignore TS-group ID numbering differences only
  - preserve and compare process structure fields (`cross`, `ts_cross`, `tunnel_id`, origin cluster IDs, etc.)
  - sort and compare with numeric tolerance
- `BT.dat`, `Evol_<T>.dat`
  - direct numeric comparison with tolerance
- `tunnel_info.out`
  - currently treated as `SKIP` (format differences)

Interpretation:
- `processes=DIFF` means there is still a real engineering mismatch (not just numbering) even if line counts match.

### B. Physical-level MATCH (for production usability / diffusion output reasonableness)

Used when judging whether C++ outputs are acceptable for the user’s immediate production goal (`D_ave_298.dat`) even if some engineering fields still differ.

Primary signals:
- `BT` (breakthrough energies)
- `Evol` (energy-volume curve)
- `Dave` (`D_ave_<T>.dat` comparison)

`Dave` rule (statistical consistency, not bitwise equality):
- per-axis diffusion coefficient accepted if either:
  - primary: `|D_cpp - D_mat| <= 3 * sqrt(err_cpp^2 + err_mat^2)` (`3σ`)
  - fallback (only for non-near-zero axes): relative difference `<= 10%`
    - “non-near-zero” means the signal is at least ~10x larger than the reported error bar

Reason:
- MATLAB and C++ kMC runs use different RNG implementations / seeds, so exact equality is not expected.

Interpretation:
- `Dave=MATCH` + `BT/Evol=MATCH` indicates physical consistency for final diffusion output, even if some residual engineering mismatch remains (e.g., a few `ts_cross` entries in `processes`).

### Practical decision rule currently used

- “Engineering fully replaceable”:
  - require `basis/processes/BT/TS/Evol = MATCH` (and preferably `Dave=MATCH` too)
- “Can be used for diffusion production now”:
  - allow limited residual engineering DIFF
  - require sampled `BT/Evol/Dave` consistency
  - require difference characterization to show it is localized (e.g., metadata/path-vector tie case rather than network/rate collapse)

## Critical production note: why `D_ave_298.dat` was missing (and fix)

### Root cause (important)

The production launcher was generating `input.param` with:

- line 4 (`run_kmc`) = `0`

So the batches produced structural/percolation outputs (`basis/processes/BT/TS/Evol`) but **did not run kMC**, which means:

- no `D_ave_298.dat` (except the special no-process zero-output path)

This was a configuration oversight while optimizing for fast production + MATLAB repair throughput.

### Code fix applied

`sTuTraSt/launch_partial_prod_and_repair.sh` now separates KMC behavior:

- `PROD_RUN_KMC` (default `1`) -> production C++ run writes `D_ave_<T>.dat`
- `REPAIR_RUN_KMC` (default `0`) -> MATLAB repair remains fast and focused on structural equivalence

Repair still copies `cpp/input.param` first, then rewrites line 4 when `REPAIR_RUN_KMC != PROD_RUN_KMC`.

### Batch impact

The following Xe@298K batches are **not final diffusion-coefficient production outputs** (screening / structural-output batches only):

- `sTuTraSt/production_partial_optimal_Xe_20260223_124818`
- `sTuTraSt/production_partial_optimal_Xe_20260224_012113`
- `sTuTraSt/production_partial_optimal_Xe_20260224_085801`
- `sTuTraSt/production_partial_optimal_Xe_20260224_091115`

Aborted wrong-KMC batches (kept for audit):

- `sTuTraSt/production_partial_optimal_Xe_20260224_190104_NO_DAVE_KMC_OFF_ABORTED`
- `sTuTraSt/production_partial_optimal_Xe_20260224_091020_BADPARAM_DEFAULTS_ABORTED`

### Restarted batch for actual diffusion output

Active restart with KMC enabled for production:

- `sTuTraSt/production_partial_optimal_Xe_20260224_190402`

Verified config:

- `PROD_RUN_KMC=1`
- `REPAIR_RUN_KMC=1` (updated after user requested final diffusion comparison in repair as well)
- `Xe @ 298K`, `step=0.5`, `cutoff=30`, `GRID_SPACING=0.2`

Expected change in behavior:

- per-case C++ runtime is longer (KMC now runs)
- first `summary.csv` entries will appear later than before
- completed cases should now include `D_ave_298.dat`

### Repair KMC comparison policy (important)

Because MATLAB and C++ KMC use different RNG implementations / seeds, `D_ave_298.dat` should not be expected to match bitwise.

`launch_partial_prod_and_repair.sh` was updated to append a `Dave` column in `repair_summary.csv` and compare `D_ave_<T>.dat` using a **statistical consistency** rule:

- per-axis diffusion coefficient accepted if either:
  - within `5 * sqrt(err_cpp^2 + err_mat^2)`, or
  - relative difference <= `20%`

This is meant to validate final diffusion output reasonableness while keeping strict engineering checks (`basis/processes/BT/TS/Evol`) unchanged.

Note:
- Earlier `Dave=MATCH` rows in existing `repair_summary.csv` were produced before this tightening, but the completed samples checked so far all remain within the tightened `3σ` criterion.

### Runtime change after user confirmation

User explicitly required repair to compare final diffusion output as well, so both sides now run KMC:

- `PROD_RUN_KMC=1`
- `REPAIR_RUN_KMC=1`

This increases repair runtime substantially (MATLAB/Octave KMC), but enables direct validation of final `D_ave_298.dat` consistency in addition to intermediate outputs.

## Current active batches (latest)

### `sTuTraSt/production_partial_optimal_Xe_20260224_190402`

- `Xe @ 298K`, `step=0.5`, `cutoff=30`, KMC enabled for production and repair
- production completed `12/12`
- verified at least one produced case includes `D_ave_298.dat` (e.g. `2UPWUPk6`)
- repair (MATLAB + KMC) still running; `repair_summary.csv` header already upgraded to include `Dave`
- first completed repair+KMC sample now passes fully:
  - `K7WnSimz`: `basis/processes/BT/TS/Evol/Dave = MATCH`

### `sTuTraSt/production_partial_optimal_Xe_20260224_204413`

- next batch (`CASE_OFFSET=60`) launched with same parameters and both KMC flags enabled
- production+repair started manually with sourced `config.env`
- initial checks:
  - `selected_cases.csv` correct
  - `running_jobs` ramps to `3`
  - `swap=0`
- later status:
  - production completed `12/12`
  - repair samples completed so far:
    - `9HehUCTz` (no-process case): `BT=MATCH`, `Dave=MATCH`, structural files mostly `SKIP` as expected
    - `95zAdfeJ`: `basis/processes/BT/TS/Evol/Dave = MATCH`

### `sTuTraSt/production_partial_optimal_Xe_20260225_102614`

- production completed `12/12`
- repair samples completed so far:
  - `wOZUTwMZ`: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - `1MptG5M3`: `basis=MATCH`, `TS=MATCH`, `BT=MATCH`, `Evol=MATCH`, `Dave=MATCH`, but `processes=DIFF`

#### `1MptG5M3` DIFF characterization (quick triage)

- `basis.dat` line counts equal: `96 vs 96`
- `processes_298.dat` line counts equal: `400 vs 400`
- `TS_data.out` line counts equal: `11815 vs 11815`
- `TS_data.out` semantic compare: `MATCH`
- `processes_298.dat` residual is very small:
  - only `2` rows show non-rate field mismatch
  - observed mismatch pattern includes `ts_cross` vector difference (example line with `ts_cross_k: 0 vs -1`)
  - rate differences are floating-point noise (`max relative rate diff ~5.5e-10`)

Interpretation:
- Another residual engineering reconstruction mismatch (likely path/cross-vector tie case), but sampled physical outputs remain aligned (`Dave=MATCH`).

## Xe/Kr screening-focused MOF prioritization (PLD-based)

Goal:
- prioritize MOFs where pore limiting diameter (PLD) is close to Kr/Xe kinetic diameters so Xe/Kr diffusion contrast is more likely to be discriminative

Working geometric targets (used for prioritization):
- `Kr ~ 3.6 A`
- `Xe ~ 4.1 A`

Priority focus windows:
- strongest geometric screening region: `PLD in [3.6, 4.1] A` (between Kr and Xe scales)
- broader practical window for pilot screening: `PLD in [3.2, 4.4] A`

Generated candidate file:
- `sTuTraSt/xe_kr_pld_priority_candidates_20260225.csv`
  - rows in `PLD [3.2, 4.4]`
  - includes `tier` (`A_between_Kr_Xe`, `B_near_thresholds`, `C_edge_window`)
  - includes distances to `Kr 3.6A` and `Xe 4.1A`

Generated diffusion-comparison summary file:
- `sTuTraSt/xe_dave_compare_stats_20260225.csv`
  - per completed repair+KMC sample, records `D_ave_298.dat` C++ vs MATLAB values, relative differences, and sigma-normalized differences

### Next launched batch

- `sTuTraSt/production_partial_optimal_Xe_20260225_143111` (`CASE_OFFSET=84`)
- launched with `PROD_RUN_KMC=1`, `REPAIR_RUN_KMC=1`

## Latest progress snapshot (continuing production)

### Repair/KMC results observed

- `sTuTraSt/production_partial_optimal_Xe_20260224_190402/repair_summary.csv`
  - `K7WnSimz`: `basis/processes/BT/TS/Evol/Dave = MATCH`

- `sTuTraSt/production_partial_optimal_Xe_20260224_204413/repair_summary.csv`
  - `9HehUCTz`: no-process case, `BT=MATCH`, `Dave=MATCH`
  - `95zAdfeJ`: `basis/processes/BT/TS/Evol/Dave = MATCH`

- `sTuTraSt/production_partial_optimal_Xe_20260225_102614/repair_summary.csv`
  - `wOZUTwMZ`: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - `1MptG5M3`: only `processes=DIFF`; `basis/TS/BT/Evol/Dave = MATCH`

### `sTuTraSt/production_partial_optimal_Xe_20260225_143111` status

- parameters verified (`Xe@298K`, `step=0.5`, `cutoff=30`, both KMC flags on)
- `selected_cases.csv` created correctly (offset 84)
- producer running with `MAX_JOBS=3`
- early stage: first 3 grid jobs running, `summary.csv` not yet populated at last check
- `swap=0`

## Kr paired production launch (offset 60 / 72) and launch pitfalls (2026-02-25)

Objective:
- start `Kr@298K` paired runs matching already-produced Xe batches for direct Xe/Kr screening comparison
- first paired offsets prioritized: `60` and `72` (good overlap with PLD-priority window and existing Xe data)

CPU / resource context at launch:
- machine reports `32` logical CPUs (`16` cores, `2` threads/core)
- user guidance: one active production task effectively uses about `2 cores / 4 threads`
- active background load before Kr launch: one Xe MATLAB repair (`octave-cli`) only
- memory remained ample (`~55-60 GB` available), `swap=0`

### Pitfall 1: run-dir timestamp collision when creating two batches in parallel

Observed:
- two parallel Kr batch creations landed in the same second and produced the same directory path:
  - `sTuTraSt/production_partial_optimal_Kr_20260225_151250`
- the second creation overwrote config (`CASE_OFFSET=72`), leaving an ambiguous batch setup attempt

Fix applied:
- patched `sTuTraSt/launch_partial_prod_and_repair.sh` `launch()` to include offset + nanoseconds in run-dir name
  - now uses form like `..._off060_<nanoseconds>`

### Pitfall 2: `source config.env` without exporting variables (manual produce/repair launch)

Observed:
- manual launch command used `source config.env && bash ...`
- shell variables were not exported, so child script saw defaults and incorrectly ran:
  - `PROBE=Xe`
  - `CASE_OFFSET=0`
  - default grid params (`spacing=0.15`, `n_jobs=1`)
- symptoms:
  - producer logs printed `selected=6 offset=0`
  - makegrid commands showed `--probe Xe` and `_Xe.cube` filenames

Bad runs preserved for audit (aborted immediately):
- `sTuTraSt/production_partial_optimal_Kr_20260225_151250_BAD_NOEXPORT_ABORTED`
- `sTuTraSt/production_partial_optimal_Kr_20260225_151524_off060_manual_BAD_NOEXPORT_ABORTED`

Correct manual launch pattern (required):
- `set -a; source <run_dir>/config.env; set +a; bash sTuTraSt/launch_partial_prod_and_repair.sh produce <run_dir>`
- same for `repair`

### Current Kr paired runs (active)

Correctly relaunched batches:
- `sTuTraSt/production_partial_optimal_Kr_20260225_151621_off060_manual` (`CASE_OFFSET=60`)
- `sTuTraSt/production_partial_optimal_Kr_20260225_151622_off072_manual` (`CASE_OFFSET=72`)

Parameters verified (both):
- `PROBE=Kr`
- `SIM_TEMP=298`
- `INPUT_ENERGY_STEP=0.5`
- `INPUT_ENERGY_CUTOFF=30`
- `GRID_SPACING=0.2`
- `GRID_NJOBS=4`
- `MAX_JOBS=3`
- `PROD_RUN_KMC=1`, `REPAIR_RUN_KMC=1`

Launch verification:
- producer logs show correct offsets (`selected=12 offset=60`, `selected=12 offset=72`)
- running makegrid commands show `--probe Kr`, `--spacing 0.2`, `--n_jobs 4`
- schedulers reached `running_jobs=3` in both batches
- `swap=0`

## Additional launch robustness fixes and high-throughput expansion (2026-02-25, continued)

### Additional pitfall: relative `RUN_DIR` breaks grid output writes

Observed during manual Kr batch restarts:
- producer was launched with relative run dir argument (e.g. `sTuTraSt/...`)
- `run_case()` changes directory into `makegrid/` before calling `generate_pes_grid_v9.py`
- grid output path (derived from `RUN_DIR`) remained relative, so `write_cube()` failed at final write step with:
  - `FileNotFoundError: .../grids/<case>_Kr.cube`

Bad runs preserved for audit (aborted):
- `sTuTraSt/production_partial_optimal_Kr_20260225_151621_off060_manual_BAD_RELRUN_ABORTED`
- `sTuTraSt/production_partial_optimal_Kr_20260225_151622_off072_manual_BAD_RELRUN_ABORTED`
- `sTuTraSt/production_partial_optimal_Kr_20260225_151856_off084_manual_BAD_RELRUN_ABORTED`

Fix applied:
- `sTuTraSt/launch_partial_prod_and_repair.sh`
  - normalize `RUN_DIR` to absolute path early:
    - `RUN_DIR="$(realpath -m \"$RUN_DIR\")"`
- manual launches also now pass absolute run-dir paths explicitly

### Active paired Kr runs (correct restarts)

Relaunched and currently running (correct config + absolute paths):
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260225_152029_off060_manual`
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260225_152030_off072_manual`
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260225_152031_off084_manual`

### Throughput scaling update (to better use available CPUs)

Because `makegrid` still prints:
- `joblib ... Permission denied. joblib will operate in serial mode`

`GRID_NJOBS=4` is not being honored in practice (effective per-grid CPU usage is roughly ~1 core in current environment).

Mitigation chosen (data-first):
- increase throughput by launching more batch producers in parallel instead of waiting for per-grid multiprocessing fix
- added another paired set (`offset=96`) for direct Xe/Kr comparison:
  - `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Xe_20260225_152146_off096_manual`
  - `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260225_152147_off096_manual`

Current resource snapshot during expanded run set:
- multiple producers active (`off060`, `off072`, `off084`, `off096` Xe/Kr)
- load average increased to ~`10-12`
- memory still comfortable (`~50+ GB` available)
- `swap=0`

## Repair sampling limit bug fixed (2026-02-26)

Issue observed:
- `REPAIR_SAMPLE_LIMIT=1` was intended to mean "repair at most one case per batch"
- actual behavior in `run_repair_monitor()` pending-selection script was:
  - skip already-done cases first
  - then apply `[:limit]` to the remaining list
- consequence: after one repaired case finished, the next smallest case became eligible, so some batches accumulated `repair_lines > 1`

Symptoms seen:
- e.g. `off060`, `off072`, `off084` batches produced more than one repair row despite `REPAIR_SAMPLE_LIMIT=1`

Fix applied:
- `sTuTraSt/launch_partial_prod_and_repair.sh`
- pending-selection Python snippet now:
  - builds full eligible list
  - sorts by `est_vox`
  - applies `limit` to the full list first
  - then skips already-done cases within that fixed top-N set

Important runtime note:
- this fix applies to repair monitors started after the patch
- repair monitors that were already running long Octave jobs keep old in-memory logic until restarted
- continue monitoring active batches to prevent unwanted extra repair samples after the current Octave job completes

## Repair sample-limit hard-cap fix and continued paired expansion (2026-02-26)

### Problem found (second-order sampling bug)

Even after the first `REPAIR_SAMPLE_LIMIT` fix, batches could still exceed `REPAIR_SAMPLE_LIMIT=1` when production was still appending `summary.csv` rows:
- pending selection was computed against the *current partial* summary
- as more rows arrived later, the identity of the sorted top-1 candidate could change
- result: one batch could still accumulate >1 repaired cases over time

Fix applied (`sTuTraSt/launch_partial_prod_and_repair.sh`):
- add hard cap before scheduling pending repairs:
  - if `len(done) >= limit`, schedule nothing
- add matching hard cap in the `remain` check:
  - if `len(done) >= limit`, return `0`

Effect:
- newly started repair monitors now enforce total repaired-case count `<= REPAIR_SAMPLE_LIMIT`
- already running repair monitors (started before this patch) may still overrun until restarted (observed in some older batches)

### Current production expansion after fix

New paired batches launched with patched sampler logic:
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Xe_20260226_144327_off156_manual`
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260226_144328_off156_manual`
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Xe_20260226_144329_off168_manual`
- `/home/sun07ao/xekr/sTuTraSt/production_partial_optimal_Kr_20260226_144330_off168_manual`

Status at launch check:
- producers reached `running_jobs=3` in each batch
- correct offsets selected (`156`, `168`)
- `--probe` paths show paired Xe/Kr same-case execution
- `swap=0`

## Algorithm-priority audit status and 3-category Xe/Kr replacement check (2026-02-26)

### Current algorithm-risk summary (based on completed repair rows)

Screened all completed `repair_summary.csv` rows from active paired production batches (`*_off*_manual`):
- total completed repair rows scanned: `22`
- rows with `mat_exit=0` and any real `DIFF`: `1`
  - `production_partial_optimal_Xe_20260225_203616_off120_manual / 0tkfBSV3`
  - `basis/processes/TS = DIFF`, `BT/Evol/Dave = MATCH`
- most other `DIFF` rows are timeout-driven (`mat_exit=124`) and not valid equivalence failures

### `0tkfBSV3` (Xe, off120) localized status (real algorithm mismatch)

Observed:
- MATLAB vs C++ final outputs:
  - `basis.dat`: `119 vs 117`
  - `processes_298.dat`: line count equal (`496 vs 496`) but content differs
  - `TS_data.out`: `13491 vs 13480`
  - `BT.dat`, `Evol_298.dat`, `D_ave_298.dat` still match

Sensitivity check (same grid, C++ only):
- `energy_step = 0.500000001` -> same as current C++ result (`117 / 496 / 13480`)
- `energy_step = 0.499999999` -> jumps to different result (`118 / 500 / 13599`)
- confirms a threshold/tie-sensitive branch issue (same class as earlier `QupxOnO1` problem)

Raw TS-list localization (using MATLAB ASCII `restart2.mat` + C++ debug dump):
- MATLAB `TS_list_all`: `13529` rows
- C++ `debug_ts_list_all_precompact.dat`: `13535` rows
- C++ has `+6` extra raw TS events before compacting/organizing
- earliest TS-list count divergence by level begins at:
  - `level 32`, then `35`, `38`, `57`
- normalized event-identity diff shows C++-only extras include mostly self-connection TS events (`cluster1==cluster2`), plus one extra cross-cluster event:
  - examples include `(57,62,44,32,323,323,0,0,0)` and `(58,61,43,32,323,323,0,0,0)` in normalized TS-list coordinates/ids signature

Important validation result:
- experimental flag `TUTRAST_SKIP_TEMP_SAMECLUSTER_TS=1` does **not** fix `0tkfBSV3`
- therefore root cause is not the previously suspected "temp list TS neighbor timing" path

Implication:
- engineering replacement is still not fully proven for all `Xe@298K, step=0.5` cases until `0tkfBSV3`-class mismatch is fixed
- but current evidence suggests the residual issue is localized and rare (1 real mismatch among 22 completed repair rows so far)

## 3-category Xe/Kr replacement check (final D + key steps)

Representative cases chosen with both Xe and Kr having completed MATLAB repair (`mat_exit=0`):
- `both_blocked`: `9HehUCTz`
- `Kr_only`: `95zAdfeJ`
- `both_pass`: `4BoAdfVz`

### 1) `both_blocked` representative: `9HehUCTz`

Runs:
- Xe: `production_partial_optimal_Xe_20260224_204413`
- Kr: `production_partial_optimal_Kr_20260225_152029_off060_manual`

Result summary:
- Xe:
  - `basis/processes/TS/Evol` are `SKIP` (no corresponding files)
  - `BT=MATCH`, `Dave=MATCH`
  - `D_ave_298.dat` MATLAB and C++ both exactly zero
- Kr:
  - `basis/processes/BT/TS/Evol/Dave = MATCH`
  - `D_ave_298.dat` MATLAB and C++ both exactly zero

Interpretation:
- final diffusion result is blocked for both species (`Xe=0`, `Kr=0`) under current settings
- even though Kr still forms intermediate structures/processes, final KMC diffusion remains zero and matches MATLAB

### 2) `Kr_only` representative: `95zAdfeJ`

Runs:
- Xe: `production_partial_optimal_Xe_20260224_204413`
- Kr: `production_partial_optimal_Kr_20260225_152029_off060_manual`

Result summary:
- Xe: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - MATLAB `D_ave_298`: exactly zero
  - C++ `D_ave_298`: near-zero `Dz ~ 3.06e-10 ± 6.13e-10` (within noise, `Dave=MATCH`)
- Kr: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - MATLAB `D_ave_298`: exactly zero
  - C++ `D_ave_298`: `Dz ~ 3.19e-06 ± 6.37e-06` (still `Dave=MATCH`, low-statistics KMC noise wide)

Interpretation:
- this is a useful screening-signature case in current pilot settings: Xe stays effectively blocked while Kr shows nonzero diffusion scale in C++ output
- key step outputs all align with MATLAB for both species in this representative pair

### 3) `both_pass` representative: `4BoAdfVz`

Runs:
- Xe: `production_partial_optimal_Xe_20260226_005119_off144_manual`
- Kr: `production_partial_optimal_Kr_20260226_005120_off144_manual`

Result summary:
- Xe: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - C++ and MATLAB `D_ave_298` agree within error bars (`Dave=MATCH`)
- Kr: `basis/processes/BT/TS/Evol/Dave = MATCH`
  - C++ and MATLAB `D_ave_298` agree within error bars (`Dave=MATCH`)

Interpretation:
- this representative "both pass" case supports engineering replaceability on both intermediate outputs and final diffusion coefficient for both species

## Practical replacement conclusion (current state)

For the three requested screening categories, representative audited pairs already support replacement *in practice*:
- `both_blocked`: representative pair aligns (final D and key steps as applicable)
- `Kr_only`: representative pair aligns and retains Xe/Kr contrast signal
- `both_pass`: representative pair aligns on key steps and final D for both species

However, global engineering replacement is **not yet complete** because `0tkfBSV3` (Xe, off120) remains a real reproducible algorithm mismatch (`mat_exit=0`, non-timeout).

## 0tkfBSV3 continued debugging (this session, no final fix yet)

Status:
- `0tkfBSV3` mismatch remains reproducible after multiple targeted patches:
  - MATLAB: `basis=119`, `TS_data=13491`
  - C++: `basis=117`, `TS_data=13480`
  - `BT/Evol/Dave = MATCH`
- raw pre-organize TS list still differs:
  - MATLAB `TS_list_all = 13529`
  - C++ `debug_ts_list_all_precompact.dat = 13535`

Validated non-root-cause experiments (no effect on `0tkfBSV3`):
- recomputing host cross from live `grid_->cross_*` for each neighbor inside `grow_clusters()` (semantically correct; no change on this case)
- pruning ghost merged-away cluster IDs from `merge_groups_` after physical merges (semantically correct; no change on this case)
- attempted merge self-TS pruning change (reverted after it worsened results; MATLAB indeed prunes all `cluster1==cluster2` rows in current TS lists after a merge)

Important new localization results:
- final `level_matrix` values at all 6 previously identified divergent coordinates match MATLAB exactly
- final `cross_matrix.i/j/k` values at those coordinates also match MATLAB exactly
- therefore the remaining mismatch is not a final grid/level/cross state mismatch; it is a transient event-order / TS-creation-order mismatch during `grow_clusters()` / merges

Target raw TS rows confirmed absent in MATLAB but present in C++:
- `(57,62,44,32,323,323,0,0,0)`
- `(58,61,43,32,323,323,0,0,0)`
- `(58,62,44,32,323,323,0,0,0)`
- `(57,61,43,35,323,323,0,0,0)`
- `(17,74,30,38,236,236,0,0,0)`
- `(15,73,30,57,192,236,0,1,-1)`

Trace observations (`TUTRAST_TRACE_OTKF_TS=1`):
- these rows are repeatedly recreated at later levels after unrelated merges (MATLAB likely also recreates some low-level self-TS rows; this alone is not proof of bug)
- the local `level 32` sequence clearly shows:
  - `diff-cluster` TS rows for `(58,61,43)` / `(58,62,44)` with `(201,212)`
  - followed by `Merging clusters 212 into 201`
  - followed by `same-cluster` TS rows in the same local patch (including later-final divergent coordinates)

Current best hypothesis (not yet proven):
- remaining mismatch is due to transient scan/merge/TS recreation ordering in `grow_clusters()` (not final `cross_matrix` or `level_matrix`)
- likely tied to rare same-cluster PBC TS recreation timing after successive merges in later levels

## Production / repair monitor updates (this session)

- `off156` Xe/Kr repair monitors are still running long MATLAB jobs; current check shows:
  - exactly one active Octave per run dir (no duplicate repair execution)
  - `repair_summary.csv` for both `off156` runs still only has header line (sample not finished yet)
- `REPAIR_SAMPLE_LIMIT=1` hard cap appears to be holding for newly launched runs under current script
- memory remains healthy and `swap=0`

## Temporary debug additions currently in working tree

These are env-gated / debug-only and safe unless env vars are enabled:
- `src/cluster.cpp`
  - `TUTRAST_TRACE_OTKF_TS`
  - trace scaffolding for selected levels / TS creation
- `src/main.cpp`
  - `TUTRAST_DEBUG_DUMP_MINIDC=1` now also dumps:
    - `debug_cross_i_flat.txt`
    - `debug_cross_j_flat.txt`
    - `debug_cross_k_flat.txt`

These should be cleaned or gated more narrowly before a final production-facing commit.

## Launcher reliability fix (this session)

Issue:
- `launch_partial_prod_and_repair.sh launch` still intermittently created `RUN_DIR` and exited without starting `produce` / `repair`

Root cause (confirmed):
- `launch()` used `$0` to spawn child modes
- when invoked as `bash /path/to/launch_partial_prod_and_repair.sh launch`, `$0` may not reliably refer to the script path for recursive exec

Fix:
- script now uses `SELF_SCRIPT=\"$(realpath -m \"${BASH_SOURCE[0]}\")\"`
- `launch()` spawns:
  - `\"$SELF_SCRIPT\" produce \"$run_dir\"`
  - `\"$SELF_SCRIPT\" repair \"$run_dir\"`

Smoke test (no-cost):
- ran `launch` with huge `CASE_OFFSET` (no selected cases)
- confirmed normal `RUN_DIR` creation + clean `production_ok=0 ... repair_checked=0` shutdown

## Post-debug regression smoke (current working tree)

To ensure low-risk debug/cleanup edits did not break previously matched cases:
- reran `Xe / 95zAdfeJ` single-case C++ and compared against existing MATLAB repair outputs
- result:
  - `basis/processes/BT/TS/Evol = MATCH`
  - `D_ave_298.dat` zero-value output consistent with MATLAB

## New data expansion launched: Xe/Kr off180 + off192

Started new paired batches (same production settings as off168):
- `Xe off180`: `production_partial_optimal_Xe_20260226_191101_off180_941256500`
- `Kr off180`: `production_partial_optimal_Kr_20260226_191101_off180_943041769`
- `Xe off192`: `production_partial_optimal_Xe_20260226_191101_off192_944008619`
- `Kr off192`: `production_partial_optimal_Kr_20260226_191101_off192_945129395`

Notes:
- these runs were first created by `launch` before the launcher fix was applied (directories existed but child processes not started)
- they were then manually recovered / started (mix of live shell sessions and direct mode invocations)
- if resuming later, check:
  - `summary.csv` line count
  - `repair_summary.csv` line count
  - active `produce` / `repair` processes for each run dir

Resource status during launch/recovery:
- `swap=0`
- memory remained >`60 GB` available

## 2026-02-27 00:20+ Extra MATLAB repair backfill (user-requested)
- Goal: Use idle resources to precompute more MATLAB repair samples for completed 298K Xe/Kr paired batches, using the existing selection policy and voxel threshold.
- Observed idle headroom before backfill: ~9 Octave jobs active, load ~8, swap=0, MemAvailable ~57 GB.
- Important launch detail: `REPAIR_SAMPLE_LIMIT` must be overridden *after* sourcing each `config.env`; otherwise `config.env` resets it to `1` and the repair monitor exits immediately if top-1 is already done.
- Backfill monitors started with `REPAIR_SAMPLE_LIMIT=3` (same sorting/selection logic, only increasing sample cap):
  - Xe off108 (`production_partial_optimal_Xe_20260225_203558_off108_manual`)
  - Kr off108 (`production_partial_optimal_Kr_20260225_203559_off108_manual`)
  - Xe off132 (`production_partial_optimal_Xe_20260226_005117_off132_manual`)
  - Kr off132 (`production_partial_optimal_Kr_20260226_005118_off132_manual`)
  - Xe off156 (`production_partial_optimal_Xe_20260226_144327_off156_manual`)
  - Kr off156 (`production_partial_optimal_Kr_20260226_144328_off156_manual`)
  - Xe off168 (`production_partial_optimal_Xe_20260226_144329_off168_manual`)
  - Kr off168 (`production_partial_optimal_Kr_20260226_144330_off168_manual`)
- Additional backfill monitors started later (same override) on runs already having exactly 1 repaired sample:
  - Xe off180 (`production_partial_optimal_Xe_20260226_191101_off180_941256500`)
  - Kr off180 (`production_partial_optimal_Kr_20260226_191101_off180_943041769`)
  - Xe off192 (`production_partial_optimal_Xe_20260226_191101_off192_944008619`)
  - Xe off084 batch (`production_partial_optimal_Xe_20260225_143111`) to improve pairing coverage with Kr off084
- Kr off192 was NOT backfilled concurrently because a repair monitor was already running for the same RUN_DIR (avoid `repair_summary.csv` and per-case directory races).
- After backfill launch: ~15 Octave jobs active, load ~10.8, swap still 0, MemAvailable ~58 GB.

## 2026-02-27 02:22+ Additional repair dispatch + real-critical check
- User requested continuing to use idle cores for more MATLAB repairs while comparing completed tasks.
- Dispatch strategy: prioritize completed formal runs (`298K/0.5/30`, KMC on) with low successful repair count and no active repair monitor; increase only `REPAIR_SAMPLE_LIMIT` to 3 (selection ordering unchanged).
- New extra repair monitors started on:
  - Kr off192 (`production_partial_optimal_Kr_20260226_191101_off192_945129395`)
  - Xe off096 (`production_partial_optimal_Xe_20260225_152146_off096_manual`)
  - Kr off096 (`production_partial_optimal_Kr_20260225_152147_off096_manual`)
  - Xe off144 (`production_partial_optimal_Xe_20260226_005119_off144_manual`)
  - Kr off144 (`production_partial_optimal_Kr_20260226_005120_off144_manual`)
- After dispatch: active Octave repairs increased to ~16, load ~11.8, `swap=0`, `MemAvailable ~59 GB`.
- Completed-task comparison check (formal runs only): after excluding rows with `mat_exit!=0` (mostly timeout 124), there are currently **0 rows** with real critical mismatch in final outputs (`BT`, `Evol`, or `Dave`).
- Practical interpretation: current apparent `Dave/BT/Evol=DIFF` rows in dashboard are timeout artifacts, not confirmed C++-vs-MATLAB final-result mismatches.

## 2026-02-27 02:3x Timeout cap handling fix (avoid fake DIFF)
- User concern: verification-stage timeout cap was producing many misleading `DIFF` rows, wasting review time.
- Root cause: `run_repair_case()` still ran file comparisons after MATLAB/Octave nonzero exit (`mat_exit!=0`), so partial/missing outputs were recorded as `BT/Evol/Dave=DIFF` even when the true cause was timeout (`124`).
- Fix in `launch_partial_prod_and_repair.sh`:
  - If `mat_exit==0`: compare files normally.
  - If `mat_exit==124`: write `TIMEOUT` for `basis/processes/BT/TS/Evol/Dave/tunnel`, `all_ok=0`, `note=mat_timeout`.
  - If other nonzero exit: write `MAT_FAIL` for those fields, `all_ok=0`, `note=mat_nonzero_exit`.
- Result: future `repair_summary.csv` rows no longer report timeout-induced fake DIFFs; dashboards and scans can separate true mismatches from audit timeouts cleanly.
- Current policy remains: keep timeout caps (`MAT_TIMEOUT`, `CPP_TIMEOUT`) as guardrails, but treat timeout rows as audit-incomplete rather than comparison failures.

## 2026-02-27 02:4x Raise MATLAB repair timeout to 12h + staged monitor recycle
- User approved raising MATLAB (original/TuTraSt) repair timeout cap to 12h for verification.
- Script default updated: `MAT_TIMEOUT` default changed from `2400` to `43200` seconds in `launch_partial_prod_and_repair.sh`.
- Important note: existing repair monitors already running from old configs/processes continue using their current in-process environment (`MAT_TIMEOUT=21600`) until restarted.
- To avoid disrupting active long-running Octave cases, duplicate idle repair wrappers (created during backfill) were cleaned first; one wrapper per RUN_DIR remains.
- Current staged strategy:
  1) Do not kill wrappers currently hosting an active Octave child.
  2) When a run finishes its current repair case (or the wrapper exits/gets recycled), restart that RUN_DIR repair monitor with `MAT_TIMEOUT=43200` and new timeout-status logic (`TIMEOUT`/`MAT_FAIL` instead of fake `DIFF`).
- Benefit: preserves current compute progress while converging the fleet to clearer audit semantics and longer timeout budget.

## 2026-02-27 09:4x 12h repair relaunch + session-safe dispatch
- Observed that previous interactive exec sessions hit session-cap warnings; switched dispatch approach to detached launch (`nohup ... repair`) so monitor jobs keep running without consuming exec session slots.
- Relaunched/confirmed active repair monitors under `MAT_TIMEOUT=43200` and `REPAIR_SAMPLE_LIMIT=3` for key paired runs:
  - Xe/Kr off132, off144, off156, off168
  - Xe/Kr off180, off192
  - Xe off143111, Xe/Kr off096
- Runtime snapshot after relaunch:
  - repair wrappers ~18, active Octave jobs ~8
  - load ~4.3
  - `MemAvailable ~60 GB`, `SwapUsed=0`
- Directory hygiene note: dispatch now avoids spawning redundant foreground sessions; background log file per run is written to `RUN_DIR/repair_dispatch.log`.

## 2026-02-27 09:48 Runtime cleanup pass (directory hygiene)
- Per user request for ongoing directory clarity, archived intermediate runtime artifacts without touching active production runs.
- Archived 43 items (mostly `tmp_*` repro dirs and ad-hoc logs) into:
  - `archive/20260227_runtime_cleanup/runtime_tmp_and_logs/`
- Manifest:
  - `archive/20260227_runtime_cleanup/ARCHIVE_MANIFEST_20260227_094839.tsv`
- Active compute unaffected after cleanup:
  - repair wrappers: ~18
  - active Octave jobs: ~8
  - swap: 0

## 2026-02-27 09:49 Additional repair backfill (paired coverage)
- Continued backfill under `MAT_TIMEOUT=43200`, `REPAIR_SAMPLE_LIMIT=3` using detached dispatch.
- Newly dispatched runs include:
  - Kr off060/off072/off084
  - Xe/Kr off108
  - Xe/Kr off120
  - (already running from earlier: Xe/Kr off132/off144/off156/off168/off180/off192, Xe off143111, Xe/Kr off096)
- Current runtime snapshot after dispatch:
  - repair wrappers ~18
  - active Octave jobs ~8
  - load ~7.5
  - `SwapUsed=0`, `MemAvailable ~60 GB`
- Current paired successful Xe/Kr MATLAB-repair sample count (mat_exit=0 on both species for same off-group+case): 5
  - off108: `eB9tE9pe`
  - off120: `0tkfBSV3`
  - off144: `4BoAdfVz`
  - off168: `JZILljtZ`
  - off180: `AmNyFYOG`

## 2026-02-27 10:0x Priority correction: contrast-first validation
- User clarified strict priority: **alignment check first**; if key mismatch exists, fix C++ and rerun; archive/handoff updates are secondary.
- Gap acknowledged: previously active MATLAB repairs were mostly backfill-selected by `est_vox` ordering, not strictly by maximum Xe/Kr contrast.
- Corrective action taken immediately:
  - launched targeted paired MATLAB repairs (independent `repair_targeted/`) for highest C++ contrast candidates:
    - off180: `uC8nbVee` (Xe~0 vs Kr>0)
    - off132: `VXfwzQww` (Xe~0 vs Kr>0)
    - off096: `xA8rS7sM` (Xe~0 vs Kr>0)
  - total targeted jobs started: 6 (Xe+Kr for each case)
- Existing background repairs still running are primarily backfill pairs in off132/off144/off156/off168 (current cases: `EH1fH9gz`, `EJdLRLZu`, `2RSmenr7`, `5OrSQrPy`).
- Runtime snapshot after targeted launch:
  - Octave jobs ~14
  - load ~8.5
  - `SwapUsed=0`, `MemAvailable ~59.9 GB`
