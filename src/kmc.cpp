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
         const std::array<double, 3>& grid_size,
         const std::array<double, 3>& BT)
    : basis_sites_(basis_sites), processes_(processes),
      temperature_(temperature), ngrid_(ngrid), grid_size_(grid_size), BT_(BT) {
    std::random_device rd;
    rng_.seed(rd());
    precompute_displacements();
}

void KMC::precompute_displacements() {
    // MATLAB: distances = (...) .* grid_size(1);
    // MATLAB uses grid_size(1) for ALL directions
    process_displacements_.resize(processes_.size());

    for (size_t i = 0; i < processes_.size(); i++) {
        const Process& proc = processes_[i];

        const Coord3D& from_site = basis_sites_[proc.from_basis];
        const Coord3D& to_site = basis_sites_[proc.to_basis];

        double dx_grid = (to_site.x - from_site.x) + ngrid_[0] * proc.cross.i;
        double dy_grid = (to_site.y - from_site.y) + ngrid_[1] * proc.cross.j;
        double dz_grid = (to_site.z - from_site.z) + ngrid_[2] * proc.cross.k;

        // Fix 8: Use grid_size_[0] for all three directions (matching MATLAB)
        process_displacements_[i].x = dx_grid * grid_size_[0];
        process_displacements_[i].y = dy_grid * grid_size_[0];
        process_displacements_[i].z = dz_grid * grid_size_[0];
    }
}

void KMC::run(int n_steps, int n_particles,
              std::vector<std::vector<double>>& traj_times,
              std::vector<std::vector<Position3D>>& traj_positions) {
    // Initialize particles at random basis sites
    std::vector<int> particle_sites(n_particles);
    std::uniform_int_distribution<int> site_dist(0, basis_sites_.size() - 1);

    for (int i = 0; i < n_particles; i++) {
        particle_sites[i] = site_dist(rng_);
    }

    // Particle positions in Angstroms (accumulated displacements)
    std::vector<Position3D> particle_positions(n_particles);

    for (int i = 0; i < n_particles; i++) {
        const Coord3D& site = basis_sites_[particle_sites[i]];
        particle_positions[i].x = site.x * grid_size_[0];
        particle_positions[i].y = site.y * grid_size_[0];
        particle_positions[i].z = site.z * grid_size_[0];
    }

    // Store full trajectories: time + position per particle per step
    // traj_times[p][step], traj_positions[p][step]
    traj_times.resize(n_particles);
    traj_positions.resize(n_particles);
    for (int p = 0; p < n_particles; p++) {
        traj_times[p].resize(n_steps + 1);
        traj_positions[p].resize(n_steps + 1);
        traj_times[p][0] = 0.0;
        traj_positions[p][0] = particle_positions[p];
    }

    std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
    double current_time = 0.0;

    for (int step = 0; step < n_steps; step++) {
        double total_rate_all_particles = 0.0;

        for (int p = 0; p < n_particles; p++) {
            int current_site = particle_sites[p];
            for (size_t i = 0; i < processes_.size(); i++) {
                if (processes_[i].from_basis == current_site && processes_[i].rate > 0) {
                    total_rate_all_particles += processes_[i].rate;
                }
            }
        }

        // Advance time
        if (total_rate_all_particles > 0) {
            double rand_val = uniform_dist(rng_);
            if (rand_val > 0) {
                double dt = std::log(1.0 / rand_val) / total_rate_all_particles;
                current_time += dt;
            }
        }

        // For each particle, perform one kMC step
        for (int p = 0; p < n_particles; p++) {
            int current_site = particle_sites[p];

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

                const Process& proc = processes_[selected_process];
                particle_sites[p] = proc.to_basis;

                particle_positions[p].x += process_displacements_[selected_process].x;
                particle_positions[p].y += process_displacements_[selected_process].y;
                particle_positions[p].z += process_displacements_[selected_process].z;
            }

            traj_times[p][step + 1] = current_time;
            traj_positions[p][step + 1] = particle_positions[p];
        }
    }
}

std::vector<int> KMC::generate_msd_steps(int n_steps) {
    // Generate logarithmic lag steps: 1,2,...,10,20,...,100,200,...,n_steps
    std::vector<int> steps;
    for (int i = 1; i <= std::min(10, n_steps); i++) {
        steps.push_back(i);
    }
    for (int decade = 10; decade < n_steps; decade *= 10) {
        for (int mult = 2; mult <= 10; mult++) {
            int val = decade * mult;
            if (val <= n_steps) {
                steps.push_back(val);
            }
        }
    }
    // Ensure n_steps is included
    if (steps.empty() || steps.back() != n_steps) {
        steps.push_back(n_steps);
    }
    return steps;
}

