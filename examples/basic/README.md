# Basic Example

This folder provides a standard runnable input pair:
- `grid.cube`
- `input.param`

## Run
From repository root:
```bash
make
cd examples/basic
../../build/tutrast
```

## Generate grid.cube with C++ mkgrid
```bash
cd /home/sun07ao/xekr/makegrid
/home/sun07ao/xekr/sTuTraSt/build/generate_pes_grid_v10_cpp \
  /path/to/xxx.cif /home/sun07ao/xekr/sTuTraSt/examples/basic/grid.cube \
  --probe Xe --spacing 0.2 --cutoff 12.8 --n_jobs 16 --cell_preexpand auto
```

## Notes
- Outputs are generated in this folder.
- If `RUN_KMC=0` in `input.param`, no `D_ave_<T>.dat` is produced.
