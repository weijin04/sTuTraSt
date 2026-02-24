#include "output_writer.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>

OutputWriter::OutputWriter(std::shared_ptr<ClusterManager> cluster_mgr,
                          std::shared_ptr<TunnelManager> tunnel_mgr,
                          std::shared_ptr<TransitionStateManager> ts_mgr,
                          double energy_step)
    : cluster_mgr_(cluster_mgr), tunnel_mgr_(tunnel_mgr), ts_mgr_(ts_mgr),
      energy_step_(energy_step) {
}

void OutputWriter::write_basis(const std::string& filename, const std::vector<Process>& processes) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    // Get unique cluster IDs from processes (MATLAB: unique(processes(:,1:2)))
    std::set<int> unique_cluster_ids;
    for (const auto& proc : processes) {
        unique_cluster_ids.insert(proc.from_cluster_orig);
        unique_cluster_ids.insert(proc.to_cluster_orig);
    }
    
    // Create mapping from cluster ID to basis index
    std::map<int, int> cluster_to_basis;
    int basis_idx = 1;
    for (int cluster_id : unique_cluster_ids) {
        cluster_to_basis[cluster_id] = basis_idx++;
    }
    
    // Write basis sites: x y z tunnel_id cluster_id (5 columns, MATLAB format)
    for (int cluster_id : unique_cluster_ids) {
        const Cluster& cluster = cluster_mgr_->get_cluster(cluster_id);
        if (!cluster.points.empty()) {
            // Find the actual minimum energy point in the cluster (not just first point)
            // MATLAB uses list.finC(iC).min(2:4), where finC excludes TS-flagged points.
            int min_idx = 0;
            double min_e = std::numeric_limits<double>::max();
            bool found_non_ts = false;
            for (size_t i = 0; i < cluster.points.size(); i++) {
                const auto& pt = cluster.points[i];
                if (pt.ts_flag != 0) continue;
                double e = cluster_mgr_->grid()->energy_at(pt.x, pt.y, pt.z);
                if (e < min_e) {
                    min_e = e;
                    min_idx = i;
                    found_non_ts = true;
                }
            }
            if (!found_non_ts) {
                for (size_t i = 0; i < cluster.points.size(); i++) {
                    const auto& pt = cluster.points[i];
                    double e = cluster_mgr_->grid()->energy_at(pt.x, pt.y, pt.z);
                    if (e < min_e) {
                        min_e = e;
                        min_idx = i;
                    }
                }
            }
            const auto& min_pt = cluster.points[min_idx];
            file << min_pt.x << " " << min_pt.y << " " << min_pt.z << " "
                 << cluster.tunnel_id << " " << cluster_id << std::endl;
        }
    }
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}

void OutputWriter::write_processes(const std::string& filename,
                                  const std::vector<Process>& processes) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    file << std::scientific << std::setprecision(10);
    
    // MATLAB format: 13 columns
    // from_basis to_basis rate cross_i cross_j cross_k ts_cross_i ts_cross_j ts_cross_k tunnel_id tsgroup_id from_orig to_orig
    // Note: basis indices are written as 1-indexed for MATLAB compatibility
    for (const auto& proc : processes) {
        file << std::setw(23) << (proc.from_basis + 1) << " "  // Convert to 1-indexed
             << std::setw(23) << (proc.to_basis + 1) << " "    // Convert to 1-indexed
             << std::setw(23) << proc.rate << " "
             << std::setw(23) << proc.cross.i << " " 
             << std::setw(23) << proc.cross.j << " " 
             << std::setw(23) << proc.cross.k << " "
             << std::setw(23) << proc.ts_cross.i << " " 
             << std::setw(23) << proc.ts_cross.j << " " 
             << std::setw(23) << proc.ts_cross.k << " "
             << std::setw(23) << proc.tunnel_id << " "
             << std::setw(23) << (proc.tsgroup_id + 1) << " "  // Convert to 1-indexed
             << std::setw(23) << proc.from_cluster_orig << " "
             << std::setw(23) << proc.to_cluster_orig << std::endl;
    }
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}

void OutputWriter::write_tunnel_info(const std::string& filename) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    const auto& tunnels = tunnel_mgr_->tunnels();
    
    file << "Total tunnels: " << tunnels.size() << std::endl;
    
    for (const auto& tunnel : tunnels) {
        file << "\nTunnel " << tunnel.id << ":" << std::endl;
        file << "  Dimensions: [" << tunnel.dimensions[0] << ", " 
             << tunnel.dimensions[1] << ", " << tunnel.dimensions[2] << "]" << std::endl;
        file << "  Min energy: " << tunnel.min_energy << " kJ/mol" << std::endl;
        file << "  Clusters: ";
        for (int cid : tunnel.cluster_ids) {
            file << cid << " ";
        }
        file << std::endl;
        file << "  TS groups: " << tunnel.tsgroup_ids.size() << std::endl;
    }
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}

void OutputWriter::write_ts_data(const std::string& filename) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    const auto& ts_groups = ts_mgr_->ts_groups();
    const auto& tunnels = tunnel_mgr_->tunnels();

    std::set<int> tunnel_tsgroups;
    for (const auto& tunnel : tunnels) {
        for (int tsgid : tunnel.tsgroup_ids) {
            tunnel_tsgroups.insert(tsgid);
        }
    }
    
    // Format: TSgroup_ID ceil(level*energy_step) 0 x-0.5 y-0.5 z-0.5 (matching MATLAB)
    // MATLAB: [TS_groupID ceil(TS_levels*energy_step) TS_zeroes TS_coords-0.5]
    for (size_t group_idx = 0; group_idx < ts_groups.size(); group_idx++) {
        if (!tunnel_tsgroups.empty() && tunnel_tsgroups.count(static_cast<int>(group_idx)) == 0) {
            continue;  // Match MATLAB TS_data.out: only TS groups attached to tunnels/processes
        }
        const auto& group = ts_groups[group_idx];
        for (const auto& pt : group.points) {
            file << (group_idx + 1) << " "  // TSgroup_ID (1-indexed)
                 << std::ceil(pt.level * energy_step_) << " "  // Energy value, not level
                 << "0 "  // Zero column (MATLAB has this)
                 << (pt.x - 0.5) << " " 
                 << (pt.y - 0.5) << " "
                 << (pt.z - 0.5) << std::endl;
        }
    }
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}

void OutputWriter::write_breakthrough(const std::string& filename,
                                     const std::array<double, 3>& bt_energies) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    file << bt_energies[0] << " " << bt_energies[1] << " " << bt_energies[2] << std::endl;
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}

void OutputWriter::write_energy_volume(const std::string& filename,
                                      const std::vector<double>& e_vol) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    // Write as single line of values (MATLAB format)
    for (size_t i = 0; i < e_vol.size(); i++) {
        if (i > 0) file << "    ";
        file << static_cast<int>(e_vol[i]);
    }
    file << std::endl;
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}
