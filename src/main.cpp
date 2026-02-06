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

int main(int /* argc */, char** /* argv */) {
    std::cout << "============================================" << std::endl;
    std::cout << "TuTraSt - C++ Implementation" << std::endl;
    std::cout << "Tunnel and Transition State Search Algorithm" << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Parse input parameters
    std::cout << "Reading input parameters..." << std::endl;
    InputParams params = InputParser::parse("input.param");
    
    std::cout << "Parameters:" << std::endl;
    std::cout << "  Energy unit: " << params.energy_unit << std::endl;
    std::cout << "  Temperatures: ";
    for (double T : params.temperatures) {
        std::cout << T << " ";
    }
    std::cout << "K" << std::endl;
    std::cout << "  Energy step: " << params.energy_step << " kJ/mol" << std::endl;
    std::cout << "  Energy cutoff: " << params.energy_cutoff << " kJ/mol" << std::endl;
    
    // Parse cube file
    std::cout << "\nReading grid.cube..." << std::endl;
    std::array<int, 3> ngrid;
    std::array<double, 3> grid_size;
    std::vector<std::vector<double>> pot_data;
    
    if (!CubeParser::parse("grid.cube", params.energy_unit, ngrid, grid_size, pot_data)) {
        std::cerr << "Error: Failed to parse grid.cube" << std::endl;
        return 1;
    }
    
    // Initialize grid
    std::cout << "\nInitializing grid..." << std::endl;
    auto grid = std::make_shared<Grid>(ngrid);
    grid->initialize(pot_data, params.energy_step, params.energy_cutoff);
    
    // Initialize managers
    auto cluster_mgr = std::make_shared<ClusterManager>(grid);
    auto ts_mgr = std::make_shared<TransitionStateManager>(grid);
    auto tunnel_mgr = std::make_shared<TunnelManager>(grid, cluster_mgr, ts_mgr);
    
    // Main algorithm: Cluster growth and TS detection
    std::cout << "\n============================================" << std::endl;
    std::cout << "Starting TS search..." << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    int level_stop = static_cast<int>(std::ceil(params.energy_cutoff / params.energy_step));
    std::vector<TSPoint> ts_list_all;
    std::vector<double> E_volume;
    std::vector<int> tunnel_cluster;
    std::vector<std::array<int,3>> tunnel_cluster_dim;
    std::array<double, 3> BT = {0.0, 0.0, 0.0};  // Breakthrough energies
    std::vector<std::array<int,3>> tunnel_directions;  // Independent tunnel directions (RREF result)
    
    for (int level = grid->min_level(); level <= std::min(level_stop, grid->max_level()); level++) {
        // Count points at this level
        int volume = 0;
        for (int z = 0; z < grid->nz(); z++) {
            for (int y = 0; y < grid->ny(); y++) {
                for (int x = 0; x < grid->nx(); x++) {
                    if (grid->level_at(x, y, z) <= level) {
                        volume++;
                    }
                }
            }
        }
        E_volume.push_back(volume);
        
        double energy = level / grid->level_scale();
        std::cout << "Level " << level << " (" << energy << " kJ/mol)";
        std::cout << ", Volume: " << volume << std::endl;
        
        std::vector<TSPoint> ts_list;
        std::vector<std::array<int,3>> tunnel_list;
        
        // Grow existing clusters (only for levels > min_level)
        // Matches Octave: if level>level_min
        if (level > grid->min_level()) {
            cluster_mgr->grow_clusters(level, ts_list, tunnel_list, tunnel_cluster, 
                                       tunnel_cluster_dim, params.energy_step);
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
        std::cout << "  Valid clusters after level " << level << ": " << valid_cluster_count << std::endl;
        
        // Add TS points to global list
        ts_list_all.insert(ts_list_all.end(), ts_list.begin(), ts_list.end());
        
        // Check for breakthrough using reduced row echelon form (RREF)
        // This matches the Octave implementation: echelon=rref(abs(tunnel_list))
        if (!tunnel_list.empty()) {
            // Debug: show tunnel_list size before RREF (for early levels)
            const int DEBUG_MAX_LEVEL = 15;
            if (level <= DEBUG_MAX_LEVEL) {
                std::cout << "  tunnel_list has " << tunnel_list.size() << " entries before RREF" << std::endl;
            }
            
            // Compute RREF to find independent tunnel directions
            auto echelon = MatrixUtils::rref(tunnel_list);
            
            if (!echelon.empty()) {
                // Update tunnel_directions with independent vectors
                tunnel_directions = echelon;
                
                std::cout << "  Breakthrough in " << echelon.size() << " independent direction(s): " << std::endl;
                for (const auto& dir : echelon) {
                    std::cout << "    [" << dir[0] << ", " << dir[1] << ", " << dir[2] << "]" << std::endl;
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
                             << " (" << energy << " kJ/mol)" << std::endl;
                }
            }
        }
        
        if (level % 10 == 0 || !tunnel_list.empty()) {
            std::cout << "  Clusters: " << cluster_mgr->num_clusters() 
                     << ", TS points: " << ts_list_all.size() << std::endl;
        }
    }
    
    std::cout << "\n============================================" << std::endl;
    std::cout << "TS search complete" << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    std::cout << "Total clusters: " << cluster_mgr->num_clusters() << std::endl;
    std::cout << "Total TS points: " << ts_list_all.size() << std::endl;
    
    // Compact clusters: filter out merged clusters (id==0) and renumber sequentially
    // This matches MATLAB's cluster filtering step
    std::map<int, int> cluster_id_mapping = cluster_mgr->compact_clusters(ts_list_all);
    
    // Organize transition states
    std::cout << "\nOrganizing transition states..." << std::endl;
    ts_mgr->organize_ts_groups(ts_list_all);
    std::cout << "TS groups: " << ts_mgr->ts_groups().size() << std::endl;
    
    // Organize tunnels
    std::cout << "\nOrganizing tunnels..." << std::endl;
    tunnel_mgr->organize_tunnels(tunnel_cluster, tunnel_cluster_dim);
    
    // Generate processes
    std::cout << "\nGenerating processes..." << std::endl;
    std::vector<Process> processes;
    tunnel_mgr->generate_processes(processes);
    
    // Calculate rates for each temperature
    std::cout << "\n============================================" << std::endl;
    std::cout << "Calculating transition rates..." << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    auto output_writer = std::make_shared<OutputWriter>(cluster_mgr, tunnel_mgr, ts_mgr, params.energy_step);
    
    // Calculate average grid size (in Angstroms)
    // MATLAB uses mean(grid_size) which is the average box dimension, NOT voxel size
    // Previous error: was using voxel size (grid_size/ngrid) causing 68x error in rates
    double ave_grid_size = (grid_size[0] + grid_size[1] + grid_size[2]) / 3.0;
    std::cout << "  Average grid size (box): " << ave_grid_size << " Angstroms" << std::endl;
    
    for (double T : params.temperatures) {
        std::cout << "\nTemperature: " << T << " K" << std::endl;
        
        double RT = R_GAS * T;  // J/(mol·K) * K = J/mol
        double beta = 1.0 / RT;  // mol/J
        double kappa = 0.5;  // Transmission coefficient
        double prefactor = kappa * std::sqrt(1.0 / (beta * 2.0 * M_PI * params.particle_mass));
        
        // Calculate rates for each process using Boltzmann partition functions
        // Following MATLAB: k = prefactor * Bsum_TS / (Bsum_cluster * ave_grid_size * 1e-10)
        for (auto& proc : processes) {
            const auto& ts_groups = ts_mgr->ts_groups();
            if (proc.tsgroup_id >= static_cast<int>(ts_groups.size())) {
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
            double Bsum_cluster = 0.0;
            for (const auto& pt : from_cluster.points) {
                double E_pt = grid->energy_at(pt.x, pt.y, pt.z);
                double dE_cluster = E_pt - from_cluster.min_energy;
                Bsum_cluster += std::exp(-1000.0 * beta * dE_cluster);
            }
            
            // Add TS contribution to cluster partition function
            Bsum_cluster += Bsum_TS;
            
            // Calculate rate constant
            // prefactor has units of Hz (1/s)
            // Division by (ave_grid_size * 1e-10) converts to proper rate units
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
        
        // Write outputs for this temperature
        std::string T_str = std::to_string(static_cast<int>(T));
        output_writer->write_processes("processes_" + T_str + ".dat", processes);
        
        // Run kMC if requested
        if (params.run_kmc && !processes.empty()) {
            std::cout << "\nRunning kMC simulations..." << std::endl;
            
            // Extract basis sites from clusters involved in processes (MATLAB-compatible)
            std::vector<Coord3D> basis_sites;
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
                }
            }
            
            // Create KMC simulator with grid info for proper unit conversions
            KMC kmc(basis_sites, processes, T, ngrid, grid_size);
            
            // Run simulations
            auto D_ave = kmc.run_multiple(params.n_runs, params.n_steps, 
                                           params.n_particles, params.print_every, 
                                           "T" + T_str + "_");
            
            // Write diffusion coefficient: D_x err_x D_y err_y D_z err_z (6 values like MATLAB)
            std::ofstream D_file("D_ave_" + T_str + ".dat");
            D_file << std::scientific;
            D_file << D_ave[0] << " " << D_ave[1] << " " 
                   << D_ave[2] << " " << D_ave[3] << " "
                   << D_ave[4] << " " << D_ave[5] << std::endl;
            D_file.close();
        }
    }
    
    // Write other output files
    std::cout << "\n============================================" << std::endl;
    std::cout << "Writing output files..." << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    output_writer->write_basis("basis.dat", processes);
    output_writer->write_tunnel_info("tunnel_info.out");
    output_writer->write_ts_data("TS_data.out");
    
    // Write breakthrough energies
    std::cout << "\nBreakthrough energies:" << std::endl;
    std::cout << "  A direction: " << BT[0] << " kJ/mol" << std::endl;
    std::cout << "  B direction: " << BT[1] << " kJ/mol" << std::endl;
    std::cout << "  C direction: " << BT[2] << " kJ/mol" << std::endl;
    output_writer->write_breakthrough("BT.dat", BT);
    
    if (!params.temperatures.empty()) {
        std::string T_str = std::to_string(static_cast<int>(params.temperatures[0]));
        output_writer->write_energy_volume("Evol_" + T_str + ".dat", E_volume);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\n============================================" << std::endl;
    std::cout << "Complete!" << std::endl;
    std::cout << "Total time: " << duration.count() << " seconds" << std::endl;
    std::cout << "============================================" << std::endl;
    
    return 0;
}
