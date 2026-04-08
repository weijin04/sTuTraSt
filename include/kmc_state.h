#ifndef KMC_STATE_H
#define KMC_STATE_H

#include "types.h"
#include <cstdint>
#include <string>
#include <vector>

// Position in Angstroms (not grid indices)
struct Position3D {
    double x, y, z;
    Position3D() : x(0), y(0), z(0) {}
    Position3D(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

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

struct KmcRunState {
    uint64_t current_step = 0;
    uint64_t target_steps = 0;
    uint64_t lag_plan_steps = 0;
    int requested_particles = 0;
    int effective_particles = 0;
    double current_time = 0.0;
    uint64_t available_lags = 0;

    std::vector<int> msd_steps;
    std::vector<unsigned char> types;
    std::vector<int> slot_to_particle_id;
    std::vector<int> site_to_slot;
    std::vector<double> time_history;
    std::vector<LoggedStep> event_history;
    std::vector<double> sum_dt;
    std::vector<AxisSums> current_squared_diffs;
    std::vector<AxisSums> accumulated_squared_diffs;
    std::vector<Position3D> lag_diffs;
    std::string rng_state;
};

struct KmcCheckpointHeader {
    uint32_t schema_version = 1;
    std::string build_fingerprint;
    std::string rng_engine;
    std::string run_label;
    uint64_t model_fingerprint = 0;
    uint64_t checkpoint_every = 0;
    int floating_rounding_mode = 0;
};

struct KmcCheckpointData {
    KmcCheckpointHeader header;
    KmcRunState state;
};

std::string current_build_fingerprint();
std::string current_rng_engine_name();
int current_floating_rounding_mode();

void write_kmc_checkpoint(const std::string& path, const KmcCheckpointData& checkpoint);
void write_kmc_checkpoint(const std::string& path,
                          const KmcCheckpointHeader& header,
                          const KmcRunState& state);
KmcCheckpointData read_kmc_checkpoint(const std::string& path);
void validate_kmc_checkpoint_identity(const KmcCheckpointData& checkpoint,
                                      uint64_t expected_model_fingerprint,
                                      const std::string& expected_run_label);

#endif // KMC_STATE_H
