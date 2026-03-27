# TODOS

## KMC

### Add Manifest-Backed Multi-Run Continuation

**What:** Add Phase 2 support for manifest-backed continuation across additional runs and longer run horizons.

**Why:** Phase 1 exact resume solves interrupted single-run recovery, but the original user goal also requires continuing a study without discarding prior valid work when `n_steps` or `n_runs` were undersized.

**Context:** Build this only after Phase 1 exact resume is stable. Reuse the Phase 1 checkpoint format, explicit CLI model, and `KmcRunState` foundation rather than inventing a second persistence path. This should cover appending more runs, extending horizons, and retaining the latest verified checkpoint until a campaign is finalized.

**Effort:** L
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
