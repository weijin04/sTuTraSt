# TODOS

## Completed

### Add Manifest-Backed Append-Runs Continuation

**What:** Added manifest-backed continuation for appending additional completed runs to an existing campaign without discarding prior valid work.

**Why:** This unlocks the first Phase 2 continuation workflow: finish `n_runs=2` today, then raise `n_runs` and continue from run 3 later while preserving exact seeded behavior.

**Context:** Implemented on branch `feature/kmc-checkpoint-phase1` with campaign manifest read/write, per-run checkpoint ownership inside the campaign directory, RNG-state carry-forward across runs, and a black-box regression that proves campaign append matches single-invocation baseline output.

**Effort:** L
**Priority:** P2
**Depends on:** Phase 1 exact resume implementation and regression harness
**Completed:** 191fba8+ (2026-03-27)

### Add Manifest-Backed Step-Horizon Extension

**What:** Extended the campaign system so a single-run campaign can grow from a smaller target step horizon to a larger one without discarding prior validated progress.

**Why:** This closes the other major continuation gap after append-runs: recovering when `n_steps` was undersized for a long run.

**Context:** Implemented on branch `feature/kmc-checkpoint-phase1` with explicit `--campaign-plan-steps`, lag-plan persistence in checkpoints/manifests, black-box extension regression, and an explicit rejection path for unsupported multi-run step extension under the current sequential-RNG semantics.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 exact resume implementation and regression harness
**Completed:** 9e03664+ (2026-03-27)

### Establish Medium And Heavy Benchmark Corpus

**What:** Added a reproducible synthetic `small / medium / heavy` benchmark corpus and a benchmark suite runner.

**Why:** This provides a stable performance/memory measurement path beyond the Phase 1 small regression case.

**Context:** Implemented via `benchmarks/generate_synthetic_fixture.py` and `benchmarks/run_benchmark_suite.py`, with medium/heavy cases designed to produce non-zero `Dz` so later optimization comparisons have signal.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 `CTest` harness and frozen small benchmark
**Completed:** 9e03664+ (2026-03-27)

### Add Statistical-Equivalence Acceptance For KMC Optimizations

**What:** Added a statistical-acceptance tool that compares baseline and candidate binaries over repeated seeds and reports per-axis mean deltas plus KS-style distribution distance.

**Why:** Future hot-loop optimizations can change stochastic trajectories even when they remain physically acceptable, so long-term validation cannot rely only on pointwise `msd` diff.

**Context:** Implemented via `benchmarks/statistical_acceptance.py`, plus smoke coverage in `CTest` by self-comparing the current binary. This is the first version of the validation harness; thresholds and fixture breadth can still evolve.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 harness and Phase 2 benchmark corpus
**Completed:** 9e03664+ (2026-03-27)
