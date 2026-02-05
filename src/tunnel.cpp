#include "tunnel.h"
#include <set>
#include <map>
#include <iostream>

TunnelManager::TunnelManager(std::shared_ptr<Grid> grid,
                            std::shared_ptr<ClusterManager> cluster_mgr,
                            std::shared_ptr<TransitionStateManager> ts_mgr)
    : grid_(grid), cluster_mgr_(cluster_mgr), ts_mgr_(ts_mgr) {
}

void TunnelManager::organize_tunnels() {
    // Group TS groups by cluster pairs to identify tunnels
    std::map<std::pair<int,int>, std::vector<int>> cluster_pair_to_tsgroups;
    
    const auto& ts_groups = ts_mgr_->ts_groups();
    
    for (size_t i = 0; i < ts_groups.size(); i++) {
        int c1 = std::min(ts_groups[i].cluster1_id, ts_groups[i].cluster2_id);
        int c2 = std::max(ts_groups[i].cluster1_id, ts_groups[i].cluster2_id);
        
        cluster_pair_to_tsgroups[{c1, c2}].push_back(i);
    }
    
    // Create tunnels from cluster pairs
    int tunnel_id = 1;
    for (const auto& pair : cluster_pair_to_tsgroups) {
        Tunnel tunnel;
        tunnel.id = tunnel_id++;
        tunnel.cluster_ids = {pair.first.first, pair.first.second};
        tunnel.tsgroup_ids = pair.second;
        
        // Find minimum energy among all TS groups in tunnel
        tunnel.min_energy = 1e10;
        for (int ts_group_idx : pair.second) {
            if (ts_groups[ts_group_idx].min_energy < tunnel.min_energy) {
                tunnel.min_energy = ts_groups[ts_group_idx].min_energy;
            }
        }
        
        // Determine tunnel dimensions (simplified)
        tunnel.dimensions = {1, 1, 1};  // Assume 3D by default
        
        tunnels_.push_back(tunnel);
    }
    
    std::cout << "Found " << tunnels_.size() << " tunnels" << std::endl;
}

void TunnelManager::generate_processes(std::vector<Process>& processes) {
    // Generate bidirectional processes for each tunnel
    const auto& ts_groups = ts_mgr_->ts_groups();
    
    for (const auto& tunnel : tunnels_) {
        for (size_t ts_idx = 0; ts_idx < tunnel.tsgroup_ids.size(); ts_idx++) {
            int tsgroup_id = tunnel.tsgroup_ids[ts_idx];
            const TSGroup& tsgroup = ts_groups[tsgroup_id];
            
            // Forward process: cluster1 -> cluster2
            Process fwd;
            fwd.from_basis = tsgroup.cluster1_id - 1;  // 0-indexed
            fwd.to_basis = tsgroup.cluster2_id - 1;
            fwd.rate = 0.0;  // Will be calculated later with temperature
            fwd.tunnel_id = tunnel.id;
            fwd.tsgroup_id = tsgroup_id;
            
            // Get cross vector
            const Cluster& c1 = cluster_mgr_->get_cluster(tsgroup.cluster1_id);
            const Cluster& c2 = cluster_mgr_->get_cluster(tsgroup.cluster2_id);
            
            // Simplified: set cross to zero
            fwd.cross = CrossVector(0, 0, 0);
            fwd.ts_cross = CrossVector(0, 0, 0);
            
            processes.push_back(fwd);
            
            // Reverse process: cluster2 -> cluster1
            Process rev;
            rev.from_basis = tsgroup.cluster2_id - 1;
            rev.to_basis = tsgroup.cluster1_id - 1;
            rev.rate = 0.0;
            rev.tunnel_id = tunnel.id;
            rev.tsgroup_id = tsgroup_id;
            rev.cross = CrossVector(0, 0, 0);
            rev.ts_cross = CrossVector(0, 0, 0);
            
            processes.push_back(rev);
        }
    }
    
    std::cout << "Generated " << processes.size() << " processes" << std::endl;
}

bool TunnelManager::check_tunnel_dimension(int dim, const std::vector<int>& cluster_ids) {
    // Check if tunnel crosses periodic boundary in given dimension
    // Simplified version
    return true;
}
