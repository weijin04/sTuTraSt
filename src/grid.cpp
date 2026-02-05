#include "grid.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>

Grid::Grid(const std::array<int, 3>& dimensions) 
    : dims_(dimensions), min_energy_(0.0), max_energy_(0.0),
      level_min_(0), level_max_(0), level_scale_(1.0) {
    
    size_t total_size = dims_[0] * dims_[1] * dims_[2];
    
    E_matrix_.resize(total_size, 0.0);
    level_matrix_.resize(total_size, 0);
    minID_L_matrix_.resize(total_size, 0);
    minID_C_matrix_.resize(total_size, 0);
    TS_matrix_.resize(total_size, 0);
    cross_i_matrix_.resize(total_size, 0);
    cross_j_matrix_.resize(total_size, 0);
    cross_k_matrix_.resize(total_size, 0);
}

void Grid::initialize(const std::vector<std::vector<double>>& pot_data,
                     double energy_step, double energy_cutoff) {
    // Convert pot_data to E_matrix
    int ind = 0;
    for (int z = 0; z < dims_[2]; z++) {
        for (int y = 0; y < dims_[1]; y++) {
            for (int x = 0; x < dims_[0]; x++) {
                int row_ind = ind / 6;
                int col_ind = ind % 6;
                
                double energy = pot_data[row_ind][col_ind];
                
                // Replace extreme values
                if (std::isinf(energy) || energy > 10 * energy_cutoff) {
                    energy = 10 * energy_cutoff;
                }
                
                size_t idx = index(x, y, z);
                E_matrix_[idx] = energy;
                
                ind++;
            }
        }
    }
    
    // Find min and max energy
    min_energy_ = *std::min_element(E_matrix_.begin(), E_matrix_.end());
    max_energy_ = *std::max_element(E_matrix_.begin(), E_matrix_.end());
    
    // Replace max values
    for (size_t i = 0; i < E_matrix_.size(); i++) {
        if (E_matrix_[i] == max_energy_) {
            E_matrix_[i] = 10 * energy_cutoff;
        }
    }
    
    max_energy_ = *std::max_element(E_matrix_.begin(), E_matrix_.end());
    
    // Calculate level scaling
    double nlevel = (max_energy_ - min_energy_) / energy_step;
    level_scale_ = nlevel / (max_energy_ - min_energy_);
    
    int level_stop = static_cast<int>(std::ceil(energy_cutoff / energy_step));
    
    // Convert energies to levels
    for (size_t i = 0; i < E_matrix_.size(); i++) {
        double pot_value_shifted = E_matrix_[i] - min_energy_;
        double pot_value_scaled = pot_value_shifted * level_scale_ + 1.0 / (level_stop * 1000.0);
        level_matrix_[i] = static_cast<int>(std::ceil(pot_value_scaled));
    }
    
    level_min_ = *std::min_element(level_matrix_.begin(), level_matrix_.end());
    level_max_ = *std::max_element(level_matrix_.begin(), level_matrix_.end());
    
    std::cout << "Grid initialized:" << std::endl;
    std::cout << "  Energy range: " << min_energy_ << " - " << max_energy_ << " kJ/mol" << std::endl;
    std::cout << "  Level range: " << level_min_ << " - " << level_max_ << std::endl;
}

double Grid::energy_at(int x, int y, int z) const {
    return E_matrix_[index(x, y, z)];
}

int Grid::level_at(int x, int y, int z) const {
    return level_matrix_[index(x, y, z)];
}

int& Grid::minID_L(int x, int y, int z) {
    return minID_L_matrix_[index(x, y, z)];
}

int Grid::minID_L(int x, int y, int z) const {
    return minID_L_matrix_[index(x, y, z)];
}

int& Grid::minID_C(int x, int y, int z) {
    return minID_C_matrix_[index(x, y, z)];
}

int Grid::minID_C(int x, int y, int z) const {
    return minID_C_matrix_[index(x, y, z)];
}

int& Grid::ts_matrix(int x, int y, int z) {
    return TS_matrix_[index(x, y, z)];
}

int Grid::ts_matrix(int x, int y, int z) const {
    return TS_matrix_[index(x, y, z)];
}

int& Grid::cross_i(int x, int y, int z) {
    return cross_i_matrix_[index(x, y, z)];
}

int Grid::cross_i(int x, int y, int z) const {
    return cross_i_matrix_[index(x, y, z)];
}

int& Grid::cross_j(int x, int y, int z) {
    return cross_j_matrix_[index(x, y, z)];
}

int Grid::cross_j(int x, int y, int z) const {
    return cross_j_matrix_[index(x, y, z)];
}

int& Grid::cross_k(int x, int y, int z) {
    return cross_k_matrix_[index(x, y, z)];
}

int Grid::cross_k(int x, int y, int z) const {
    return cross_k_matrix_[index(x, y, z)];
}
