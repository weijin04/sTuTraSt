# TODOS

## KMC

### Add Manifest-Backed Step-Horizon Extension

**What:** Extend the campaign system so an existing run can grow from a smaller target step horizon to a larger one without discarding prior validated progress.

**Why:** Append-runs continuation is now implemented, but the original user goal also included recovering from undersized `n_steps` choices on a single long run.

**Context:** Reuse the new campaign manifest and per-run checkpoint flow instead of inventing a second continuation path. This remaining work is specifically about changing step horizon semantics while keeping lag identity, checkpoint validity, and final output rules explicit.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 exact resume implementation and regression harness

## Benchmarking

### Establish Medium And Heavy Benchmark Corpus

**What:** Curate and freeze one or more medium/heavy benchmark cases for Phase 2 performance and memory validation.

**Why:** The small Phase 1 benchmark is enough to prove exact resume correctness, but it is not enough to prove that later optimizations improve real-world runtime and peak RSS on difficult systems.

**Context:** Add this after the Phase 1 `CTest` harness is stable. The corpus should be representative of the "white-run" pain points that motivated this work and should support repeatable measurement of wall time, peak memory, checkpoint overhead, and output correctness on harder cases.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 `CTest` harness and frozen small benchmark

## Validation

### Add Statistical-Equivalence Acceptance For KMC Optimizations

**What:** Design and implement a statistical-equivalence validation layer for future KMC performance optimizations.

**Why:** Once hot-loop optimizations change floating-point ordering, stochastic event paths may diverge even when the implementation remains physically acceptable. Pointwise `msd` diff is not a valid long-term gate for those changes.

**Context:** This is Phase 2+ work. Build it on top of the Phase 1 harness and the heavier benchmark corpus. Likely ingredients include repeated runs, derived-metric comparisons, and distributional tests for diffusion outputs so optimized paths can be judged rigorously without demanding identical trajectories.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 harness and Phase 2 benchmark corpus

## Completed

### Add Manifest-Backed Append-Runs Continuation

**What:** Added manifest-backed continuation for appending additional completed runs to an existing campaign without discarding prior valid work.

**Why:** This unlocks the first Phase 2 continuation workflow: finish `n_runs=2` today, then raise `n_runs` and continue from run 3 later while preserving exact seeded behavior.

**Context:** Implemented on branch `feature/kmc-checkpoint-phase1` with campaign manifest read/write, per-run checkpoint ownership inside the campaign directory, RNG-state carry-forward across runs, and a black-box regression that proves campaign append matches single-invocation baseline output.

**Effort:** L
**Priority:** P2
**Depends on:** Phase 1 exact resume implementation and regression harness
**Completed:** 191fba8+ (2026-03-27)
