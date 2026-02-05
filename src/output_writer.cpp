#include "output_writer.h"
#include <fstream>
#include <iostream>
#include <iomanip>

OutputWriter::OutputWriter(std::shared_ptr<ClusterManager> cluster_mgr,
                          std::shared_ptr<TunnelManager> tunnel_mgr,
                          std::shared_ptr<TransitionStateManager> ts_mgr)
    : cluster_mgr_(cluster_mgr), tunnel_mgr_(tunnel_mgr), ts_mgr_(ts_mgr) {
}

void OutputWriter::write_basis(const std::string& filename) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write " << filename << std::endl;
        return;
    }
    
    const auto& clusters = cluster_mgr_->clusters();
    
    for (const auto& cluster : clusters) {
        if (cluster.id == 0) continue;  // Skip removed clusters
        
        // Find the minimum energy point in cluster
        if (!cluster.points.empty()) {
            const auto& min_pt = cluster.points[0];
            file << min_pt.x << " " << min_pt.y << " " << min_pt.z << " "
                 << cluster.tunnel_id << std::endl;
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
    
    for (const auto& proc : processes) {
        file << proc.from_basis << " " 
             << proc.to_basis << " "
             << proc.rate << " "
             << proc.cross.i << " " << proc.cross.j << " " << proc.cross.k << " "
             << proc.ts_cross.i << " " << proc.ts_cross.j << " " << proc.ts_cross.k << " "
             << proc.tunnel_id << " "
             << proc.tsgroup_id << std::endl;
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
    
    for (const auto& group : ts_groups) {
        for (const auto& pt : group.points) {
            file << pt.x << " " << pt.y << " " << pt.z << " " 
                 << pt.energy << std::endl;
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
    
    for (size_t i = 0; i < e_vol.size(); i++) {
        file << i << " " << e_vol[i] << std::endl;
    }
    
    file.close();
    std::cout << "Wrote " << filename << std::endl;
}
