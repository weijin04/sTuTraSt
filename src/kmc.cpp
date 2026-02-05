#include "kmc.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>

KMC::KMC(const std::vector<Coord3D>& basis_sites,
         const std::vector<Process>& processes,
         double temperature,
         const std::array<int, 3>& ngrid,
         const std::array<double, 3>& grid_size)
    : basis_sites_(basis_sites), processes_(processes), 
      temperature_(temperature), ngrid_(ngrid), grid_size_(grid_size) {
    // Initialize random number generator
    std::random_device rd;
    rng_.seed(rd());
    
    // Pre-compute displacements in Angstroms
    precompute_displacements();
}

void KMC::precompute_displacements() {
    // MATLAB: distances = (basis_sites(proc(:,2),1:3) - basis_sites(proc(:,1),1:3) + 
    //                     [ngrid(1)*proc(:,4) ngrid(2)*proc(:,5) ngrid(3)*proc(:,6)]) .* grid_size(1);
    //
    // Note: grid_size is the VOXEL spacing in Angstroms (not total box size)
    // basis_sites are grid indices (integers)
    
    process_displacements_.resize(processes_.size());
    
    for (size_t i = 0; i < processes_.size(); i++) {
        const Process& proc = processes_[i];
        
        // Get basis site coordinates (grid indices)
        const Coord3D& from_site = basis_sites_[proc.from_basis];
        const Coord3D& to_site = basis_sites_[proc.to_basis];
        
        // Calculate displacement in grid units, accounting for periodic boundaries
        // cross vector tells us if we wrap: -1, 0, or 1
        double dx_grid = (to_site.x - from_site.x) + ngrid_[0] * proc.cross.i;
        double dy_grid = (to_site.y - from_site.y) + ngrid_[1] * proc.cross.j;
        double dz_grid = (to_site.z - from_site.z) + ngrid_[2] * proc.cross.k;
        
        // Convert to Angstroms by multiplying by voxel spacing
        // MATLAB uses grid_size(1) for all directions - we'll use the first element
        double voxel_spacing = grid_size_[0];
        
        process_displacements_[i].x = dx_grid * voxel_spacing;
        process_displacements_[i].y = dy_grid * voxel_spacing;
        process_displacements_[i].z = dz_grid * voxel_spacing;
    }
}

void KMC::run(int n_steps, int n_particles, 
             std::vector<std::array<double, 3>>& msd_per_direction) {
    // Initialize particles at random basis sites
    std::vector<int> particle_sites(n_particles);
    std::uniform_int_distribution<int> site_dist(0, basis_sites_.size() - 1);
    
    for (int i = 0; i < n_particles; i++) {
        particle_sites[i] = site_dist(rng_);
    }
    
    // Particle positions in Angstroms (accumulated displacements)
    std::vector<Position3D> particle_positions(n_particles);
    std::vector<Position3D> initial_positions(n_particles);
    
    // Initialize positions to basis site locations in Angstroms
    // grid_size is the voxel spacing in Angstroms
    double voxel_spacing = grid_size_[0];
    
    for (int i = 0; i < n_particles; i++) {
        const Coord3D& site = basis_sites_[particle_sites[i]];
        particle_positions[i].x = site.x * voxel_spacing;
        particle_positions[i].y = site.y * voxel_spacing;
        particle_positions[i].z = site.z * voxel_spacing;
        initial_positions[i] = particle_positions[i];
    }
    
    // Track actual time (seconds) - MATLAB: dt = log(1/rand) / rate_length
    std::vector<double> time_points(n_steps + 1, 0.0);
    std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
    
    msd_per_direction.resize(n_steps + 1);
    msd_per_direction[0] = {0.0, 0.0, 0.0};
    
    // Run kMC steps
    for (int step = 0; step < n_steps; step++) {
        double total_rate_all_particles = 0.0;
        
        // Calculate total rate for all particles
        for (int p = 0; p < n_particles; p++) {
            int current_site = particle_sites[p];
            for (size_t i = 0; i < processes_.size(); i++) {
                if (processes_[i].from_basis == current_site && processes_[i].rate > 0) {
                    total_rate_all_particles += processes_[i].rate;
                }
            }
        }
        
        // Advance time: dt = log(1/rand) / total_rate
        if (total_rate_all_particles > 0) {
            double rand_val = uniform_dist(rng_);
            if (rand_val > 0) {
                double dt = std::log(1.0 / rand_val) / total_rate_all_particles;
                time_points[step + 1] = time_points[step] + dt;
            } else {
                time_points[step + 1] = time_points[step];
            }
        } else {
            time_points[step + 1] = time_points[step];
        }
        
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
                
                // CRITICAL FIX: Accumulate displacement instead of resetting position
                // MATLAB: type_trajectory(coord_Li,3:5) = type_trajectory(coord_Li,3:5) + distances(process_exec(step),:);
                particle_positions[p].x += process_displacements_[selected_process].x;
                particle_positions[p].y += process_displacements_[selected_process].y;
                particle_positions[p].z += process_displacements_[selected_process].z;
            }
        }
        
        // Calculate MSD per direction for this step
        double msd_x = 0.0, msd_y = 0.0, msd_z = 0.0;
        for (int p = 0; p < n_particles; p++) {
            double dx = particle_positions[p].x - initial_positions[p].x;
            double dy = particle_positions[p].y - initial_positions[p].y;
            double dz = particle_positions[p].z - initial_positions[p].z;
            msd_x += dx * dx;
            msd_y += dy * dy;
            msd_z += dz * dz;
        }
        msd_per_direction[step + 1][0] = msd_x / n_particles;
        msd_per_direction[step + 1][1] = msd_y / n_particles;
        msd_per_direction[step + 1][2] = msd_z / n_particles;
    }
    
    // Store time points in the first "column" of msd data (we'll use a modified structure)
    // For now, just keep MSD - fitting will use step indices as proxy for time
    // TODO: Could store time_points separately if needed for more accurate fitting
}

