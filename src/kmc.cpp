#include "kmc.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <limits>

namespace {

struct LoggedStep {
    int particle_ids[2] = {-1, -1};
    int particle_signs[2] = {0, 0};
    int process_index = -1;
};

struct AxisSums {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline void apply_axis_delta(double delta, double& diff_component, double& squared_sum) {
    if (delta == 0.0) return;
    squared_sum -= diff_component * diff_component;
    diff_component += delta;
    squared_sum += diff_component * diff_component;
}

inline void apply_position_delta(const Position3D& delta, Position3D& diff, AxisSums& squared_sums, double sign) {
    apply_axis_delta(sign * delta.x, diff.x, squared_sums.x);
    apply_axis_delta(sign * delta.y, diff.y, squared_sums.y);
    apply_axis_delta(sign * delta.z, diff.z, squared_sums.z);
}

class FenwickTree {
public:
    explicit FenwickTree(size_t n)
        : tree_(n + 1, 0.0), total_(0.0) {}

    void add(size_t index, double delta) {
        if (delta == 0.0) return;
        total_ += delta;
        for (size_t i = index + 1; i < tree_.size(); i += i & (~i + 1)) {
            tree_[i] += delta;
        }
    }

    double total() const {
        return total_;
    }

    int lower_bound(double target) const {
        if (tree_.size() <= 1) return -1;

        size_t idx = 0;
        double accumulated = 0.0;
        size_t bit = 1;
        while ((bit << 1) < tree_.size()) {
            bit <<= 1;
        }

        while (bit != 0) {
            size_t next = idx + bit;
            if (next < tree_.size() && accumulated + tree_[next] < target) {
                idx = next;
                accumulated += tree_[next];
            }
            bit >>= 1;
        }

        const size_t n = tree_.size() - 1;
        if (idx >= n) {
            return static_cast<int>(n - 1);
        }
        return static_cast<int>(idx);
    }

private:
    std::vector<double> tree_;
    double total_;
};

}  // namespace

KMC::KMC(const std::vector<Coord3D>& basis_sites,
         const std::vector<int>& basis_tunnel_ids,
         const std::vector<Process>& processes,
         double temperature,
         const std::array<int, 3>& ngrid,
         const std::array<double, 3>& grid_size,
         int per_tunnel,
         const std::array<double, 3>& BT)
    : basis_sites_(basis_sites), basis_tunnel_ids_(basis_tunnel_ids), processes_(processes),
      ngrid_(ngrid), grid_size_(grid_size), BT_(BT) {
    (void)temperature;
    (void)per_tunnel;
    if (const char* seed_env = std::getenv("TUTRAST_KMC_SEED")) {
        rng_.seed(static_cast<std::mt19937::result_type>(std::strtoull(seed_env, nullptr, 10)));
    } else {
        std::random_device rd;
        rng_.seed(rd());
    }
    precompute_displacements();
    precompute_outgoing_processes();
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

void KMC::precompute_outgoing_processes() {
    outgoing_process_offsets_.assign(basis_sites_.size() + 1, 0);
    for (const auto& process : processes_) {
        if (process.from_basis >= 0 && process.from_basis < static_cast<int>(basis_sites_.size())) {
            outgoing_process_offsets_[static_cast<size_t>(process.from_basis) + 1]++;
        }
    }

    for (size_t i = 1; i < outgoing_process_offsets_.size(); i++) {
        outgoing_process_offsets_[i] += outgoing_process_offsets_[i - 1];
    }

    outgoing_process_indices_.assign(processes_.size(), -1);
    std::vector<int> write_positions = outgoing_process_offsets_;
    for (size_t i = 0; i < processes_.size(); i++) {
        const int from_basis = processes_[i].from_basis;
        if (from_basis >= 0 && from_basis < static_cast<int>(basis_sites_.size())) {
            outgoing_process_indices_[write_positions[from_basis]++] = static_cast<int>(i);
        }
    }
}

size_t KMC::flat_index(int outer, int inner, int inner_size) {
    return static_cast<size_t>(outer) * static_cast<size_t>(inner_size) + static_cast<size_t>(inner);
}

void KMC::initialize_particles(int n_particles,
                               std::vector<unsigned char>& types,
                               std::vector<int>& particle_slots) const {
    const int nbasis = static_cast<int>(basis_sites_.size());
    types.assign(nbasis, 0);
    particle_slots.clear();

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

    particle_slots.reserve(nbasis);
    for (int i = 0; i < nbasis; i++) {
        if (types[i] == 1) particle_slots.push_back(i);
    }
}

void KMC::run_and_compute_msd(int n_steps, int n_particles,
                              const std::vector<int>& msd_steps,
                              std::vector<std::array<double, 4>>& msd_out) {
    // MATLAB kmc_noplot initialization:
    // types is occupancy by basis index; type_trajectory stores [site_id, cumulative displacement].
    const int nbasis = static_cast<int>(basis_sites_.size());
    std::vector<unsigned char> types;
    std::vector<int> particle_slots;
    initialize_particles(n_particles, types, particle_slots);

    const int effective_particles = static_cast<int>(particle_slots.size());
    msd_out.assign(msd_steps.size(), {0.0, 0.0, 0.0, 0.0});
    if (effective_particles == 0 || n_steps <= 0) {
        return;
    }

    std::vector<int> slot_to_particle_id(nbasis, -1);
    for (int particle = 0; particle < effective_particles; particle++) {
        slot_to_particle_id[particle_slots[particle]] = particle;
    }

    std::vector<int> site_to_slot(nbasis);
    for (int i = 0; i < nbasis; i++) {
        site_to_slot[i] = i;
    }

    FenwickTree active_rates(processes_.size());
    for (int site = 0; site < nbasis; site++) {
        if (types[site] == 0) continue;
        for (int offset = outgoing_process_offsets_[site]; offset < outgoing_process_offsets_[site + 1]; offset++) {
            const int process_index = outgoing_process_indices_[offset];
            active_rates.add(static_cast<size_t>(process_index), processes_[process_index].rate);
        }
    }

    std::uniform_real_distribution<double> uniform01(0.0, 1.0);
    double current_time = 0.0;
    const int max_lag = msd_steps.empty() ? 0 : msd_steps.back();
    const int history_size = std::max(1, max_lag + 1);
    std::vector<double> time_history(history_size, 0.0);
    std::vector<LoggedStep> event_history(history_size);
    std::vector<double> sum_dt(msd_steps.size(), 0.0);
    std::vector<AxisSums> current_squared_diffs(msd_steps.size());
    std::vector<AxisSums> accumulated_squared_diffs(msd_steps.size());
    std::vector<Position3D> lag_diffs(static_cast<size_t>(msd_steps.size()) *
                                      static_cast<size_t>(effective_particles),
                                      Position3D());
    size_t available_lags = 0;

    for (int step = 0; step < n_steps; step++) {
        LoggedStep logged_step;
        const double rate_length = active_rates.total();
        if (rate_length > 0.0) {
            double pick_process = uniform01(rng_) * rate_length;
            if (pick_process <= 0.0) {
                pick_process = std::numeric_limits<double>::min();
            }
            const int selected = active_rates.lower_bound(pick_process);
            if (selected >= 0) {
                const Process& proc = processes_[selected];
                const int from0 = proc.from_basis;
                const int to0 = proc.to_basis;
                const int coord_li = site_to_slot[from0];
                const int coord_empty = site_to_slot[to0];
                logged_step.process_index = selected;
                logged_step.particle_ids[0] = slot_to_particle_id[coord_li];
                logged_step.particle_signs[0] = 1;
                if (coord_li != coord_empty) {
                    logged_step.particle_ids[1] = slot_to_particle_id[coord_empty];
                    logged_step.particle_signs[1] = -1;
                }
                const bool self_process = (from0 == to0);
                const bool to_was_occupied = (!self_process && types[to0] == 1);

                if (!self_process) {
                    types[from0] = 0;
                    types[to0] = 1;
                    for (int offset = outgoing_process_offsets_[from0];
                         offset < outgoing_process_offsets_[from0 + 1];
                         offset++) {
                        const int process_index = outgoing_process_indices_[offset];
                        active_rates.add(static_cast<size_t>(process_index), -processes_[process_index].rate);
                    }
                    if (!to_was_occupied) {
                        for (int offset = outgoing_process_offsets_[to0];
                             offset < outgoing_process_offsets_[to0 + 1];
                             offset++) {
                            const int process_index = outgoing_process_indices_[offset];
                            active_rates.add(static_cast<size_t>(process_index), processes_[process_index].rate);
                        }
                    }

                    site_to_slot[from0] = coord_empty;
                    site_to_slot[to0] = coord_li;
                }

                double r = uniform01(rng_);
                if (r <= 0.0) r = 1e-12;
                current_time += std::log(1.0 / r) / rate_length;
            }
        }

        if (logged_step.process_index >= 0) {
            const auto& d = process_displacements_[logged_step.process_index];
            for (int update_idx = 0; update_idx < 2; update_idx++) {
                const int particle_id = logged_step.particle_ids[update_idx];
                const int particle_sign = logged_step.particle_signs[update_idx];
                if (particle_id < 0 || particle_sign == 0) continue;
                for (size_t lag_index = 0; lag_index < msd_steps.size(); lag_index++) {
                    Position3D& diff = lag_diffs[flat_index(static_cast<int>(lag_index),
                                                            particle_id,
                                                            effective_particles)];
                    apply_position_delta(d, diff, current_squared_diffs[lag_index], static_cast<double>(particle_sign));
                }
            }
        }

        while (available_lags < msd_steps.size() && msd_steps[available_lags] <= step) {
            available_lags++;
        }
        for (size_t lag_index = 0; lag_index < available_lags; lag_index++) {
            const int lag = msd_steps[lag_index];
            const int history_slot = (step - lag) % history_size;
            const LoggedStep& old_step = event_history[history_slot];
            if (old_step.process_index >= 0) {
                const auto& d = process_displacements_[old_step.process_index];
                for (int update_idx = 0; update_idx < 2; update_idx++) {
                    const int particle_id = old_step.particle_ids[update_idx];
                    const int particle_sign = old_step.particle_signs[update_idx];
                    if (particle_id < 0 || particle_sign == 0) continue;
                    Position3D& diff = lag_diffs[flat_index(static_cast<int>(lag_index),
                                                            particle_id,
                                                            effective_particles)];
                    apply_position_delta(d,
                                         diff,
                                         current_squared_diffs[lag_index],
                                         -static_cast<double>(particle_sign));
                }
            }

            sum_dt[lag_index] += current_time - time_history[history_slot];
            accumulated_squared_diffs[lag_index].x += current_squared_diffs[lag_index].x;
            accumulated_squared_diffs[lag_index].y += current_squared_diffs[lag_index].y;
            accumulated_squared_diffs[lag_index].z += current_squared_diffs[lag_index].z;
        }

        const int write_slot = step % history_size;
        time_history[write_slot] = current_time;
        event_history[write_slot] = logged_step;
    }

    for (size_t lag_index = 0; lag_index < msd_steps.size(); lag_index++) {
        const int lag = msd_steps[lag_index];
        const int n_windows = n_steps - lag;
        if (n_windows <= 0) {
            continue;
        }

        const double count = static_cast<double>(effective_particles) * static_cast<double>(n_windows);
        if (count > 0.0) {
            msd_out[lag_index] = {sum_dt[lag_index] / static_cast<double>(n_windows),
                                  accumulated_squared_diffs[lag_index].x / count,
                                  accumulated_squared_diffs[lag_index].y / count,
                                  accumulated_squared_diffs[lag_index].z / count};
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
        std::vector<std::array<double, 4>> msd;
        run_and_compute_msd(n_steps, n_particles, msd_steps, msd);

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
