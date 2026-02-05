#include "kmc.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>

KMC::KMC(const std::vector<Coord3D>& basis_sites,
         const std::vector<Process>& processes,
         double temperature)
    : basis_sites_(basis_sites), processes_(processes), 
      temperature_(temperature) {
    // Initialize random number generator
    std::random_device rd;
    rng_.seed(rd());
}

void KMC::run(int n_steps, int n_particles, std::vector<double>& msd) {
    // Initialize particles at random basis sites
    std::vector<int> particle_sites(n_particles);
    std::uniform_int_distribution<int> site_dist(0, basis_sites_.size() - 1);
    
    for (int i = 0; i < n_particles; i++) {
        particle_sites[i] = site_dist(rng_);
    }
    
    std::vector<Coord3D> particle_positions(n_particles);
    std::vector<Coord3D> initial_positions(n_particles);
    
    for (int i = 0; i < n_particles; i++) {
        particle_positions[i] = basis_sites_[particle_sites[i]];
        initial_positions[i] = particle_positions[i];
    }
    
    msd.resize(n_steps, 0.0);
    
    // Run kMC steps
    for (int step = 0; step < n_steps; step++) {
        // For each particle, perform one kMC step
        for (int p = 0; p < n_particles; p++) {
            int current_site = particle_sites[p];
            
            // Find all possible processes from current site
            std::vector<int> valid_processes;
            std::vector<double> rates;
            double total_rate = 0.0;
            
            for (size_t i = 0; i < processes_.size(); i++) {
                if (processes_[i].from_basis == current_site && processes_[i].rate > 0) {
                    valid_processes.push_back(i);
                    rates.push_back(processes_[i].rate);
                    total_rate += processes_[i].rate;
                }
            }
            
            if (total_rate > 0 && !valid_processes.empty()) {
                // Select process based on rates
                std::uniform_real_distribution<double> rate_dist(0.0, total_rate);
                double rand_rate = rate_dist(rng_);
                
                double cumulative = 0.0;
                int selected_process = valid_processes[0];
                
                for (size_t i = 0; i < valid_processes.size(); i++) {
                    cumulative += rates[i];
                    if (rand_rate <= cumulative) {
                        selected_process = valid_processes[i];
                        break;
                    }
                }
                
                // Execute transition
                const Process& proc = processes_[selected_process];
                particle_sites[p] = proc.to_basis;
                
                // Update position with periodic boundary crossing
                Coord3D new_pos = basis_sites_[proc.to_basis];
                new_pos.x += proc.cross.i;
                new_pos.y += proc.cross.j;
                new_pos.z += proc.cross.k;
                particle_positions[p] = new_pos;
            }
        }
        
        // Calculate MSD for this step
        double step_msd = 0.0;
        for (int p = 0; p < n_particles; p++) {
            int dx = particle_positions[p].x - initial_positions[p].x;
            int dy = particle_positions[p].y - initial_positions[p].y;
            int dz = particle_positions[p].z - initial_positions[p].z;
            step_msd += dx*dx + dy*dy + dz*dz;
        }
        msd[step] = step_msd / n_particles;
    }
}

double KMC::run_multiple(int n_runs, int n_steps, int n_particles,
                        int print_every, const std::string& output_prefix) {
    std::vector<double> diffusion_coefficients;
    
    for (int run = 0; run < n_runs; run++) {
        std::vector<double> msd;
        this->run(n_steps, n_particles, msd);
        
        // Write MSD to file
        std::string msd_filename = output_prefix + "msd" + std::to_string(run+1) + ".dat";
        std::ofstream msd_file(msd_filename);
        
        for (size_t i = 0; i < msd.size(); i += print_every) {
            msd_file << i << " " << msd[i] << std::endl;
        }
        msd_file.close();
        
        // Calculate diffusion coefficient from MSD slope
        // D = MSD / (6 * t) for 3D diffusion
        // Use latter half of MSD for better statistics
        int start_idx = msd.size() / 2;
        double sum_msd = 0.0;
        int count = 0;
        
        for (size_t i = start_idx; i < msd.size(); i++) {
            sum_msd += msd[i];
            count++;
        }
        
        double avg_msd = sum_msd / count;
        double avg_time = (start_idx + msd.size() - 1) / 2.0;
        double D = avg_msd / (6.0 * avg_time);
        
        diffusion_coefficients.push_back(D);
    }
    
    // Calculate average and standard deviation
    double mean = std::accumulate(diffusion_coefficients.begin(), 
                                  diffusion_coefficients.end(), 0.0) / n_runs;
    
    double sq_sum = 0.0;
    for (double d : diffusion_coefficients) {
        sq_sum += (d - mean) * (d - mean);
    }
    double stddev = std::sqrt(sq_sum / n_runs);
    
    std::cout << "Diffusion coefficient: " << mean << " +/- " << stddev << std::endl;
    
    return mean;
}

double KMC::compute_msd(const std::vector<Coord3D>& trajectory) {
    if (trajectory.size() < 2) return 0.0;
    
    const Coord3D& initial = trajectory[0];
    const Coord3D& final = trajectory.back();
    
    int dx = final.x - initial.x;
    int dy = final.y - initial.y;
    int dz = final.z - initial.z;
    
    return dx*dx + dy*dy + dz*dz;
}
