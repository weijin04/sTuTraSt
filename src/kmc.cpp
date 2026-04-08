#include "kmc.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {

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

template <typename T>
void fnv_append_scalar(uint64_t& hash, const T& value) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(&value);
    for (size_t i = 0; i < sizeof(T); i++) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= kPrime;
    }
}

template <typename T>
void fnv_append_vector(uint64_t& hash, const std::vector<T>& values) {
    fnv_append_scalar(hash, static_cast<uint64_t>(values.size()));
    for (const auto& value : values) {
        fnv_append_scalar(hash, value);
    }
}

void fnv_append_position(uint64_t& hash, const Position3D& position) {
    fnv_append_scalar(hash, position.x);
    fnv_append_scalar(hash, position.y);
    fnv_append_scalar(hash, position.z);
}

void fnv_append_process(uint64_t& hash, const Process& process) {
    fnv_append_scalar(hash, process.from_basis);
    fnv_append_scalar(hash, process.to_basis);
    fnv_append_scalar(hash, process.rate);
    fnv_append_scalar(hash, process.cross.i);
    fnv_append_scalar(hash, process.cross.j);
    fnv_append_scalar(hash, process.cross.k);
    for (int value : process.cross.vals) fnv_append_scalar(hash, value);
    fnv_append_scalar(hash, process.ts_cross.i);
    fnv_append_scalar(hash, process.ts_cross.j);
    fnv_append_scalar(hash, process.ts_cross.k);
    for (int value : process.ts_cross.vals) fnv_append_scalar(hash, value);
    fnv_append_scalar(hash, process.tunnel_id);
    fnv_append_scalar(hash, process.tsgroup_id);
    fnv_append_scalar(hash, process.from_cluster_orig);
    fnv_append_scalar(hash, process.to_cluster_orig);
}

std::string serialize_rng_state(const std::mt19937& rng) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << rng;
    return oss.str();
}

void restore_rng_engine_state(std::mt19937& rng, const std::string& state) {
    std::istringstream iss(state);
    iss.imbue(std::locale::classic());
    iss >> rng;
    if (!iss) {
        throw std::runtime_error("Failed to restore mt19937 state from checkpoint");
    }
}

void write_msd_file(const std::string& path, const std::vector<std::array<double, 4>>& msd) {
    std::ofstream msd_file(path);
    for (const auto& row : msd) {
        msd_file << row[0] << " " << row[1] << " " << row[2] << " " << row[3] << '\n';
    }
}

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
    model_fingerprint_ = compute_model_fingerprint();
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

