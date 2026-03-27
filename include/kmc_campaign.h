#ifndef KMC_CAMPAIGN_H
#define KMC_CAMPAIGN_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct KmcCampaignRunResult {
    int run_index = 0;
    std::array<double, 3> diffusion = {0.0, 0.0, 0.0};
};

struct KmcCampaignManifest {
    uint32_t schema_version = 1;
    std::string build_fingerprint;
    std::string rng_engine;
    uint64_t model_fingerprint = 0;
    int floating_rounding_mode = 0;

    int target_steps = 0;
    int requested_particles = 0;
    int target_runs = 0;
    int checkpoint_every = 0;

    std::string run_label_prefix;
    std::string output_prefix;
    std::string current_rng_state;
    int active_run_index = 0;
    std::string active_checkpoint_path;

    std::vector<KmcCampaignRunResult> completed_runs;
};

std::string campaign_manifest_path(const std::string& campaign_dir);
void write_kmc_campaign_manifest(const std::string& path, const KmcCampaignManifest& manifest);
KmcCampaignManifest read_kmc_campaign_manifest(const std::string& path);
void validate_kmc_campaign_manifest(const KmcCampaignManifest& manifest,
                                    uint64_t expected_model_fingerprint,
                                    const std::string& expected_run_label_prefix,
                                    const std::string& expected_output_prefix,
                                    int expected_steps,
                                    int expected_particles);
std::array<double, 6> aggregate_campaign_diffusion(const std::vector<KmcCampaignRunResult>& completed_runs);

#endif // KMC_CAMPAIGN_H
