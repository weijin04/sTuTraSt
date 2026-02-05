#include "output_writer.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>

OutputWriter::OutputWriter(std::shared_ptr<ClusterManager> cluster_mgr,
                          std::shared_ptr<TunnelManager> tunnel_mgr,
                          std::shared_ptr<TransitionStateManager> ts_mgr)
    : cluster_mgr_(cluster_mgr), tunnel_mgr_(tunnel_mgr), ts_mgr_(ts_mgr) {
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
            // Find minimum energy point
            const auto& min_pt = cluster.points[0];
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
    for (const auto& proc : processes) {
        file << std::setw(23) << proc.from_basis << " " 
             << std::setw(23) << proc.to_basis << " "
             << std::setw(23) << proc.rate << " "
             << std::setw(23) << proc.cross.i << " " 
             << std::setw(23) << proc.cross.j << " " 
             << std::setw(23) << proc.cross.k << " "
             << std::setw(23) << proc.ts_cross.i << " " 
             << std::setw(23) << proc.ts_cross.j << " " 
             << std::setw(23) << proc.ts_cross.k << " "
             << std::setw(23) << proc.tunnel_id << " "
             << std::setw(23) << proc.tsgroup_id << " "
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
    
    // Format: TSgroup_ID level 0 x-0.5 y-0.5 z-0.5 (matching MATLAB)
    for (size_t group_idx = 0; group_idx < ts_groups.size(); group_idx++) {
        const auto& group = ts_groups[group_idx];
        for (const auto& pt : group.points) {
            file << (group_idx + 1) << " "  // TSgroup_ID (1-indexed)
                 << pt.level << " "
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