void KMC::compute_msd_sliding_window(
    const std::vector<std::vector<double>>& traj_times,
    const std::vector<std::vector<Position3D>>& traj_positions,
    const std::vector<int>& msd_steps,
    std::vector<std::array<double, 4>>& msd_out) {
    // MATLAB sliding window MSD:
    // for each lag j:
    //   diff = trajectory[j+1:end] - trajectory[1:end-j]
    //   msd[j] = [mean(diff_time), mean(diff_x^2), mean(diff_y^2), mean(diff_z^2)]

    int n_particles = traj_times.size();
    if (n_particles == 0) return;
    int n_total = traj_times[0].size();  // n_steps + 1

    msd_out.resize(msd_steps.size());

    for (size_t si = 0; si < msd_steps.size(); si++) {
        int lag = msd_steps[si];
        if (lag >= n_total) {
            msd_out[si] = {0.0, 0.0, 0.0, 0.0};
            continue;
        }

        double sum_dt = 0.0, sum_dx2 = 0.0, sum_dy2 = 0.0, sum_dz2 = 0.0;
        int count = 0;

        for (int p = 0; p < n_particles; p++) {
            int n_windows = n_total - lag;
            for (int t = 0; t < n_windows; t++) {
                double dt = traj_times[p][t + lag] - traj_times[p][t];
                double dx = traj_positions[p][t + lag].x - traj_positions[p][t].x;
                double dy = traj_positions[p][t + lag].y - traj_positions[p][t].y;
                double dz = traj_positions[p][t + lag].z - traj_positions[p][t].z;

                sum_dt += dt;
                sum_dx2 += dx * dx;
                sum_dy2 += dy * dy;
                sum_dz2 += dz * dz;
                count++;
            }
        }

        if (count > 0) {
            msd_out[si] = {sum_dt / count, sum_dx2 / count, sum_dy2 / count, sum_dz2 / count};
        } else {
            msd_out[si] = {0.0, 0.0, 0.0, 0.0};
        }
    }
}

std::array<double, 2> KMC::linear_fit(const std::vector<double>& x, const std::vector<double>& y) {
    int n = x.size();
    if (n == 0) return {0.0, 0.0};

    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0;

    for (int i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_xx += x[i] * x[i];
    }

    double denom = n * sum_xx - sum_x * sum_x;
    if (std::abs(denom) < 1e-30) return {0.0, 0.0};

    double slope = (n * sum_xy - sum_x * sum_y) / denom;
    double intercept = (sum_y - slope * sum_x) / n;

    return {slope, intercept};
}

std::array<double, 6> KMC::run_multiple(int n_runs, int n_steps, int n_particles,
                                         int /* print_every */, const std::string& output_prefix) {
    // Box size in Angstroms: abc = ngrid * grid_size
    std::array<double, 3> abc;
    abc[0] = ngrid_[0] * grid_size_[0];
    abc[1] = ngrid_[1] * grid_size_[1];
    abc[2] = ngrid_[2] * grid_size_[2];

    std::vector<int> msd_steps = generate_msd_steps(n_steps);

    std::vector<std::array<double, 3>> diffusion_coefficients(n_runs);

    for (int run = 0; run < n_runs; run++) {
        // Run simulation, get full trajectories
        std::vector<std::vector<double>> traj_times;
        std::vector<std::vector<Position3D>> traj_positions;
        this->run(n_steps, n_particles, traj_times, traj_positions);

        // Compute MSD with sliding window
        std::vector<std::array<double, 4>> msd;
        compute_msd_sliding_window(traj_times, traj_positions, msd_steps, msd);

        // Write MSD to file
        std::string msd_filename = output_prefix + "msd" + std::to_string(run + 1) + ".dat";
        std::ofstream msd_file(msd_filename);
        for (size_t i = 0; i < msd.size(); i++) {
            msd_file << msd[i][0] << " " << msd[i][1] << " " << msd[i][2] << " " << msd[i][3] << std::endl;
        }
        msd_file.close();

        // Fix 7: MATLAB fitting range using box size thresholds and BT
        // istart = first index where msd[dir] > abc[dir]^2
        // iend = first index where msd[dir] > 4 * abc[dir]^2
        // if (iend not found || BT[dir] == 0) D[dir] = 0
        for (int dir = 0; dir < 3; dir++) {
            if (BT_[dir] == 0.0) {
                diffusion_coefficients[run][dir] = 0.0;
                continue;
            }

            double abc2 = abc[dir] * abc[dir];
            int istart = -1;
            int iend = -1;

            for (size_t i = 0; i < msd.size(); i++) {
                if (istart < 0 && msd[i][dir + 1] > abc2) {
                    istart = static_cast<int>(i);
                }
                if (iend < 0 && msd[i][dir + 1] > 4.0 * abc2) {
                    iend = static_cast<int>(i);
                }
            }

            if (istart < 0 || iend < 0 || iend <= istart) {
                diffusion_coefficients[run][dir] = 0.0;
                continue;
            }

            std::vector<double> time_values;
            std::vector<double> msd_values;

            for (int i = istart; i <= iend; i++) {
                time_values.push_back(msd[i][0]);
                msd_values.push_back(msd[i][dir + 1]);
            }

            auto [slope, intercept] = linear_fit(time_values, msd_values);

            // D = 0.5 * slope * 1e-16 (convert Angstrom^2/s to cm^2/s)
            diffusion_coefficients[run][dir] = 0.5 * slope * 1e-16;
        }
    }

    // Calculate average and standard deviation for each direction
    std::array<double, 6> result;

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

    std::cout << "Diffusion coefficients (cm^2/s):" << std::endl;
    std::cout << "  D_x = " << result[0] << " +/- " << result[1] << std::endl;
    std::cout << "  D_y = " << result[2] << " +/- " << result[3] << std::endl;
    std::cout << "  D_z = " << result[4] << " +/- " << result[5] << std::endl;

    return result;
}
