#include "kmc_campaign.h"
#include "kmc_state.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

std::string manifest_magic() {
    return "TUTRAST_KMC_CAMPAIGN_V1";
}

std::string trim(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

void atomic_write_text_file(const std::string& path, const std::string& content) {
    const std::filesystem::path final_path(path);
    if (!final_path.parent_path().empty()) {
        std::filesystem::create_directories(final_path.parent_path());
    }
    const std::filesystem::path temp_path = final_path.string() + ".tmp";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open temporary manifest file for write");
    }
    out << content;
    out.close();
    if (!out) {
        throw std::runtime_error("Failed to flush manifest to disk");
    }
    std::filesystem::rename(temp_path, final_path);
}

} // namespace

std::string campaign_manifest_path(const std::string& campaign_dir) {
    return (std::filesystem::absolute(std::filesystem::path(campaign_dir)) / "campaign.manifest").string();
}

void write_kmc_campaign_manifest(const std::string& path, const KmcCampaignManifest& manifest) {
    std::ostringstream out;
    out << manifest_magic() << '\n';
    out << "schema_version=" << manifest.schema_version << '\n';
    out << "build_fingerprint=" << manifest.build_fingerprint << '\n';
    out << "rng_engine=" << manifest.rng_engine << '\n';
    out << "model_fingerprint=" << manifest.model_fingerprint << '\n';
    out << "floating_rounding_mode=" << manifest.floating_rounding_mode << '\n';
    out << "target_steps=" << manifest.target_steps << '\n';
    out << "requested_particles=" << manifest.requested_particles << '\n';
    out << "target_runs=" << manifest.target_runs << '\n';
    out << "checkpoint_every=" << manifest.checkpoint_every << '\n';
    out << "run_label_prefix=" << manifest.run_label_prefix << '\n';
    out << "output_prefix=" << manifest.output_prefix << '\n';
    out << "current_rng_state=" << manifest.current_rng_state << '\n';
    out << "active_run_index=" << manifest.active_run_index << '\n';
    out << "active_checkpoint_path=" << manifest.active_checkpoint_path << '\n';
    for (const auto& run : manifest.completed_runs) {
        out << "run_result=" << run.run_index << " "
            << run.diffusion[0] << " "
            << run.diffusion[1] << " "
            << run.diffusion[2] << '\n';
    }

    atomic_write_text_file(path, out.str());
}

KmcCampaignManifest read_kmc_campaign_manifest(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open campaign manifest: " + path);
    }

    std::string line;
    if (!std::getline(input, line) || trim(line) != manifest_magic()) {
        throw std::runtime_error("Campaign manifest magic mismatch");
    }

    KmcCampaignManifest manifest;
    while (std::getline(input, line)) {
        if (trim(line).empty()) continue;
        const size_t pos = line.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Malformed manifest line: " + line);
        }

        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);

        if (key == "schema_version") manifest.schema_version = static_cast<uint32_t>(std::stoul(value));
        else if (key == "build_fingerprint") manifest.build_fingerprint = value;
        else if (key == "rng_engine") manifest.rng_engine = value;
        else if (key == "model_fingerprint") manifest.model_fingerprint = std::stoull(value);
        else if (key == "floating_rounding_mode") manifest.floating_rounding_mode = std::stoi(value);
        else if (key == "target_steps") manifest.target_steps = std::stoi(value);
        else if (key == "requested_particles") manifest.requested_particles = std::stoi(value);
        else if (key == "target_runs") manifest.target_runs = std::stoi(value);
        else if (key == "checkpoint_every") manifest.checkpoint_every = std::stoi(value);
        else if (key == "run_label_prefix") manifest.run_label_prefix = value;
        else if (key == "output_prefix") manifest.output_prefix = value;
        else if (key == "current_rng_state") manifest.current_rng_state = value;
        else if (key == "active_run_index") manifest.active_run_index = std::stoi(value);
        else if (key == "active_checkpoint_path") manifest.active_checkpoint_path = value;
        else if (key == "run_result") {
            std::istringstream iss(value);
            KmcCampaignRunResult run;
            iss >> run.run_index >> run.diffusion[0] >> run.diffusion[1] >> run.diffusion[2];
            if (!iss) {
                throw std::runtime_error("Malformed run_result line in campaign manifest");
            }
            manifest.completed_runs.push_back(run);
        } else {
            throw std::runtime_error("Unknown manifest key: " + key);
        }
    }

    if (manifest.schema_version != 1) {
        throw std::runtime_error("Unsupported campaign manifest schema version: " + std::to_string(manifest.schema_version));
    }

    std::sort(manifest.completed_runs.begin(),
              manifest.completed_runs.end(),
              [](const KmcCampaignRunResult& lhs, const KmcCampaignRunResult& rhs) {
                  return lhs.run_index < rhs.run_index;
              });
    return manifest;
}

void validate_kmc_campaign_manifest(const KmcCampaignManifest& manifest,
                                    uint64_t expected_model_fingerprint,
                                    const std::string& expected_run_label_prefix,
                                    const std::string& expected_output_prefix,
                                    int expected_steps,
                                    int expected_particles) {
    if (manifest.build_fingerprint != current_build_fingerprint()) {
        throw std::runtime_error("Campaign manifest build fingerprint does not match the current binary");
    }
    if (manifest.rng_engine != current_rng_engine_name()) {
        throw std::runtime_error("Campaign manifest RNG engine does not match the current binary");
    }
    if (manifest.floating_rounding_mode != current_floating_rounding_mode()) {
        throw std::runtime_error("Campaign manifest floating-point environment does not match the current process");
    }
    if (manifest.model_fingerprint != expected_model_fingerprint) {
        throw std::runtime_error("Campaign manifest model fingerprint does not match the current derived KMC model");
    }
    if (manifest.run_label_prefix != expected_run_label_prefix) {
        throw std::runtime_error("Campaign manifest run label prefix mismatch");
    }
    if (manifest.output_prefix != expected_output_prefix) {
        throw std::runtime_error("Campaign manifest output prefix mismatch");
    }
    if (manifest.target_steps != expected_steps) {
        throw std::runtime_error("Campaign manifest target_steps mismatch");
    }
    if (manifest.requested_particles != expected_particles) {
        throw std::runtime_error("Campaign manifest particle count mismatch");
    }
}

std::array<double, 6> aggregate_campaign_diffusion(const std::vector<KmcCampaignRunResult>& completed_runs) {
    std::array<double, 6> result = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (completed_runs.empty()) {
        return result;
    }

    for (int dir = 0; dir < 3; dir++) {
        double sum = 0.0;
        for (const auto& run : completed_runs) {
            sum += run.diffusion[dir];
        }
        const double mean = sum / static_cast<double>(completed_runs.size());

        double sq_sum = 0.0;
        for (const auto& run : completed_runs) {
            const double diff = run.diffusion[dir] - mean;
            sq_sum += diff * diff;
        }

        result[dir * 2] = mean;
        result[dir * 2 + 1] = std::sqrt(sq_sum / static_cast<double>(completed_runs.size()));
    }

    return result;
}
