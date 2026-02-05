#ifndef KMC_H
#define KMC_H

#include "types.h"
#include <vector>
#include <string>
#include <random>

class KMC {
public:
    KMC(const std::vector<Coord3D>& basis_sites,
        const std::vector<Process>& processes,
        double temperature);
    
    // Run single kMC simulation
    void run(int n_steps, int n_particles, std::vector<double>& msd);
    
    // Run multiple simulations and compute average diffusion coefficient
    double run_multiple(int n_runs, int n_steps, int n_particles,
                       int print_every, const std::string& output_prefix);
    
private:
    std::vector<Coord3D> basis_sites_;
    std::vector<Process> processes_;
    double temperature_;
    std::mt19937 rng_;
    
    // Compute MSD from trajectory
    double compute_msd(const std::vector<Coord3D>& trajectory);
};

#endif // KMC_H
