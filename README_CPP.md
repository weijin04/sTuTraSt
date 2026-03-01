# TuTraSt - C++ Implementation

C++ rewrite of the TuTraSt (TUnnel and TRAnsition STate search) algorithm for Linux systems. This implementation provides significantly faster execution while maintaining equivalent output to the original MATLAB version.

## About TuTraSt

TuTraSt is a novel algorithm to predict self-diffusion of a mobile guest particle in a crystalline material. It detects the energies at which diffusion paths are formed, allowing for easy identification of diffusive systems, and partitions the potential energy field into energy basins and transition states. This enables transition state theory based analysis for fast prediction of diffusion coefficients with an automated multiscale modeling approach.

## Original Reference

Based on the MATLAB implementation presented in:
- [1] Amber Mace, Senja Barthel and Berend Smit, J. Chem. Theory Comput. 2019, 15, 4, 2127-2141

## System Requirements

- **Operating System**: Linux (Ubuntu, Debian, Fedora, CentOS, etc.)
- **Compiler**: GCC 7.0+ or Clang 6.0+ with C++17 support
- **CMake**: Version 3.10 or higher
- **Memory**: Depends on grid size (typically 1-8 GB)

## Installation

### On Ubuntu/Debian:

```bash
# Install required packages
sudo apt-get update
sudo apt-get install build-essential cmake

# Clone the repository
git clone https://github.com/weijin04/sTuTraSt.git
cd sTuTraSt

# Build the project
./build.sh

# Alternative: use make (which calls build.sh)
make

# Optionally install system-wide
sudo make install
```

### On Fedora/CentOS/RHEL:

```bash
# Install required packages
sudo dnf install gcc-c++ cmake make

# Clone and build (same as Ubuntu)
git clone https://github.com/weijin04/sTuTraSt.git
cd sTuTraSt
./build.sh
```

## Usage

### Input Files

The program requires two input files in the directory where you run it:

1. **grid.cube**: Potential energy grid file in cube format
   - Header lines with system information
   - Grid dimensions and voxel vectors
   - Potential energy values (6 values per line)

2. **input.param**: User-defined parameters (one per line)
   ```
   1              # Energy unit: 1=kJ/mol, 2=kcal/mol, 3=Ry, 4=eV, 5=Hartree
   1              # Number of temperatures
   1000           # Temperature in K
   1              # Run kMC? 0=no, 1=yes
   0              # Plot MSD? 0=no, 1=yes (not applicable in C++ version)
   250000         # Number of kMC steps
   100            # Print trajectory every n steps
   10             # Number of kMC simulations for averaging
   1              # Number of particles for kMC
   1              # Particles given: 0=in total, 1=per tunnel
   6.941e-3       # Particle mass (kg/mol)
   2              # Energy step (kJ/mol)
   100            # Energy cutoff (kJ/mol)
   ```

### Running the Program

```bash
# Recommended: run the bundled example
cd examples/basic
../../build/tutrast

# Or if installed system-wide
tutrast
```

The program will:
1. Read and parse input files
2. Process the potential energy grid
3. Perform cluster growth and transition state detection
4. Organize tunnels and generate processes
5. Calculate transition rates for each temperature
6. Optionally run kinetic Monte Carlo simulations
7. Write output files

## v10 Grid Generator (C++)

Build also provides:
- `build/generate_pes_grid_v10_cpp`

Example:
```bash
cd /home/sun07ao/xekr/makegrid
/home/sun07ao/xekr/sTuTraSt/build/generate_pes_grid_v10_cpp \
  /path/to/xxx.cif /path/to/grid.cube \
  --probe Xe --spacing 0.2 --cutoff 12.8 --n_jobs 16 --cell_preexpand auto
```

### Output Files

The program generates several output files:

- **basis.dat**: Basis site coordinates and tunnel assignments
- **processes_[T].dat**: Transition processes and rates at temperature T (K)
- **tunnel_info.out**: Detailed tunnel information (dimensions, clusters, TS groups)
- **TS_data.out**: Transition state coordinates and energies
- **BT.dat**: Breakthrough energies for A/B/C directions
- **Evol_[T].dat**: Energy volume evolution
- **D_ave_[T].dat**: Average diffusion coefficient at temperature T (if kMC enabled)
- **T[T]_msd[run].dat**: Mean square displacement curves (if kMC enabled)

## Performance

The C++ implementation offers significant performance improvements over the MATLAB version:

- **Speed**: Typically 5-20x faster depending on grid size
- **Memory**: More efficient memory usage with optimized data structures
- **Scaling**: Better performance on large grids (>100³ points)

## Differences from MATLAB Version

This C++ implementation maintains algorithmic equivalence with the MATLAB version while making some practical adjustments:

1. **Numerical precision**: Results may differ by negligible amounts (< 1e-10) due to floating-point arithmetic
2. **No GUI plotting**: The plot_msd option is not supported; MSD data is written to files instead
3. **No MATLAB-specific features**: No .mat checkpoint files (not needed due to faster execution)
4. **Optimized algorithms**: Some internal algorithms are optimized for C++ while maintaining output equivalence

## Building from Source

### Advanced Build Options

```bash
# Debug build for development
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build with optimizations (default)
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Install to custom location
cmake -DCMAKE_INSTALL_PREFIX=/custom/path ..
make install
```

### Compiler Options

The default build uses aggressive optimizations (`-O3`) for maximum performance. You can customize compiler flags:

```bash
cmake -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native" ..
make
```

## Troubleshooting

### Common Issues

1. **Build failures or "No makefile found" errors**: The build script automatically handles corrupted build directories. If you encounter persistent issues, try:
   ```bash
   ./build.sh clean
   ./build.sh
   ```

2. **CMake version error**: Ensure CMake 3.10+ is installed
   ```bash
   cmake --version
   ```

3. **Compiler not found**: Install GCC or Clang
   ```bash
   g++ --version
   ```

4. **Input file errors**: Check that grid.cube and input.param are in the current directory
```bash
ls -l grid.cube input.param
```

5. **Memory errors**: For very large grids, increase system swap space or reduce grid resolution

## Citation

If you use this software in your research, please cite the original TuTraSt paper:

```bibtex
@article{mace2019tutrast,
  title={Tunnel and transition state search algorithm for the prediction of diffusion kinetics in crystalline materials},
  author={Mace, Amber and Barthel, Senja and Smit, Berend},
  journal={Journal of Chemical Theory and Computation},
  volume={15},
  number={4},
  pages={2127--2141},
  year={2019},
  publisher={ACS Publications}
}
```

## License

This C++ implementation maintains the same license as the original MATLAB code. See LICENSE file for details.

## Contact

For questions about this C++ implementation:
- GitHub Issues: https://github.com/weijin04/sTuTraSt/issues

For questions about the original algorithm:
- Amber Mace (amber.mace@kemi.uu.se)

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

## Acknowledgments

- Original MATLAB implementation by Amber Mace, Senja Barthel, and Berend Smit
- C++ rewrite optimized for Linux systems
