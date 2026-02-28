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

## Notes
- Outputs are generated in this folder.
- If `RUN_KMC=0` in `input.param`, no `D_ave_<T>.dat` is produced.