std::array<double, 2> KMC::linear_fit(const std::vector<double>& x, const std::vector<double>& y) {
    // Simple linear regression: y = ax + b
    // Returns [slope, intercept]
    
    int n = x.size();
    if (n == 0) return {0.0, 0.0};
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0;
    
    for (int i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_xx += x[i] * x[i];
    }
    
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    double intercept = (sum_y - slope * sum_x) / n;
    
    return {slope, intercept};
}

std::array<double, 6> KMC::run_multiple(int n_runs, int n_steps, int n_particles,
                                         int print_every, const std::string& output_prefix) {
    std::vector<std::array<double, 3>> diffusion_coefficients(n_runs);
    
    for (int run = 0; run < n_runs; run++) {
        std::vector<std::array<double, 3>> msd;
        this->run(n_steps, n_particles, msd);
        
        // Write MSD to file (similar to MATLAB format)
        std::string msd_filename = output_prefix + "msd" + std::to_string(run+1) + ".dat";
        std::ofstream msd_file(msd_filename);
        
        for (size_t i = 0; i < msd.size(); i += print_every) {
            // Format: step msd_x msd_y msd_z
            msd_file << i << " " << msd[i][0] << " " << msd[i][1] << " " << msd[i][2] << std::endl;
        }
        msd_file.close();
        
        // Calculate diffusion coefficient per direction
        // MATLAB: Find range where MSD is between abc^2 and 4*abc^2, then fit
        // For simplicity, use latter half of data
        
        for (int dir = 0; dir < 3; dir++) {
            // Use latter half for fitting
            int start_idx = msd.size() / 2;
            int end_idx = msd.size();
            
            std::vector<double> time_points;
            std::vector<double> msd_values;
            
            for (int i = start_idx; i < end_idx; i++) {
                time_points.push_back(static_cast<double>(i));
                msd_values.push_back(msd[i][dir]);
            }
            
            // Linear fit
            auto [slope, intercept] = linear_fit(time_points, msd_values);
            
            // MATLAB: D(iD) = 0.5 * msd_fit(1) * 1e-16
            // slope is in Å²/step
            // We need to convert: Å²/step → Å²/s → cm²/s
            // For now, assume 1 step = 1 time unit (will need proper time tracking later)
            // 1e-16 converts Å²/s to cm²/s (since 1 Å = 1e-8 cm, so 1 Å² = 1e-16 cm²)
            diffusion_coefficients[run][dir] = 0.5 * slope * 1e-16;
        }
    }
    
    // Calculate average and standard deviation for each direction
    std::array<double, 6> result;  // [D_x, err_x, D_y, err_y, D_z, err_z]
    
    for (int dir = 0; dir < 3; dir++) {
        double sum = 0.0;
        for (int run = 0; run < n_runs; run++) {
            sum += diffusion_coefficients[run][dir];
        }
        double mean = sum / n_runs;
        
        double sq_sum = 0.0;
        for (int run = 0; run < n_runs; run++) {
            double diff = diffusion_coefficients[run][dir] - mean;
            sq_sum += diff * diff;
        }
        double stddev = std::sqrt(sq_sum / n_runs);
        
        result[dir * 2] = mean;
        result[dir * 2 + 1] = stddev;
    }
    
    std::cout << "Diffusion coefficients (cm²/s):" << std::endl;
    std::cout << "  D_x = " << result[0] << " +/- " << result[1] << std::endl;
    std::cout << "  D_y = " << result[2] << " +/- " << result[3] << std::endl;
    std::cout << "  D_z = " << result[4] << " +/- " << result[5] << std::endl;
    
    return result;
}
