# sTuTraSt

C++ implementation of TuTraSt (Tunnel and Transition State search) with MATLAB-compatible core outputs.

## Repository scope
This repository now keeps only:
- Core C++ source (`src/`, `include/`)
- Core MATLAB reference algorithm files (`*.m`)
- Build files (`CMakeLists.txt`, `Makefile`, `build.sh`)
- One runnable example (`examples/basic`)

Production pipelines, validation orchestration, and debugging artifacts are archived outside this repo.

## Build
```bash
make
# binary: build/tutrast
```

Or:
```bash
./build.sh
```

## Quick run (example)
```bash
cd examples/basic
../../build/tutrast
```

## C++ mkgrid (v10, auto-expand)
Build creates a second binary:
- `build/generate_pes_grid_v10_cpp`

Example:
```bash
cd /home/sun07ao/xekr/makegrid
/home/sun07ao/xekr/sTuTraSt/build/generate_pes_grid_v10_cpp \
  /path/to/xxx.cif /path/to/grid.cube \
  --probe Xe --spacing 0.2 --cutoff 12.8 --n_jobs 16 --cell_preexpand auto
```

Expected key outputs (written in current directory):
- `basis.dat`
- `processes_<T>.dat`
- `TS_data.out`
- `BT.dat`
- `Evol_<T>.dat`
- `D_ave_<T>.dat` (if kMC enabled)

## Input files
Run directory must contain:
- `grid.cube`
- `input.param`

The `input.param` ordering must follow TuTraSt conventions.

## Citation
Amber Mace, Senja Barthel and Berend Smit, J. Chem. Theory Comput. 2019, 15, 4, 2127-2141
