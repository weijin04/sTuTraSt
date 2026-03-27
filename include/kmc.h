#ifndef KMC_H
#define KMC_H

#include "kmc_state.h"
#include "types.h"
#include <array>
#include <cstddef>
#include <functional>
#include <random>
#include <string>
#include <vector>

class KMC {
public:
    KMC(const std::vector<Coord3D>& basis_sites,
        const std::vector<int>& basis_tunnel_ids,
        const std::vector<Process>& processes,
        double temperature,
        const std::array<int, 3>& ngrid,
        const std::array<double, 3>& grid_size,
        int per_tunnel,
        const std::array<double, 3>& BT = {0.0, 0.0, 0.0});

    // Run multiple simulations and compute average diffusion coefficient per direction
    // Returns [D_x, err_x, D_y, err_y, D_z, err_z]
    std::array<double, 6> run_multiple(int n_runs, int n_steps, int n_particles,
                                        int print_every, const std::string& output_prefix);

    /*
     * Shared execution pipeline for Phase 1:
     *
     *   fresh run  -> create_initial_run_state()
     *              -> advance_run_state()
     *              -> compute_msd_from_state()
     *              -> compute_diffusion_from_msd()
     *
     *   resume     -> deserialize KmcRunState
     *              -> validate_run_state()
     *              -> advance_run_state()
     *              -> compute_msd_from_state()
     *              -> compute_diffusion_from_msd()
     */
    KmcRunState create_initial_run_state(int target_steps, int n_particles) const;
    bool advance_run_state(KmcRunState& state,
                           int checkpoint_every,
                           int max_additional_steps,
                           const std::function<void(const KmcRunState&)>& on_checkpoint);
    void validate_run_state(const KmcRunState& state,
                            int expected_steps,
                            int expected_requested_particles) const;
    std::vector<std::array<double, 4>> compute_msd_from_state(const KmcRunState& state) const;
    std::array<double, 3> compute_diffusion_from_msd(const std::vector<std::array<double, 4>>& msd) const;
    uint64_t model_fingerprint() const { return model_fingerprint_; }

private:
    std::vector<Coord3D> basis_sites_;
    std::vector<int> basis_tunnel_ids_;
    std::vector<Process> processes_;
    std::vector<Position3D> process_displacements_;
    std::vector<int> outgoing_process_offsets_;
    std::vector<int> outgoing_process_indices_;
    std::mt19937 rng_;
    uint64_t model_fingerprint_ = 0;
    std::array<int, 3> ngrid_;
    std::array<double, 3> grid_size_;
    std::array<double, 3> BT_;

    void precompute_displacements();
    std::array<double, 2> linear_fit(const std::vector<double>& x, const std::vector<double>& y) const;
    void precompute_outgoing_processes();
    void initialize_particles(int n_particles,
                              std::vector<unsigned char>& types,
                              std::vector<int>& particle_slots) const;
    uint64_t compute_model_fingerprint() const;
    static size_t flat_index(int outer, int inner, int inner_size);

    // Generate logarithmic MSD step lags: 1,2,...,10,20,...,100,200,...,n_steps
    std::vector<int> generate_msd_steps(int n_steps) const;
};

#endif // KMC_H
