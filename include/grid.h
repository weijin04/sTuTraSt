#ifndef GRID_H
#define GRID_H

#include "types.h"
#include <vector>
#include <array>
#include <memory>
#include <cstdint>

class Grid {
public:
    Grid(const std::array<int, 3>& dimensions);
    
    // Initialize from potential data
    void initialize(const std::vector<double>& pot_data,
                   double energy_step, double energy_cutoff);
    
    // Getters
    int nx() const { return dims_[0]; }
    int ny() const { return dims_[1]; }
    int nz() const { return dims_[2]; }
    
    double energy_at(int x, int y, int z) const;
    int level_at(int x, int y, int z) const;
    
    int16_t& minID_L(int x, int y, int z);
    int16_t minID_L(int x, int y, int z) const;
    
    int& minID_C(int x, int y, int z);
    int minID_C(int x, int y, int z) const;
    
    uint8_t& ts_matrix(int x, int y, int z);
    uint8_t ts_matrix(int x, int y, int z) const;

    uint8_t& ts_ever(int x, int y, int z);
    uint8_t ts_ever(int x, int y, int z) const;

    int8_t& cross_i(int x, int y, int z);
    int8_t cross_i(int x, int y, int z) const;

    int8_t& cross_j(int x, int y, int z);
    int8_t cross_j(int x, int y, int z) const;

    int8_t& cross_k(int x, int y, int z);
    int8_t cross_k(int x, int y, int z) const;
    
    double min_energy() const { return min_energy_; }
    double max_energy() const { return max_energy_; }
    int min_level() const { return level_min_; }
    int max_level() const { return level_max_; }
    
    double level_scale() const { return level_scale_; }
    
private:
    std::array<int, 3> dims_;
    
    // 3D matrices
    std::vector<double> E_matrix_;        // Energy at each point
    std::vector<int> level_matrix_;       // Level at each point
    std::vector<int16_t> minID_L_matrix_;     // Level ID (values fit in int16)
    std::vector<int> minID_C_matrix_;     // Cluster ID
    std::vector<uint8_t> TS_matrix_;          // TS marker (0 or 1)
    std::vector<uint8_t> TS_ever_matrix_;      // Sticky TS marker (0 or 1)
    std::vector<int8_t> cross_i_matrix_;     // Cross vectors (small range)
    std::vector<int8_t> cross_j_matrix_;
    std::vector<int8_t> cross_k_matrix_;
    
    double min_energy_;
    double max_energy_;
    int level_min_;
    int level_max_;
    double level_scale_;
    
    // Helper to convert 3D coordinates to 1D index
    inline size_t index(int x, int y, int z) const {
        return x + dims_[0] * (y + dims_[1] * z);
    }
};

#endif // GRID_H
