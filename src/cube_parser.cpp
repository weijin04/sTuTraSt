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
    
    // Read potential data (6 values per line)
    int total_points = nx * ny * nz;
    int n_rows = (total_points + 5) / 6;  // Ceiling division
    
    pot_data.clear();
    pot_data.resize(n_rows);
    
    for (int row = 0; row < n_rows; row++) {
        std::getline(file, line);
        std::istringstream iss(line);
        double value;
        
        while (iss >> value) {
            // Convert energy units
            double converted = convert_energy(value, energy_unit);
            pot_data[row].push_back(converted);
        }
    }
    
    file.close();
    
    std::cout << "Read " << pot_data.size() << " rows from cube file" << std::endl;
    
    // IMPORTANT: Reorder pot_data to match MATLAB cube2xsfdat behavior
    // MATLAB reads cube file in x-y-z order (into data_mat), then reorders to z-y-x (into pot_data)
    // We need to do the same reordering
    std::vector<std::vector<double>> reordered_data;
    reordered_data.resize(n_rows);
    for (int i = 0; i < n_rows; i++) {
        reordered_data[i].reserve(6);
    }
    
    int new_ind = 0;
    int error_count = 0;
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                // Original cube file has data in x-fastest order: for z, for y, for x
                int orig_ind = z * (nx * ny) + y * nx + x;
                int orig_row = orig_ind / 6;
                int orig_col = orig_ind % 6;
                
                int new_row = new_ind / 6;
                int new_col = new_ind % 6;
                
                if (orig_row >= (int)pot_data.size()) {
                    if (error_count < 5) {
                        std::cerr << "Error: orig_row " << orig_row << " >= pot_data.size() " << pot_data.size() << std::endl;
                    }
                    error_count++;
                    continue;
                }
                if (orig_col >= (int)pot_data[orig_row].size()) {
                    if (error_count < 5) {
                        std::cerr << "Error: orig_col " << orig_col << " >= pot_data[" << orig_row << "].size() " << pot_data[orig_row].size() << std::endl;
                    }
                    error_count++;
                    continue;
                }
                
                reordered_data[new_row].push_back(pot_data[orig_row][orig_col]);
                new_ind++;
            }
        }
    }
    
    if (error_count > 0) {
        std::cerr << "Total " << error_count << " reordering errors" << std::endl;
        return false;
    }
    
    pot_data = reordered_data;
    
    std::cout << "Loaded cube file:" << std::endl;
    std::cout << "  Grid dimensions: " << nx << " x " << ny << " x " << nz << std::endl;
    std::cout << "  Grid size: " << grid_size[0] << " x " 
              << grid_size[1] << " x " << grid_size[2] << " Angstrom" << std::endl;
    
    return true;
}