uint64_t KMC::compute_model_fingerprint() const {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    uint64_t hash = kOffset;

    fnv_append_scalar(hash, static_cast<uint64_t>(basis_sites_.size()));
    for (const auto& site : basis_sites_) {
        fnv_append_scalar(hash, site.x);
        fnv_append_scalar(hash, site.y);
        fnv_append_scalar(hash, site.z);
    }
    fnv_append_vector(hash, basis_tunnel_ids_);
    fnv_append_scalar(hash, static_cast<uint64_t>(processes_.size()));
    for (const auto& process : processes_) {
        fnv_append_process(hash, process);
    }
    fnv_append_scalar(hash, static_cast<uint64_t>(process_displacements_.size()));
    for (const auto& displacement : process_displacements_) {
        fnv_append_position(hash, displacement);
    }
    fnv_append_vector(hash, outgoing_process_offsets_);
    fnv_append_vector(hash, outgoing_process_indices_);
    for (int value : ngrid_) fnv_append_scalar(hash, value);
    for (double value : grid_size_) fnv_append_scalar(hash, value);
    for (double value : BT_) fnv_append_scalar(hash, value);

    return hash;
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

std::vector<int> KMC::generate_msd_steps(int n_steps) const {
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

std::array<double, 2> KMC::linear_fit(const std::vector<double>& x, const std::vector<double>& y) const {
    int n = static_cast<int>(x.size());
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

KmcRunState KMC::create_initial_run_state(int target_steps, int n_particles, int lag_plan_steps) const {
    KmcRunState state;
    state.target_steps = static_cast<uint64_t>(target_steps);
    state.lag_plan_steps = static_cast<uint64_t>(std::max(target_steps, lag_plan_steps > 0 ? lag_plan_steps : target_steps));
    state.requested_particles = n_particles;
    state.msd_steps = generate_msd_steps(static_cast<int>(state.lag_plan_steps));
    state.types.clear();
    std::vector<int> particle_slots;
    initialize_particles(n_particles, state.types, particle_slots);

    const int nbasis = static_cast<int>(basis_sites_.size());
    state.effective_particles = static_cast<int>(particle_slots.size());
    state.slot_to_particle_id.assign(nbasis, -1);
    for (int particle = 0; particle < state.effective_particles; particle++) {
        state.slot_to_particle_id[particle_slots[particle]] = particle;
    }

    state.site_to_slot.resize(nbasis);
    for (int i = 0; i < nbasis; i++) {
        state.site_to_slot[i] = i;
    }

    const int max_lag = state.msd_steps.empty() ? 0 : state.msd_steps.back();
    const int history_size = std::max(1, max_lag + 1);
    state.time_history.assign(history_size, 0.0);
    state.event_history.assign(history_size, LoggedStep());
    state.sum_dt.assign(state.msd_steps.size(), 0.0);
    state.current_squared_diffs.assign(state.msd_steps.size(), AxisSums());
    state.accumulated_squared_diffs.assign(state.msd_steps.size(), AxisSums());
    state.lag_diffs.assign(static_cast<size_t>(state.msd_steps.size()) *
                               static_cast<size_t>(state.effective_particles),
                           Position3D());
    state.rng_state = serialize_rng_state(rng_);

    return state;
}

void KMC::run_and_compute_msd(int n_steps, int n_particles,
                              const std::vector<int>& msd_steps,
                              std::vector<std::array<double, 4>>& msd_out) {
    // Dedicated fast path for plain runs without checkpoint/campaign overhead.
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
            const auto& displacement = process_displacements_[logged_step.process_index];
            for (int update_idx = 0; update_idx < 2; update_idx++) {
                const int particle_id = logged_step.particle_ids[update_idx];
                const int particle_sign = logged_step.particle_signs[update_idx];
                if (particle_id < 0 || particle_sign == 0) continue;
                for (size_t lag_index = 0; lag_index < msd_steps.size(); lag_index++) {
                    Position3D& diff = lag_diffs[flat_index(static_cast<int>(lag_index),
                                                            particle_id,
                                                            effective_particles)];
                    apply_position_delta(displacement, diff, current_squared_diffs[lag_index], static_cast<double>(particle_sign));
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
                const auto& displacement = process_displacements_[old_step.process_index];
                for (int update_idx = 0; update_idx < 2; update_idx++) {
                    const int particle_id = old_step.particle_ids[update_idx];
                    const int particle_sign = old_step.particle_signs[update_idx];
                    if (particle_id < 0 || particle_sign == 0) continue;
                    Position3D& diff = lag_diffs[flat_index(static_cast<int>(lag_index),
                                                            particle_id,
                                                            effective_particles)];
                    apply_position_delta(displacement,
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

void KMC::validate_run_state(const KmcRunState& state,
                             int expected_steps,
                             int expected_requested_particles,
                             int expected_lag_plan_steps) const {
    const int nbasis = static_cast<int>(basis_sites_.size());
    if (state.target_steps != static_cast<uint64_t>(expected_steps)) {
        throw std::runtime_error("Checkpoint target step count does not match current run");
    }
    const int resolved_lag_plan_steps = std::max(expected_steps,
                                                 expected_lag_plan_steps > 0 ? expected_lag_plan_steps : expected_steps);
    if (state.lag_plan_steps != static_cast<uint64_t>(resolved_lag_plan_steps)) {
        throw std::runtime_error("Checkpoint lag planning horizon does not match current run");
    }
    if (state.requested_particles != expected_requested_particles) {
        throw std::runtime_error("Checkpoint particle count does not match current run");
    }
    if (state.types.size() != static_cast<size_t>(nbasis) ||
        state.slot_to_particle_id.size() != static_cast<size_t>(nbasis) ||
        state.site_to_slot.size() != static_cast<size_t>(nbasis)) {
        throw std::runtime_error("Checkpoint basis-sized vectors do not match the current model");
    }

    const std::vector<int> expected_msd_steps = generate_msd_steps(resolved_lag_plan_steps);
    if (state.msd_steps != expected_msd_steps) {
        throw std::runtime_error("Checkpoint lag definition does not match the current run configuration");
    }

    const size_t history_size = state.msd_steps.empty() ? 1U : static_cast<size_t>(state.msd_steps.back()) + 1U;
    if (state.time_history.size() != history_size || state.event_history.size() != history_size) {
        throw std::runtime_error("Checkpoint rolling history size does not match the lag definition");
    }
    if (state.sum_dt.size() != state.msd_steps.size() ||
        state.current_squared_diffs.size() != state.msd_steps.size() ||
        state.accumulated_squared_diffs.size() != state.msd_steps.size()) {
        throw std::runtime_error("Checkpoint lag accumulators do not match the lag definition");
    }
    const size_t expected_lag_diffs = static_cast<size_t>(state.effective_particles) * state.msd_steps.size();
    if (state.lag_diffs.size() != expected_lag_diffs) {
        throw std::runtime_error("Checkpoint lag diff buffer size is inconsistent");
    }
    if (state.available_lags > state.msd_steps.size()) {
        throw std::runtime_error("Checkpoint available_lags exceeds lag count");
    }
    if (state.current_step > state.target_steps) {
        throw std::runtime_error("Checkpoint current_step exceeds target_steps");
    }
}

bool KMC::advance_run_state(KmcRunState& state,
                            int checkpoint_every,
                            int max_additional_steps,
                            const std::function<void(const KmcRunState&)>& on_checkpoint) {
    validate_run_state(state,
                       static_cast<int>(state.target_steps),
                       state.requested_particles,
                       static_cast<int>(state.lag_plan_steps));

    restore_rng_engine_state(rng_, state.rng_state);

    const int nbasis = static_cast<int>(basis_sites_.size());
    FenwickTree active_rates(processes_.size());
    for (int site = 0; site < nbasis; site++) {
        if (state.types[site] == 0) continue;
        for (int offset = outgoing_process_offsets_[site]; offset < outgoing_process_offsets_[site + 1]; offset++) {
            const int process_index = outgoing_process_indices_[offset];
            active_rates.add(static_cast<size_t>(process_index), processes_[process_index].rate);
        }
    }

    const uint64_t stop_step = (max_additional_steps > 0)
        ? std::min(state.target_steps, state.current_step + static_cast<uint64_t>(max_additional_steps))
        : state.target_steps;
    const uint64_t start_step = state.current_step;

    std::uniform_real_distribution<double> uniform01(0.0, 1.0);

    while (state.current_step < stop_step) {
        const int step = static_cast<int>(state.current_step);
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
                const int coord_li = state.site_to_slot[from0];
                const int coord_empty = state.site_to_slot[to0];
                logged_step.process_index = selected;
                logged_step.particle_ids[0] = state.slot_to_particle_id[coord_li];
                logged_step.particle_signs[0] = 1;
                if (coord_li != coord_empty) {
                    logged_step.particle_ids[1] = state.slot_to_particle_id[coord_empty];
                    logged_step.particle_signs[1] = -1;
                }
                const bool self_process = (from0 == to0);
                const bool to_was_occupied = (!self_process && state.types[to0] == 1);

                if (!self_process) {
                    state.types[from0] = 0;
                    state.types[to0] = 1;
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

                    state.site_to_slot[from0] = coord_empty;
                    state.site_to_slot[to0] = coord_li;
                }

                double r = uniform01(rng_);
                if (r <= 0.0) r = 1e-12;
                state.current_time += std::log(1.0 / r) / rate_length;
            }
        }

        if (logged_step.process_index >= 0) {
            const auto& displacement = process_displacements_[logged_step.process_index];
            for (int update_idx = 0; update_idx < 2; update_idx++) {
                const int particle_id = logged_step.particle_ids[update_idx];
                const int particle_sign = logged_step.particle_signs[update_idx];
                if (particle_id < 0 || particle_sign == 0) continue;
                for (size_t lag_index = 0; lag_index < state.msd_steps.size(); lag_index++) {
                    Position3D& diff = state.lag_diffs[flat_index(static_cast<int>(lag_index),
                                                                  particle_id,
                                                                  state.effective_particles)];
                    apply_position_delta(displacement,
                                         diff,
                                         state.current_squared_diffs[lag_index],
                                         static_cast<double>(particle_sign));
                }
            }
        }

        while (state.available_lags < state.msd_steps.size() &&
               state.msd_steps[state.available_lags] <= step) {
            state.available_lags++;
        }
        for (size_t lag_index = 0; lag_index < state.available_lags; lag_index++) {
            const int lag = state.msd_steps[lag_index];
            const int history_slot = (step - lag) % static_cast<int>(state.time_history.size());
            const LoggedStep& old_step = state.event_history[history_slot];
            if (old_step.process_index >= 0) {
                const auto& displacement = process_displacements_[old_step.process_index];
                for (int update_idx = 0; update_idx < 2; update_idx++) {
                    const int particle_id = old_step.particle_ids[update_idx];
                    const int particle_sign = old_step.particle_signs[update_idx];
                    if (particle_id < 0 || particle_sign == 0) continue;
                    Position3D& diff = state.lag_diffs[flat_index(static_cast<int>(lag_index),
                                                                  particle_id,
                                                                  state.effective_particles)];
                    apply_position_delta(displacement,
                                         diff,
                                         state.current_squared_diffs[lag_index],
                                         -static_cast<double>(particle_sign));
                }
            }

            state.sum_dt[lag_index] += state.current_time - state.time_history[history_slot];
            state.accumulated_squared_diffs[lag_index].x += state.current_squared_diffs[lag_index].x;
            state.accumulated_squared_diffs[lag_index].y += state.current_squared_diffs[lag_index].y;
            state.accumulated_squared_diffs[lag_index].z += state.current_squared_diffs[lag_index].z;
        }

        const int write_slot = step % static_cast<int>(state.time_history.size());
        state.time_history[write_slot] = state.current_time;
        state.event_history[write_slot] = logged_step;

        state.current_step++;

        if (checkpoint_every > 0 &&
            state.current_step < state.target_steps &&
            (state.current_step % static_cast<uint64_t>(checkpoint_every)) == 0 &&
            on_checkpoint) {
            state.rng_state = serialize_rng_state(rng_);
            on_checkpoint(state);
        }
    }

    state.rng_state = serialize_rng_state(rng_);

    const bool completed = state.current_step >= state.target_steps;
    const bool should_emit_pause_checkpoint = !completed && on_checkpoint && state.current_step > start_step;
    const bool emitted_on_boundary =
        checkpoint_every > 0 &&
        (state.current_step % static_cast<uint64_t>(checkpoint_every)) == 0 &&
        state.current_step < state.target_steps;

    if (should_emit_pause_checkpoint && !emitted_on_boundary) {
        on_checkpoint(state);
    }

    return completed;
}

std::vector<std::array<double, 4>> KMC::compute_msd_from_state(const KmcRunState& state) const {
    if (state.current_step != state.target_steps) {
        throw std::runtime_error("Cannot compute final MSD from an incomplete KMC run state");
    }

    std::vector<std::array<double, 4>> msd_out;
    if (state.effective_particles == 0 || state.target_steps == 0) {
        return msd_out;
    }

    for (size_t lag_index = 0; lag_index < state.msd_steps.size(); lag_index++) {
        const int lag = state.msd_steps[lag_index];
        const int n_windows = static_cast<int>(state.target_steps) - lag;
        if (n_windows <= 0) {
            break;
        }

        const double count = static_cast<double>(state.effective_particles) * static_cast<double>(n_windows);
        if (count > 0.0) {
            msd_out.push_back({state.sum_dt[lag_index] / static_cast<double>(n_windows),
                               state.accumulated_squared_diffs[lag_index].x / count,
                               state.accumulated_squared_diffs[lag_index].y / count,
                               state.accumulated_squared_diffs[lag_index].z / count});
        }
    }

    return msd_out;
}

std::array<double, 3> KMC::compute_diffusion_from_msd(const std::vector<std::array<double, 4>>& msd) const {
    // Box size in Angstroms: abc = ngrid * grid_size
    std::array<double, 3> abc;
    abc[0] = ngrid_[0] * grid_size_[0];
    abc[1] = ngrid_[1] * grid_size_[1];
    abc[2] = ngrid_[2] * grid_size_[2];

    std::array<double, 3> diffusion_coefficients = {0.0, 0.0, 0.0};

    for (int dir = 0; dir < 3; dir++) {
        if (BT_[dir] == 0.0) {
            diffusion_coefficients[dir] = 0.0;
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
            diffusion_coefficients[dir] = 0.0;
            continue;
        }

        std::vector<double> time_values;
        std::vector<double> msd_values;

        for (int i = istart; i <= iend; i++) {
            time_values.push_back(msd[i][0]);
            msd_values.push_back(msd[i][dir + 1]);
        }

        auto [slope, intercept] = linear_fit(time_values, msd_values);
        (void)intercept;

        // D = 0.5 * slope * 1e-16 (convert Angstrom^2/s to cm^2/s)
        diffusion_coefficients[dir] = 0.5 * slope * 1e-16;
    }

    return diffusion_coefficients;
}

std::string KMC::current_rng_state() const {
    return serialize_rng_state(rng_);
}

void KMC::restore_rng_state(const std::string& state) {
    restore_rng_engine_state(rng_, state);
}

std::array<double, 6> KMC::run_multiple(int n_runs, int n_steps, int n_particles,
                                        int /* print_every */, const std::string& output_prefix) {
    std::vector<std::array<double, 3>> diffusion_coefficients(n_runs);
    const std::vector<int> msd_steps = generate_msd_steps(n_steps);

    for (int run = 0; run < n_runs; run++) {
        std::vector<std::array<double, 4>> msd;
        run_and_compute_msd(n_steps, n_particles, msd_steps, msd);
        write_msd_file(output_prefix + "msd" + std::to_string(run + 1) + ".dat", msd);
        diffusion_coefficients[run] = compute_diffusion_from_msd(msd);
    }

    std::array<double, 6> result;
    for (int dir = 0; dir < 3; dir++) {
        double sum = 0.0;
        for (int run = 0; run < n_runs; run++) {
            sum += diffusion_coefficients[run][dir];
        }
        const double mean = sum / n_runs;

        double sq_sum = 0.0;
        for (int run = 0; run < n_runs; run++) {
            const double diff = diffusion_coefficients[run][dir] - mean;
            sq_sum += diff * diff;
        }
        const double stddev = std::sqrt(sq_sum / n_runs);

        result[dir * 2] = mean;
        result[dir * 2 + 1] = stddev;
    }

    std::cout << "Diffusion coefficients (cm^2/s):" << '\n';
    std::cout << "  D_x = " << result[0] << " +/- " << result[1] << '\n';
    std::cout << "  D_y = " << result[2] << " +/- " << result[3] << '\n';
    std::cout << "  D_z = " << result[4] << " +/- " << result[5] << '\n';

    return result;
}
