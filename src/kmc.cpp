#include "kmc.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>

KMC::KMC(const std::vector<Coord3D>& basis_sites,
         const std::vector<int>& basis_tunnel_ids,
         const std::vector<Process>& processes,
         double temperature,
         const std::array<int, 3>& ngrid,
         const std::array<double, 3>& grid_size,
         int per_tunnel,
         const std::array<double, 3>& BT)
    : basis_sites_(basis_sites), basis_tunnel_ids_(basis_tunnel_ids), processes_(processes),
      temperature_(temperature), ngrid_(ngrid), grid_size_(grid_size), per_tunnel_(per_tunnel), BT_(BT) {
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
    // MATLAB kmc_noplot initialization:
    // types is occupancy by basis index; type_trajectory stores [site_id, cumulative displacement].
    const int nbasis = static_cast<int>(basis_sites_.size());
    std::vector<int> types(nbasis, 0);

    int max_tunnel_id = 0;
    for (int tid : basis_tunnel_ids_) max_tunnel_id = std::max(max_tunnel_id, tid);

    // Place n_particles in each tunnel group using MATLAB's deterministic stride rule.
    for (int iT = 1; iT <= max_tunnel_id; iT++) {
        int nbasis_iT = 0;
        int first_idx = -1;
        for (int i = 0; i < nbasis; i++) {
            if (basis_tunnel_ids_[i] == iT) {
                if (first_idx < 0) first_idx = i;
                nbasis_iT++;
            }
        }
        if (nbasis_iT == 0 || n_particles <= 0 || first_idx < 0) continue;

        int first = (nbasis_iT % n_particles) + first_idx;
        int every = nbasis_iT / n_particles;
        if (every <= 0) continue;

        int end_idx = first + nbasis_iT - 1;
        for (int idx = first; idx <= end_idx && idx < nbasis; idx += every) {
            types[idx] = 1;
        }
    }

    int effective_particles = n_particles;
    if (per_tunnel_ == 1 && max_tunnel_id > 0) {
        effective_particles = n_particles * max_tunnel_id;
    }
    (void)effective_particles;  // kept for traceability to MATLAB variable flow

    struct TrajState {
        int site_id_1based;
        Position3D pos;
    };
    std::vector<TrajState> type_trajectory(nbasis);
    for (int i = 0; i < nbasis; i++) {
        type_trajectory[i].site_id_1based = i + 1;
        type_trajectory[i].pos = Position3D(0.0, 0.0, 0.0);
    }

    std::vector<int> li_sites;
    li_sites.reserve(nbasis);
    for (int i = 0; i < nbasis; i++) {
        if (types[i] == 1) li_sites.push_back(i);
    }
    effective_particles = static_cast<int>(li_sites.size());

    traj_times.assign(effective_particles, std::vector<double>(n_steps, 0.0));
    traj_positions.assign(effective_particles, std::vector<Position3D>(n_steps, Position3D()));

    std::uniform_real_distribution<double> uniform01(0.0, 1.0);
    double current_time = 0.0;

    for (int step = 0; step < n_steps; step++) {
        std::vector<int> type1_indices;
        type1_indices.reserve(nbasis);
        std::vector<int> type2_indices;
        type2_indices.reserve(nbasis + effective_particles);

        for (int i = 0; i < nbasis; i++) {
            if (types[i] == 1) {
                type1_indices.push_back(i + 1);  // MATLAB basis index is 1-based
                type2_indices.push_back(i + 1);
            }
        }
        for (int i = 0; i < nbasis; i++) {
            if (types[i] == 0) type2_indices.push_back(i + 1);
        }

        std::vector<int> process_avail;
        process_avail.reserve(processes_.size());
        for (size_t pi = 0; pi < processes_.size(); pi++) {
            const int from1 = processes_[pi].from_basis + 1;
            const int to1 = processes_[pi].to_basis + 1;
            if (std::find(type1_indices.begin(), type1_indices.end(), from1) == type1_indices.end()) continue;
            if (std::find(type2_indices.begin(), type2_indices.end(), to1) == type2_indices.end()) continue;
            process_avail.push_back(static_cast<int>(pi));
        }

        double rate_length = 0.0;
        for (int pi : process_avail) rate_length += processes_[pi].rate;
        if (process_avail.empty() || rate_length <= 0.0) {
            for (int k = 0; k < effective_particles; k++) {
                traj_times[k][step] = current_time;
                traj_positions[k][step] = type_trajectory[li_sites[k]].pos;
            }
            continue;
        }

        const double pick_process = uniform01(rng_) * rate_length;
        double process_sum = 0.0;
        int selected = process_avail.front();
        for (int pi : process_avail) {
            process_sum += processes_[pi].rate;
            if (process_sum >= pick_process) {
                selected = pi;
                break;
            }
        }

        const Process& proc = processes_[selected];
        const int from0 = proc.from_basis;
        const int to0 = proc.to_basis;
        types[from0] = 0;
        types[to0] = 1;

        int coord_li = -1;
        int coord_empty = -1;
        const int from1 = from0 + 1;
        const int to1 = to0 + 1;
        for (int i = 0; i < nbasis; i++) {
            if (type_trajectory[i].site_id_1based == from1) coord_li = i;
            if (type_trajectory[i].site_id_1based == to1) coord_empty = i;
        }

        if (coord_li >= 0 && coord_empty >= 0) {
            std::swap(type_trajectory[coord_li].site_id_1based, type_trajectory[coord_empty].site_id_1based);
            const auto& d = process_displacements_[selected];
            type_trajectory[coord_li].pos.x += d.x;
            type_trajectory[coord_li].pos.y += d.y;
            type_trajectory[coord_li].pos.z += d.z;
            if (coord_li != coord_empty) {
                type_trajectory[coord_empty].pos.x -= d.x;
                type_trajectory[coord_empty].pos.y -= d.y;
                type_trajectory[coord_empty].pos.z -= d.z;
            }
        }

        double r = uniform01(rng_);
        if (r <= 0.0) r = 1e-12;
        current_time += std::log(1.0 / r) / rate_length;

        for (int k = 0; k < effective_particles; k++) {
            traj_times[k][step] = current_time;
            traj_positions[k][step] = type_trajectory[li_sites[k]].pos;
        }
    }
}

std::vector<int> KMC::generate_msd_steps(int n_steps) {
    // Match MATLAB TuTraSt_main.m:
    // msd_steps = []
    // for log_order = 0:ceil(log10(nsteps))-1
    //     msd_steps = [msd_steps 10^log_order*(1:9)]
    // end
    // msd_steps(msd_steps>=nsteps) = []
    std::vector<int> steps;
    if (n_steps <= 1) return steps;
    int max_order = static_cast<int>(std::ceil(std::log10(static_cast<double>(n_steps)))) - 1;
    for (int order = 0; order <= max_order; order++) {
        int base = static_cast<int>(std::pow(10.0, order));
        for (int mult = 1; mult <= 9; mult++) {
            int val = base * mult;
            if (val < n_steps) {
                steps.push_back(val);
            }
        }
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
