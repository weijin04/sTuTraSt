#include "cube_parser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>

double CubeParser::convert_energy(double energy, int from_unit) {
    // Convert to kJ/mol
    switch (from_unit) {
        case 1: // kJ/mol
            return energy;
        case 2: // kcal/mol
            return energy * 4.184;
        case 3: // Ry (Rydberg)
            return energy * 1312.75;
        case 4: // eV
            return energy * 96.4853;
        case 5: // Hartree
            // Keep MATLAB compatibility (legacy factor used in original script).
            return energy * 627.5096;
        case 6: // Kelvin (K)
            return energy * R_GAS / 1000.0;  // 0.0083144621 kJ/(mol*K)
        default:
            std::cerr << "Warning: Unknown energy unit, assuming kJ/mol" << std::endl;
            return energy;
    }
}

bool CubeParser::parse(const std::string& filename, int energy_unit,
                      std::array<int, 3>& ngrid,
                      std::array<double, 3>& grid_size,
                      std::vector<std::vector<double>>& pot_data) {
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return false;
    }
    
    std::string line;
    
    // Skip first two comment lines
    std::getline(file, line);
    std::getline(file, line);
    
    // Read number of atoms and origin
    int n_atoms;
    double ox, oy, oz;
    file >> n_atoms >> ox >> oy >> oz;
    
    // Read grid dimensions and voxel vectors
    int nx, ny, nz;
    double vx1, vy1, vz1;
    double vx2, vy2, vz2;
    double vx3, vy3, vz3;
    
    file >> nx >> vx1 >> vy1 >> vz1;
    file >> ny >> vx2 >> vy2 >> vz2;
    file >> nz >> vx3 >> vy3 >> vz3;
    
    // Consume the trailing newline after grid dimensions
    std::getline(file, line);
    
    ngrid[0] = nx;
    ngrid[1] = ny;
    ngrid[2] = nz;
    
    // Match MATLAB exactly: norm of voxel vector components in Bohr -> Angstrom.
    const double BOHR_TO_ANGSTROM = 0.529177249;
    grid_size[0] = std::sqrt(vx1 * vx1 + vy1 * vy1 + vz1 * vz1) * BOHR_TO_ANGSTROM;
    grid_size[1] = std::sqrt(vx2 * vx2 + vy2 * vy2 + vz2 * vz2) * BOHR_TO_ANGSTROM;
    grid_size[2] = std::sqrt(vx3 * vx3 + vy3 * vy3 + vz3 * vz3) * BOHR_TO_ANGSTROM;
    
    // Skip atom lines
    for (int i = 0; i < std::abs(n_atoms); i++) {
        std::getline(file, line);
    }
    
    // Read raw cube values (linear file order)
    const int total_points = nx * ny * nz;
    std::vector<double> raw_values;
    raw_values.reserve(total_points);

    double value;
    while (file >> value && static_cast<int>(raw_values.size()) < total_points) {
        raw_values.push_back(convert_energy(value, energy_unit));
    }

    if (static_cast<int>(raw_values.size()) < total_points) {
        std::cerr << "Error: insufficient cube data values, got " << raw_values.size()
                  << " expected " << total_points << std::endl;
        return false;
    }

    // Match MATLAB cube2xsfdat.m exactly:
    // shift_cube_data = (cube_data - min(cube_data)) * conv2kJmol
    // We already converted units while reading, so subtract the converted minimum here.
    double min_raw = *std::min_element(raw_values.begin(), raw_values.end());
    for (auto& v : raw_values) {
        v -= min_raw;
    }

    // Match MATLAB cube2xsfdat.m exactly:
    // 1) data_mat(a,b,c) filled with raw values in (a,b,c) loop order (c fastest)
    // 2) pot_data emitted in (c,b,a) order (a fastest)
    std::vector<double> matlab_ordered;
    matlab_ordered.reserve(total_points);
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                const int line = z + nz * (y + ny * x);
                matlab_ordered.push_back(raw_values[line]);
            }
        }
    }

    // Reshape into rows of 6 for compatibility with Grid::initialize
    const int n_rows = (total_points + 5) / 6;
    pot_data.assign(n_rows, {});
    for (int i = 0; i < total_points; i++) {
        pot_data[i / 6].push_back(matlab_ordered[i]);
    }
    
    file.close();
    
    std::cout << "Loaded cube file:" << std::endl;
    std::cout << "  Grid dimensions: " << nx << " x " << ny << " x " << nz << std::endl;
    std::cout << "  Grid size: " << grid_size[0] << " x " 
              << grid_size[1] << " x " << grid_size[2] << " Angstrom" << std::endl;
    
    return true;
}
