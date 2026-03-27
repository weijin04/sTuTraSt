#include "cli_options.h"
#include "kmc_campaign.h"
#include "input_parser.h"
#include "cube_parser.h"
#include "grid.h"
#include "cluster.h"
#include "transition_state.h"
#include "tunnel.h"
#include "output_writer.h"
#include "kmc.h"
#include "pbc.h"
#include "matrix_utils.h"

#include <iostream>
#include <fstream>
#include <memory>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <set>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <stdexcept>

namespace {

std::string checkpoint_path_for(const CliOptions& options, const std::string& run_label) {
    const std::filesystem::path dir = options.checkpoint_dir.empty()
        ? std::filesystem::path(".")
        : std::filesystem::path(options.checkpoint_dir);
    return std::filesystem::absolute(dir / (run_label + ".chk")).string();
}

std::string campaign_checkpoint_path_for(const CliOptions& options, const std::string& run_label) {
    const std::filesystem::path campaign_root = std::filesystem::absolute(std::filesystem::path(options.campaign_dir));
    return (campaign_root / "checkpoints" / (run_label + ".chk")).string();
}

void write_diffusion_file(const std::string& path, const std::array<double, 6>& diffusion) {
    std::ofstream d_file(path);
    d_file << std::scientific;
    d_file << diffusion[0] << " " << diffusion[1] << " "
           << diffusion[2] << " " << diffusion[3] << " "
           << diffusion[4] << " " << diffusion[5] << '\n';
}

KmcCheckpointHeader make_checkpoint_header(const KMC& kmc,
                                           const std::string& run_label,
                                           int checkpoint_every) {
    KmcCheckpointHeader header;
    header.build_fingerprint = current_build_fingerprint();
    header.rng_engine = current_rng_engine_name();
    header.run_label = run_label;
    header.model_fingerprint = kmc.model_fingerprint();
    header.checkpoint_every = static_cast<uint64_t>(std::max(0, checkpoint_every));
    header.floating_rounding_mode = current_floating_rounding_mode();
    return header;
}

void validate_checkpoint_for_resume(const KmcCheckpointData& checkpoint,
                                    const KMC& kmc,
                                    const std::string& expected_run_label,
                                    int expected_steps,
                                    int expected_particles,
                                    int expected_lag_plan_steps = -1) {
    validate_kmc_checkpoint_identity(checkpoint, kmc.model_fingerprint(), expected_run_label);
    kmc.validate_run_state(checkpoint.state, expected_steps, expected_particles, expected_lag_plan_steps);
}

bool run_single_kmc_phase1(KMC& kmc,
                           const CliOptions& options,
                           int n_steps,
                           int n_particles,
                           const std::string& run_label,
                           const std::string& output_prefix,
                           std::array<double, 3>& diffusion_out) {
    const std::string checkpoint_path = options.has_resume()
        ? options.resume_path
        : checkpoint_path_for(options, run_label);

    KmcRunState state = kmc.create_initial_run_state(n_steps, n_particles);
    int checkpoint_every = options.checkpoint_every;

    if (options.has_resume()) {
        const KmcCheckpointData checkpoint = read_kmc_checkpoint(options.resume_path);
        validate_checkpoint_for_resume(checkpoint, kmc, run_label, n_steps, n_particles);
        state = checkpoint.state;
        if (!options.checkpoint_every_set) {
            checkpoint_every = static_cast<int>(checkpoint.header.checkpoint_every);
        }
    }

    const auto on_checkpoint = [&](const KmcRunState& current_state) {
        write_kmc_checkpoint(checkpoint_path,
                             KmcCheckpointData{make_checkpoint_header(kmc, run_label, checkpoint_every),
                                               current_state});
        std::cout << "Checkpoint saved: " << checkpoint_path
                  << " (step " << current_state.current_step
                  << "/" << current_state.target_steps << ")\n";
    };

    const bool completed = kmc.advance_run_state(state,
                                                 checkpoint_every,
                                                 options.max_kmc_steps,
                                                 on_checkpoint);
    if (!completed) {
        std::cout << "kMC paused after " << state.current_step
                  << " / " << state.target_steps
                  << " steps. Resume with --resume " << checkpoint_path << '\n';
        return false;
    }

    const std::vector<std::array<double, 4>> msd = kmc.compute_msd_from_state(state);
    std::ofstream msd_file(output_prefix + "msd1.dat");
    for (const auto& row : msd) {
        msd_file << row[0] << " " << row[1] << " " << row[2] << " " << row[3] << '\n';
    }
    diffusion_out = kmc.compute_diffusion_from_msd(msd);
    return true;
}

KmcCampaignManifest make_initial_campaign_manifest(const KMC& kmc,
                                                  const std::string& run_label_prefix,
                                                  const std::string& output_prefix,
                                                  int n_steps,
                                                  int lag_plan_steps,
                                                  int n_particles,
                                                  int target_runs,
                                                  int checkpoint_every) {
    KmcCampaignManifest manifest;
    manifest.build_fingerprint = current_build_fingerprint();
    manifest.rng_engine = current_rng_engine_name();
    manifest.model_fingerprint = kmc.model_fingerprint();
    manifest.floating_rounding_mode = current_floating_rounding_mode();
    manifest.target_steps = n_steps;
    manifest.lag_plan_steps = lag_plan_steps;
    manifest.requested_particles = n_particles;
    manifest.target_runs = target_runs;
    manifest.checkpoint_every = std::max(0, checkpoint_every);
    manifest.run_label_prefix = run_label_prefix;
    manifest.output_prefix = output_prefix;
    manifest.current_rng_state = kmc.current_rng_state();
    return manifest;
}

bool run_campaign_kmc_phase2(KMC& kmc,
                             const CliOptions& options,
                             int n_steps,
                             int n_particles,
                             int target_runs,
                             const std::string& run_label_prefix,
                             const std::string& output_prefix,
                             std::array<double, 6>& diffusion_out) {
    const std::string manifest_path = campaign_manifest_path(options.campaign_dir);
    KmcCampaignManifest manifest;

    if (std::filesystem::exists(manifest_path)) {
        manifest = read_kmc_campaign_manifest(manifest_path);
        if (options.campaign_plan_steps_set && options.campaign_plan_steps != manifest.lag_plan_steps) {
            throw std::runtime_error("Existing campaign already fixed its lag planning horizon; create a new campaign to change --campaign-plan-steps");
        }
        validate_kmc_campaign_manifest(manifest,
                                       kmc.model_fingerprint(),
                                       run_label_prefix,
                                       output_prefix,
                                       n_steps,
                                       n_particles,
                                       manifest.lag_plan_steps);
        if (n_steps > manifest.lag_plan_steps) {
            throw std::runtime_error("Requested n_steps exceeds the campaign lag planning horizon; start the campaign with a larger --campaign-plan-steps value");
        }
        if (n_steps < manifest.target_steps) {
            throw std::runtime_error("Shrinking target n_steps for an existing campaign is not supported");
        }
        if (n_steps > manifest.target_steps) {
            if (manifest.target_runs != 1 || manifest.completed_runs.size() > 1) {
                throw std::runtime_error("Exact step-horizon extension is currently supported only for single-run campaigns; append-runs continuation remains available for multi-run campaigns");
            }
            manifest.active_run_index = 1;
            manifest.active_checkpoint_path = campaign_checkpoint_path_for(options, run_label_prefix + "run1");
            manifest.completed_runs.clear();
        }
        manifest.target_steps = n_steps;
        if (target_runs < static_cast<int>(manifest.completed_runs.size())) {
            throw std::runtime_error("Campaign target n_runs is smaller than already completed runs");
        }
        if (target_runs > manifest.target_runs) {
            manifest.target_runs = target_runs;
        }
        if (options.checkpoint_every_set) {
            manifest.checkpoint_every = options.checkpoint_every;
        }
    } else {
        manifest = make_initial_campaign_manifest(kmc,
                                                 run_label_prefix,
                                                 output_prefix,
                                                 n_steps,
                                                 options.campaign_plan_steps_set ? options.campaign_plan_steps : n_steps,
                                                 n_particles,
                                                 target_runs,
                                                 options.checkpoint_every);
        write_kmc_campaign_manifest(manifest_path, manifest);
    }

    int next_run_index = static_cast<int>(manifest.completed_runs.size()) + 1;
    if (manifest.active_run_index > 0) {
        next_run_index = manifest.active_run_index;
    }

    while (next_run_index <= manifest.target_runs) {
        const std::string run_label = run_label_prefix + "run" + std::to_string(next_run_index);
        const std::string checkpoint_path = campaign_checkpoint_path_for(options, run_label);
        KmcRunState state = kmc.create_initial_run_state(n_steps, n_particles, manifest.lag_plan_steps);

        if (manifest.active_run_index == next_run_index && !manifest.active_checkpoint_path.empty()) {
            const KmcCheckpointData checkpoint = read_kmc_checkpoint(manifest.active_checkpoint_path);
            validate_kmc_checkpoint_identity(checkpoint, kmc.model_fingerprint(), run_label);
            kmc.validate_run_state(checkpoint.state,
                                   static_cast<int>(checkpoint.state.target_steps),
                                   n_particles,
                                   manifest.lag_plan_steps);
            state = checkpoint.state;
            if (state.target_steps < static_cast<uint64_t>(n_steps)) {
                if (state.lag_plan_steps < static_cast<uint64_t>(n_steps)) {
                    throw std::runtime_error("Checkpoint lag planning horizon is too small for the requested step extension");
                }
                state.target_steps = static_cast<uint64_t>(n_steps);
            }
        } else {
            kmc.restore_rng_state(manifest.current_rng_state);
            state = kmc.create_initial_run_state(n_steps, n_particles, manifest.lag_plan_steps);
        }

        const auto on_checkpoint = [&](const KmcRunState& current_state) {
            write_kmc_checkpoint(checkpoint_path,
                                 KmcCheckpointData{make_checkpoint_header(kmc, run_label, manifest.checkpoint_every),
                                                   current_state});
            manifest.active_run_index = next_run_index;
            manifest.active_checkpoint_path = checkpoint_path;
            write_kmc_campaign_manifest(manifest_path, manifest);
            std::cout << "Campaign checkpoint saved: " << checkpoint_path
                      << " (run " << next_run_index
                      << ", step " << current_state.current_step
                      << "/" << current_state.target_steps << ")\n";
        };

        const bool completed = kmc.advance_run_state(state,
                                                     manifest.checkpoint_every,
                                                     options.max_kmc_steps,
                                                     on_checkpoint);
        if (!completed) {
            diffusion_out = aggregate_campaign_diffusion(manifest.completed_runs);
            std::cout << "Campaign paused during run " << next_run_index
                      << ". Re-run with --campaign-dir " << std::filesystem::absolute(options.campaign_dir).string()
                      << " to continue." << '\n';
            return false;
        }

        const std::vector<std::array<double, 4>> msd = kmc.compute_msd_from_state(state);
        std::ofstream msd_file(output_prefix + "msd" + std::to_string(next_run_index) + ".dat");
        for (const auto& row : msd) {
            msd_file << row[0] << " " << row[1] << " " << row[2] << " " << row[3] << '\n';
        }

        const std::array<double, 3> diffusion = kmc.compute_diffusion_from_msd(msd);
        manifest.completed_runs.push_back(KmcCampaignRunResult{next_run_index, diffusion});
        manifest.current_rng_state = state.rng_state;
        manifest.active_run_index = 0;
        manifest.active_checkpoint_path.clear();
        if (manifest.target_runs == 1 && manifest.lag_plan_steps > static_cast<int>(state.target_steps)) {
            write_kmc_checkpoint(checkpoint_path,
                                 KmcCheckpointData{make_checkpoint_header(kmc, run_label, manifest.checkpoint_every),
                                                   state});
            manifest.active_checkpoint_path = checkpoint_path;
        }
        write_kmc_campaign_manifest(manifest_path, manifest);

        const std::array<double, 6> aggregate = aggregate_campaign_diffusion(manifest.completed_runs);
        write_diffusion_file("D_ave_" + run_label_prefix.substr(1, run_label_prefix.size() - 2) + ".dat",
                             aggregate);

        next_run_index++;
    }

    diffusion_out = aggregate_campaign_diffusion(manifest.completed_runs);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
    const CliOptions options = parse_cli_options(argc, argv);
    if (options.show_help) {
        std::cout << cli_help_text();
        return 0;
    }

    std::cout << "============================================" << '\n';
    std::cout << "TuTraSt - C++ Implementation" << '\n';
    std::cout << "Tunnel and Transition State Search Algorithm" << '\n';
    std::cout << "============================================\n" << '\n';
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Parse input parameters
    std::cout << "Reading input parameters..." << '\n';
    InputParams params = InputParser::parse("input.param");
    validate_cli_options(options, params);
    
    std::cout << "Parameters:" << '\n';
    std::cout << "  Energy unit: " << params.energy_unit << '\n';
    std::cout << "  Temperatures: ";
    bool kmc_paused = false;
    for (double T : params.temperatures) {
        std::cout << T << " ";
    }
    std::cout << "K" << '\n';
    std::cout << "  Energy step: " << params.energy_step << " kJ/mol" << '\n';
    std::cout << "  Energy cutoff: " << params.energy_cutoff << " kJ/mol" << '\n';
    
    // Parse cube file
    std::cout << "\nReading grid.cube..." << '\n';
    std::array<int, 3> ngrid;
    std::array<double, 3> grid_size;
    std::vector<double> pot_data;
    
    if (!CubeParser::parse("grid.cube", params.energy_unit, ngrid, grid_size, pot_data)) {
        std::cerr << "Error: Failed to parse grid.cube" << std::endl;
        return 1;
    }
    
    // Initialize grid
    std::cout << "\nInitializing grid..." << '\n';
    auto grid = std::make_shared<Grid>(ngrid);
    grid->initialize(pot_data, params.energy_step, params.energy_cutoff);
    pot_data.clear();
    pot_data.shrink_to_fit();
    
    // Initialize managers
    auto cluster_mgr = std::make_shared<ClusterManager>(grid);
    auto ts_mgr = std::make_shared<TransitionStateManager>(grid);
    auto tunnel_mgr = std::make_shared<TunnelManager>(grid, cluster_mgr, ts_mgr);

    auto maybe_dump_ts_list = [](const std::string& filename, const std::vector<TSPoint>& ts_list) {
        const char* flag = std::getenv("TUTRAST_DEBUG_DUMP_TS_LIST");
        if (!flag || std::string(flag) != "1") return;
        std::ofstream f(filename);
        if (!f.is_open()) return;
        f << std::scientific << std::setprecision(15);
        // MATLAB TS_list_all (pre-organize, pre-reindex) is 10 columns:
        // x y z level energy cluster1 cluster2 crossi crossj crossk
        // Write MATLAB-style 1-based coordinates to compare directly against restart2.mat.
        for (const auto& ts : ts_list) {
            f << (ts.x + 1) << " "
              << (ts.y + 1) << " "
              << (ts.z + 1) << " "
              << ts.level << " "
              << ts.energy << " "
              << ts.cluster1_id << " "
              << ts.cluster2_id << " "
              << ts.cross_i << " "
              << ts.cross_j << " "
              << ts.cross_k << "\n";
        }
    };
    auto maybe_dump_minid_c = [&]() {
        const char* flag = std::getenv("TUTRAST_DEBUG_DUMP_MINIDC");
        if (!flag || std::string(flag) != "1") return;
        std::ofstream fL("debug_minID_L_precompact_flat.txt");
        if (fL.is_open()) {
            for (int z = 0; z < grid->nz(); z++) {
                for (int y = 0; y < grid->ny(); y++) {
                    for (int x = 0; x < grid->nx(); x++) {
                        fL << grid->minID_L(x, y, z) << "\n";
                    }
                }
            }
        }
        std::ofstream f("debug_minID_C_precompact_flat.txt");
        if (!f.is_open()) return;
        for (int z = 0; z < grid->nz(); z++) {
            for (int y = 0; y < grid->ny(); y++) {
                for (int x = 0; x < grid->nx(); x++) {
                    f << grid->minID_C(x, y, z) << "\n";
                }
            }
        }
        std::ofstream fl("debug_level_matrix_flat.txt");
        if (!fl.is_open()) return;
        for (int z = 0; z < grid->nz(); z++) {
            for (int y = 0; y < grid->ny(); y++) {
                for (int x = 0; x < grid->nx(); x++) {
                    fl << grid->level_at(x, y, z) << "\n";
                }
            }
        }
    };
    
    // Main algorithm: Cluster growth and TS detection
    std::cout << "\n============================================" << '\n';
    std::cout << "Starting TS search..." << '\n';
    std::cout << "============================================\n" << '\n';
    
    int level_stop = static_cast<int>(std::ceil(params.energy_cutoff / params.energy_step));
    std::vector<TSPoint> ts_list_all;
    std::vector<double> E_volume;
    std::vector<int> tunnel_cluster;
    std::vector<std::array<int,3>> tunnel_cluster_dim;
    std::array<double, 3> BT = {0.0, 0.0, 0.0};  // Breakthrough energies
    std::vector<std::array<int,3>> tunnel_directions;  // Independent tunnel directions (RREF result)
    
    // Phase 4: Precompute cumulative volume per level to avoid O(N_grid) scan at every level
    std::vector<long long> cum_volume(grid->max_level() + 2, 0);
    {
        for (int z = 0; z < grid->nz(); z++)
            for (int y = 0; y < grid->ny(); y++)
                for (int x = 0; x < grid->nx(); x++)
                    cum_volume[grid->level_at(x, y, z)]++;
        for (int l = 1; l < (int)cum_volume.size(); l++)
            cum_volume[l] += cum_volume[l - 1];
    }

    for (int level = grid->min_level(); level <= std::min(level_stop, grid->max_level()); level++) {
        // Use precomputed cumulative histogram instead of O(N_grid) scan
        int volume = (level < (int)cum_volume.size()) ? static_cast<int>(cum_volume[level]) : static_cast<int>(cum_volume.back());
        E_volume.push_back(volume);
        
        double energy = level / grid->level_scale();
        std::cout << "Level " << level << " (" << energy << " kJ/mol)";
        std::cout << ", Volume: " << volume << '\n';
        
        std::vector<TSPoint> ts_list;
        std::vector<std::array<int,3>> tunnel_list;
        
        // Grow existing clusters (only for levels > min_level)
        // Matches Octave: if level>level_min
        if (level > grid->min_level()) {
            cluster_mgr->grow_clusters(level, ts_list, tunnel_list, tunnel_cluster, 
                                       tunnel_cluster_dim, params.energy_step, &ts_list_all);
        }
        
        // Initiate new clusters (for all levels)
        cluster_mgr->initiate_clusters(level);
        
        // Debug: Count valid clusters (id != 0) at this level
        int valid_cluster_count = 0;
        for (const auto& c : cluster_mgr->clusters()) {
            if (c.id != 0) {
                valid_cluster_count++;
            }
        }
        std::cout << "  Valid clusters after level " << level << ": " << valid_cluster_count << '\n';
        
        // Add TS points to global list
        ts_list_all.insert(ts_list_all.end(), ts_list.begin(), ts_list.end());
        
        // Check for breakthrough using reduced row echelon form (RREF)
        // This matches the Octave implementation: echelon=rref(abs(tunnel_list))
        if (!tunnel_list.empty()) {
            // Debug: show tunnel_list size before RREF (for early levels)
            const int DEBUG_MAX_LEVEL = 15;
            if (level <= DEBUG_MAX_LEVEL) {
                std::cout << "  tunnel_list has " << tunnel_list.size() << " entries before RREF" << '\n';
            }
            
            // Compute RREF to find independent tunnel directions
            auto echelon = MatrixUtils::rref(tunnel_list);
            
            if (!echelon.empty()) {
                // Update tunnel_directions with independent vectors
                tunnel_directions = echelon;
                
                std::cout << "  Breakthrough in " << echelon.size() << " independent direction(s): " << '\n';
                for (const auto& dir : echelon) {
                    std::cout << "    [" << dir[0] << ", " << dir[1] << ", " << dir[2] << "]" << '\n';
                }
                
                // Check each axis for breakthrough
                // Matches Octave: if BT(abc)==0 && sum(list.tunnel_directions(:,abc))>0
                bool has_breakthrough = false;
                for (int d = 0; d < 3; d++) {
                    int sum = 0;
                    for (const auto& dir : tunnel_directions) {
                        sum += dir[d];
                    }
                    
                    if (BT[d] == 0.0 && sum > 0) {
                        // MATLAB: BT(abc)=level*energy_step
                        BT[d] = level * params.energy_step;
                        has_breakthrough = true;
                    }
                }
                
                if (has_breakthrough) {
                    std::cout << "  >>> Breakthrough detected at level " << level 
                             << " (" << energy << " kJ/mol)" << '\n';
                }
            }
        }
        
        if (level % 10 == 0 || !tunnel_list.empty()) {
            std::cout << "  Clusters: " << cluster_mgr->num_clusters() 
                     << ", TS points: " << ts_list_all.size() << '\n';
        }
    }
    
    std::cout << "\n============================================" << '\n';
    std::cout << "TS search complete" << '\n';
    std::cout << "============================================\n" << '\n';
    
    std::cout << "Total clusters: " << cluster_mgr->num_clusters() << '\n';
    std::cout << "Total TS points: " << ts_list_all.size() << '\n';
    maybe_dump_ts_list("debug_ts_list_all_precompact.dat", ts_list_all);
    maybe_dump_minid_c();
    
    // Compact clusters: filter out merged clusters (id==0) and renumber sequentially
    // This matches MATLAB's cluster filtering step
    std::map<int, int> cluster_id_mapping = cluster_mgr->compact_clusters(ts_list_all);
    maybe_dump_ts_list("debug_ts_list_all_postcompact.dat", ts_list_all);

    // Update tunnel_cluster IDs to match compacted cluster IDs
    for (auto& tc : tunnel_cluster) {
        if (cluster_id_mapping.count(tc)) {
            tc = cluster_id_mapping[tc];
        }
    }
    
    std::vector<Process> processes;
    // MATLAB compatibility: only build TS/tunnel/process structures if breakthrough exists.
    if (!tunnel_directions.empty()) {
        // Organize transition states
        std::cout << "\nOrganizing transition states..." << '\n';
        ts_mgr->organize_ts_groups(ts_list_all);
        std::cout << "TS groups: " << ts_mgr->ts_groups().size() << '\n';
        
        // Organize tunnels
        std::cout << "\nOrganizing tunnels..." << '\n';
        tunnel_mgr->organize_tunnels(tunnel_cluster, tunnel_cluster_dim);
        
        // Generate processes
        std::cout << "\nGenerating processes..." << '\n';
        tunnel_mgr->generate_processes(processes);
    } else {
        std::cout << "\nNo breakthrough directions found; skipping TS/tunnel/process generation." << '\n';
    }
    
    auto output_writer = std::make_shared<OutputWriter>(cluster_mgr, tunnel_mgr, ts_mgr, params.energy_step);
    bool structural_outputs_written = false;

    const auto write_structural_outputs = [&](const std::vector<Process>& current_processes) {
        std::cout << "\n============================================" << '\n';
        std::cout << "Writing output files..." << '\n';
        std::cout << "============================================\n" << '\n';

        if (!current_processes.empty()) {
            output_writer->write_basis("basis.dat", current_processes);
            output_writer->write_ts_data("TS_data.out");
        }
        output_writer->write_tunnel_info("tunnel_info.out");

        std::cout << "\nBreakthrough energies:" << '\n';
        std::cout << "  A direction: " << BT[0] << " kJ/mol" << '\n';
        std::cout << "  B direction: " << BT[1] << " kJ/mol" << '\n';
        std::cout << "  C direction: " << BT[2] << " kJ/mol" << '\n';
        output_writer->write_breakthrough("BT.dat", BT);

        if (!params.temperatures.empty() && !current_processes.empty()) {
            std::string evol_t = std::to_string(static_cast<int>(params.temperatures[0]));
            output_writer->write_energy_volume("Evol_" + evol_t + ".dat", E_volume);
        }
        structural_outputs_written = true;
    };

    // Calculate rates for each temperature
    std::cout << "\n============================================" << '\n';
    std::cout << "Calculating transition rates..." << '\n';
    std::cout << "============================================\n" << '\n';
    
    // Calculate average voxel size (in Angstroms)
    // grid_size is the voxel spacing; ave_grid_size = mean(grid_size) matches MATLAB
    double ave_grid_size = (grid_size[0] + grid_size[1] + grid_size[2]) / 3.0;
    std::cout << "  Average grid size (voxel): " << ave_grid_size << " Angstroms" << '\n';
    
    for (double T : params.temperatures) {
        std::cout << "\nTemperature: " << T << " K" << '\n';
        
        double RT = R_GAS * T;  // J/(mol·K) * K = J/mol
        double beta = 1.0 / RT;  // mol/J
        double kappa = 0.5;  // Transmission coefficient
        double prefactor = kappa * std::sqrt(1.0 / (beta * 2.0 * M_PI * params.particle_mass));
        
        // Calculate rates for each process using Boltzmann partition functions
        // Following MATLAB: k = prefactor * Bsum_TS / (Bsum_cluster * ave_grid_size * 1e-10)
        for (auto& proc : processes) {
            const auto& ts_groups = ts_mgr->ts_groups();
            if (proc.tsgroup_id < 0 || proc.tsgroup_id >= static_cast<int>(ts_groups.size())) {
                continue;
            }

            const auto& ts_group = ts_groups[proc.tsgroup_id];
            // Use from_cluster_orig not from_basis (which gets modified to basis index)
            const Cluster& from_cluster = cluster_mgr->get_cluster(proc.from_cluster_orig);

            // Calculate Boltzmann sum for TS group
            // Using factor of 1000 to convert kJ/mol to J/mol for exponential
            double Bsum_TS = 0.0;
            for (const auto& ts_pt : ts_group.points) {
                double dE_TS = ts_pt.energy - from_cluster.min_energy;
                Bsum_TS += std::exp(-1000.0 * beta * dE_TS);
            }

            // Calculate Boltzmann sum for cluster states
            // MATLAB removes TS-marked points (info(:,6)==1) from finC before partition function
            // Then adds Bsum_TS back. ts_flag is per-cluster-point, matching MATLAB's info(:,6).
            double Bsum_cluster = 0.0;
            for (const auto& pt : from_cluster.points) {
                if (pt.ts_flag != 0) continue;
                double E_pt = grid->energy_at(pt.x, pt.y, pt.z);
                double dE_cluster = E_pt - from_cluster.min_energy;
                Bsum_cluster += std::exp(-1000.0 * beta * dE_cluster);
            }

            // Add TS contribution to cluster partition function
            Bsum_cluster += Bsum_TS;

            // Calculate rate constant
            proc.rate = prefactor * Bsum_TS / (Bsum_cluster * ave_grid_size * 1e-10);
        }
        
        // Map cluster indices to basis site indices
        // In MATLAB: tunnel_cluster_list = unique(processes(:,1:2))
        // Only clusters involved in processes become basis sites
        std::set<int> unique_cluster_ids;
        for (const auto& proc : processes) {
            unique_cluster_ids.insert(proc.from_cluster_orig);
            unique_cluster_ids.insert(proc.to_cluster_orig);
        }
        
        // Create mapping from original cluster ID to basis index
        std::map<int, int> cluster_to_basis;
        int basis_idx = 0;  // 0-indexed for C++ (MATLAB is 1-indexed, but we need 0-based for arrays)
        for (int cluster_id : unique_cluster_ids) {
            cluster_to_basis[cluster_id] = basis_idx++;
        }
        
        // Update process basis indices
        for (auto& proc : processes) {
            proc.from_basis = cluster_to_basis[proc.from_cluster_orig];
            proc.to_basis = cluster_to_basis[proc.to_cluster_orig];
        }
        
        // Write outputs for this temperature (MATLAB writes processes only when non-empty).
        std::string T_str = std::to_string(static_cast<int>(T));
        if (!processes.empty()) {
            output_writer->write_processes("processes_" + T_str + ".dat", processes);
        }
        
        // Match MATLAB fallback: if no processes, emit zero diffusion for each temperature.
        if (processes.empty()) {
            std::ofstream D_file("D_ave_" + T_str + ".dat");
            D_file << std::scientific;
            D_file << 0.0 << " " << 0.0 << " "
                   << 0.0 << " " << 0.0 << " "
                   << 0.0 << " " << 0.0 << '\n';
            D_file.close();
        } else if (params.run_kmc) {
            // Run kMC if requested
            std::cout << "\nRunning kMC simulations..." << '\n';
            
            // Extract basis sites from clusters involved in processes (MATLAB-compatible)
            std::vector<Coord3D> basis_sites;
            std::vector<int> basis_tunnel_ids;
            for (int cluster_id : unique_cluster_ids) {
                const Cluster& cluster = cluster_mgr->get_cluster(cluster_id);
                if (!cluster.points.empty()) {
                    // Find minimum energy point (same as output_writer does)
                    int min_idx = 0;
                    double min_e = std::numeric_limits<double>::max();
                    for (size_t i = 0; i < cluster.points.size(); i++) {
                        const auto& pt = cluster.points[i];
                        double e = cluster_mgr->grid()->energy_at(pt.x, pt.y, pt.z);
                        if (e < min_e) {
                            min_e = e;
                            min_idx = i;
                        }
                    }
                    const auto& min_pt = cluster.points[min_idx];
                    basis_sites.push_back(Coord3D(min_pt.x, min_pt.y, min_pt.z));
                    basis_tunnel_ids.push_back(cluster.tunnel_id);
                }
            }
            
            // Create KMC simulator with grid info and BT for proper fitting range
            KMC kmc(basis_sites, basis_tunnel_ids, processes, T, ngrid, grid_size,
                    params.per_tunnel, BT);

            if (!structural_outputs_written && params.temperatures.size() == 1) {
                write_structural_outputs(processes);
                output_writer.reset();
                tunnel_mgr.reset();
                ts_mgr.reset();
                cluster_mgr.reset();
                grid.reset();
                ts_list_all.clear();
                ts_list_all.shrink_to_fit();
                tunnel_cluster.clear();
                tunnel_cluster.shrink_to_fit();
                tunnel_cluster_dim.clear();
                tunnel_cluster_dim.shrink_to_fit();
                tunnel_directions.clear();
                tunnel_directions.shrink_to_fit();
                E_volume.shrink_to_fit();
            }

            if (options.has_campaign()) {
                std::array<double, 6> diffusion = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
                const bool completed = run_campaign_kmc_phase2(kmc,
                                                               options,
                                                               params.n_steps,
                                                               params.n_particles,
                                                               params.n_runs,
                                                               "T" + T_str + "_",
                                                               "T" + T_str + "_",
                                                               diffusion);
                if (!completed) {
                    kmc_paused = true;
                }
            } else if (options.has_kmc_control()) {
                std::array<double, 3> diffusion = {0.0, 0.0, 0.0};
                const bool completed = run_single_kmc_phase1(kmc,
                                                             options,
                                                             params.n_steps,
                                                             params.n_particles,
                                                             "T" + T_str + "_run1",
                                                             "T" + T_str + "_",
                                                             diffusion);
                if (completed) {
                    write_diffusion_file("D_ave_" + T_str + ".dat",
                                         {diffusion[0], 0.0, diffusion[1], 0.0, diffusion[2], 0.0});
                } else {
                    kmc_paused = true;
                }
            } else {
                // Run simulations
                auto D_ave = kmc.run_multiple(params.n_runs, params.n_steps,
                                              params.n_particles, params.print_every,
                                              "T" + T_str + "_");

                // Write diffusion coefficient: D_x err_x D_y err_y D_z err_z (6 values like MATLAB)
                std::ofstream D_file("D_ave_" + T_str + ".dat");
                D_file << std::scientific;
                D_file << D_ave[0] << " " << D_ave[1] << " "
                       << D_ave[2] << " " << D_ave[3] << " "
                       << D_ave[4] << " " << D_ave[5] << '\n';
                D_file.close();
            }
        }
    }
    
    // Write other output files
    if (!structural_outputs_written) {
        write_structural_outputs(processes);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\n============================================" << '\n';
    std::cout << (kmc_paused ? "Paused after checkpoint!" : "Complete!") << '\n';
    std::cout << "Total time: " << duration.count() << " seconds" << '\n';
    std::cout << "============================================" << '\n';
    
    return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n'
                  << "Use --help for supported checkpoint/resume options." << std::endl;
        return 1;
    }
}
