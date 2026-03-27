#include "kmc.h"
#include "kmc_state.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

Process make_process(int from_basis, int to_basis, double rate) {
    Process process;
    process.from_basis = from_basis;
    process.to_basis = to_basis;
    process.rate = rate;
    process.tunnel_id = 1;
    return process;
}

KMC make_test_kmc() {
    std::vector<Coord3D> basis_sites = {
        Coord3D(0, 0, 0),
        Coord3D(0, 0, 1),
    };
    std::vector<int> basis_tunnel_ids = {1, 1};
    std::vector<Process> processes = {
        make_process(0, 1, 1.0),
        make_process(1, 0, 1.0),
    };
    return KMC(basis_sites,
               basis_tunnel_ids,
               processes,
               300.0,
               {1, 1, 2},
               {1.0, 1.0, 1.0},
               1,
               {0.0, 0.0, 1.0});
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_exact_msd_match(const std::vector<std::array<double, 4>>& lhs,
                             const std::vector<std::array<double, 4>>& rhs) {
    require(lhs.size() == rhs.size(), "MSD row count mismatch");
    for (size_t i = 0; i < lhs.size(); i++) {
        require(lhs[i] == rhs[i], "MSD row mismatch at index " + std::to_string(i));
    }
}

std::filesystem::path unique_temp_dir() {
    const auto base = std::filesystem::temp_directory_path();
    const auto candidate = base / ("tutrast-kmc-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(candidate);
    std::filesystem::create_directories(candidate);
    return candidate;
}

void test_checkpoint_roundtrip_and_resume() {
    setenv("TUTRAST_KMC_SEED", "42", 1);

    KMC baseline = make_test_kmc();
    KmcRunState baseline_state = baseline.create_initial_run_state(50, 1);
    require(baseline.advance_run_state(baseline_state, 0, 0, {}),
            "Baseline run did not complete");
    const auto baseline_msd = baseline.compute_msd_from_state(baseline_state);

    KMC checkpointed = make_test_kmc();
    KmcRunState partial_state = checkpointed.create_initial_run_state(50, 1);

    const auto temp_dir = unique_temp_dir();
    const auto checkpoint_path = temp_dir / "resume.chk";
    const KmcCheckpointHeader header = {
        1,
        current_build_fingerprint(),
        current_rng_engine_name(),
        "unit_run",
        checkpointed.model_fingerprint(),
        10,
        current_floating_rounding_mode(),
    };

    const auto checkpoint_writer = [&](const KmcRunState& state) {
        write_kmc_checkpoint(checkpoint_path.string(), KmcCheckpointData{header, state});
    };

    require(!checkpointed.advance_run_state(partial_state, 10, 20, checkpoint_writer),
            "Checkpointed run should have paused after 20 steps");
    require(std::filesystem::exists(checkpoint_path), "Checkpoint file was not written");

    const KmcCheckpointData checkpoint = read_kmc_checkpoint(checkpoint_path.string());
    validate_kmc_checkpoint_identity(checkpoint, checkpointed.model_fingerprint(), "unit_run");
    checkpointed.validate_run_state(checkpoint.state, 50, 1);
    require(checkpoint.state.current_step == 20, "Checkpoint current_step mismatch");

    KMC resumed = make_test_kmc();
    KmcRunState resumed_state = checkpoint.state;
    require(resumed.advance_run_state(resumed_state, 10, 0, checkpoint_writer),
            "Resumed run did not complete");
    const auto resumed_msd = resumed.compute_msd_from_state(resumed_state);
    require_exact_msd_match(baseline_msd, resumed_msd);
}

void test_checksum_and_identity_rejection() {
    setenv("TUTRAST_KMC_SEED", "42", 1);

    KMC kmc = make_test_kmc();
    KmcRunState state = kmc.create_initial_run_state(20, 1);
    require(!state.msd_steps.empty(), "Expected non-empty lag set");

    const auto temp_dir = unique_temp_dir();
    const auto good_path = temp_dir / "good.chk";
    const auto corrupted_path = temp_dir / "corrupt.chk";
    const auto mismatch_path = temp_dir / "mismatch.chk";

    const KmcCheckpointHeader header = {
        1,
        current_build_fingerprint(),
        current_rng_engine_name(),
        "unit_run",
        kmc.model_fingerprint(),
        0,
        current_floating_rounding_mode(),
    };

    write_kmc_checkpoint(good_path.string(), KmcCheckpointData{header, state});

    std::filesystem::copy_file(good_path, corrupted_path, std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream file(corrupted_path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        file.seekp(-1, std::ios::end);
        byte ^= 0x1;
        file.write(&byte, 1);
    }

    bool checksum_failed = false;
    try {
        (void)read_kmc_checkpoint(corrupted_path.string());
    } catch (const std::exception&) {
        checksum_failed = true;
    }
    require(checksum_failed, "Corrupted checkpoint should fail checksum validation");

    KmcCheckpointData mismatch = read_kmc_checkpoint(good_path.string());
    mismatch.header.build_fingerprint = "different-build";
    write_kmc_checkpoint(mismatch_path.string(), mismatch);

    bool identity_failed = false;
    try {
        validate_kmc_checkpoint_identity(read_kmc_checkpoint(mismatch_path.string()),
                                         kmc.model_fingerprint(),
                                         "unit_run");
    } catch (const std::exception&) {
        identity_failed = true;
    }
    require(identity_failed, "Build fingerprint mismatch should be rejected");
}

} // namespace

int main() {
    test_checkpoint_roundtrip_and_resume();
    test_checksum_and_identity_rejection();
    return 0;
}
