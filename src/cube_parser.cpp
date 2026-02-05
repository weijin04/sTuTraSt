#include "cube_parser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>

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
            return energy * 2625.5;
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
    
    ngrid[0] = nx;
    ngrid[1] = ny;
    ngrid[2] = nz;
    
    // Calculate grid sizes (in Angstroms, convert from Bohr)
    const double BOHR_TO_ANGSTROM = 0.529177;
    grid_size[0] = std::abs(vx1 * nx) * BOHR_TO_ANGSTROM;
    grid_size[1] = std::abs(vy2 * ny) * BOHR_TO_ANGSTROM;
    grid_size[2] = std::abs(vz3 * nz) * BOHR_TO_ANGSTROM;
    
    // Skip atom lines
    for (int i = 0; i < std::abs(n_atoms); i++) {
        std::getline(file, line);
    }
    
    // Read potential data (variable values per line)
    int total_points = nx * ny * nz;
    
    pot_data.clear();
    pot_data.resize(1);  // Start with one row
    
    double value;
    int count = 0;
    while (file >> value && count < total_points) {
        // Convert energy units
        double converted = convert_energy(value, energy_unit);
        pot_data[0].push_back(converted);
        count++;
    }
    
    // Reshape into rows of 6 for compatibility with grid.cpp
    int n_rows = (total_points + 5) / 6;
    std::vector<std::vector<double>> reshaped_data;
    reshaped_data.resize(n_rows);
    
    for (int i = 0; i < total_points; i++) {
        int row = i / 6;
        reshaped_data[row].push_back(pot_data[0][i]);
    }
    
    pot_data = reshaped_data;
    
    file.close();
    
    std::cout << "Loaded cube file:" << std::endl;
    std::cout << "  Grid dimensions: " << nx << " x " << ny << " x " << nz << std::endl;
    std::cout << "  Grid size: " << grid_size[0] << " x " 
              << grid_size[1] << " x " << grid_size[2] << " Angstrom" << std::endl;
    
    return true;
}
