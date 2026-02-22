#ifndef KMC_H
#define KMC_H

#include "types.h"
#include <vector>
#include <string>
#include <random>
#include <array>

// Position in Angstroms (not grid indices)
struct Position3D {
    double x, y, z;
    Position3D() : x(0), y(0), z(0) {}
    Position3D(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

class KMC {
public:
    KMC(const std::vector<Coord3D>& basis_sites,
        const std::vector<Process>& processes,
        double temperature,
        const std::array<int, 3>& ngrid,
        const std::array<double, 3>& grid_size,
        const std::array<double, 3>& BT = {0.0, 0.0, 0.0});

    // Run single kMC simulation, returns trajectory data
    void run(int n_steps, int n_particles,
             std::vector<std::vector<double>>& traj_times,
             std::vector<std::vector<Position3D>>& traj_positions);

    // Run multiple simulations and compute average diffusion coefficient per direction
    // Returns [D_x, err_x, D_y, err_y, D_z, err_z]
    std::array<double, 6> run_multiple(int n_runs, int n_steps, int n_particles,
                                        int print_every, const std::string& output_prefix);

private:
    std::vector<Coord3D> basis_sites_;
    std::vector<Process> processes_;
    std::vector<Position3D> process_displacements_;
    double temperature_;
    std::mt19937 rng_;
    std::array<int, 3> ngrid_;
    std::array<double, 3> grid_size_;
    std::array<double, 3> BT_;

    void precompute_displacements();
    std::array<double, 2> linear_fit(const std::vector<double>& x, const std::vector<double>& y);

    // Generate logarithmic MSD step lags: 1,2,...,10,20,...,100,200,...,n_steps
    std::vector<int> generate_msd_steps(int n_steps);

    // Compute MSD using sliding window from trajectories
    void compute_msd_sliding_window(
        const std::vector<std::vector<double>>& traj_times,
        const std::vector<std::vector<Position3D>>& traj_positions,
        const std::vector<int>& msd_steps,
        std::vector<std::array<double, 4>>& msd_out);  // [mean_time, mean_dx2, mean_dy2, mean_dz2]
};

#endif // KMC_H
