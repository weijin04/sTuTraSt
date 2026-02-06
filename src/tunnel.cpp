#include "tunnel.h"
#include <set>
#include <map>
#include <algorithm>
#include <iostream>
#include <limits>

TunnelManager::TunnelManager(std::shared_ptr<Grid> grid,
                            std::shared_ptr<ClusterManager> cluster_mgr,
                            std::shared_ptr<TransitionStateManager> ts_mgr)
    : grid_(grid), cluster_mgr_(cluster_mgr), ts_mgr_(ts_mgr) {
}

void TunnelManager::organize_tunnels(const std::vector<int>& tunnel_cluster,
                                      const std::vector<std::array<int,3>>& tunnel_cluster_dim) {
    // Use merge groups to organize tunnels (matches MATLAB approach)
    // Each merge group becomes one tunnel, with all clusters in group sharing tunnel_id
    
    const auto& merge_groups = cluster_mgr_->merge_groups();
    const auto& ts_groups = ts_mgr_->ts_groups();
    const auto& clusters = cluster_mgr_->clusters();
    
    // Build set of cluster IDs that have TS connections (tunnel_cluster equivalent)
    std::set<int> clusters_with_ts;
    for (const auto& ts_group : ts_groups) {
        clusters_with_ts.insert(ts_group.cluster1_id);
        clusters_with_ts.insert(ts_group.cluster2_id);
    }
    
    // Create tunnels from merge groups that contain clusters with TS
    int tunnel_id = 1;
    // Track which TSgroups have been processed to avoid duplicates
    std::set<size_t> processed_tsgroups;
    
    for (const auto& merge_group : merge_groups) {
        // Check if any cluster in this merge group has TS connections
        bool has_tunnel = false;
        for (int cluster_id : merge_group) {
            if (clusters_with_ts.count(cluster_id) > 0) {
                has_tunnel = true;
                break;
            }
        }
        
        if (!has_tunnel) continue;
        
        // Create tunnel for this merge group
        Tunnel tunnel;
        tunnel.id = tunnel_id;
        tunnel.cluster_ids = merge_group;
        
        // Find all TS groups connecting clusters in this merge group
        for (size_t i = 0; i < ts_groups.size(); i++) {
            // Skip if already processed by another merge group
            if (processed_tsgroups.find(i) != processed_tsgroups.end()) {
                continue;
            }
            
            // Check if both clusters are in this merge group
            bool c1_in_group = std::find(merge_group.begin(), merge_group.end(), 
                                        ts_groups[i].cluster1_id) != merge_group.end();
            bool c2_in_group = std::find(merge_group.begin(), merge_group.end(), 
                                        ts_groups[i].cluster2_id) != merge_group.end();
            
            // TS connects clusters within group OR connects to external cluster
            // But only add once (deduplication via processed_tsgroups)
            if (c1_in_group || c2_in_group) {
                tunnel.tsgroup_ids.push_back(i);
                processed_tsgroups.insert(i);
            }
        }
        
        // Assign same tunnel_id to all clusters in merge group
        for (int cluster_id : merge_group) {
            // Only assign if cluster still exists (id != 0)
            if (cluster_id > 0 && cluster_id <= static_cast<int>(clusters.size())) {
                auto& cluster = cluster_mgr_->get_cluster(cluster_id);
                if (cluster.id != 0) {
                    cluster.tunnel_id = tunnel_id;
                }
            }
        }
        
        // Find minimum energy among all TS groups in tunnel
        tunnel.min_energy = std::numeric_limits<double>::max();
        for (int ts_group_idx : tunnel.tsgroup_ids) {
            if (ts_groups[ts_group_idx].min_energy < tunnel.min_energy) {
                tunnel.min_energy = ts_groups[ts_group_idx].min_energy;
            }
        }
        
        // Determine tunnel dimensions (simplified)
        tunnel.dimensions = {1, 1, 1};  // Assume 3D by default
        
        tunnels_.push_back(tunnel);
        tunnel_id++;
    }
    
    std::cout << "Found " << tunnels_.size() << " tunnels from " 
              << merge_groups.size() << " merge groups" << std::endl;
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
            fwd.from_cluster_orig = tsgroup.cluster1_id;  // Original cluster ID (1-indexed)
            fwd.to_cluster_orig = tsgroup.cluster2_id;
            
            // Get clusters and their minimum positions
            const Cluster& c1 = cluster_mgr_->get_cluster(tsgroup.cluster1_id);
            const Cluster& c2 = cluster_mgr_->get_cluster(tsgroup.cluster2_id);
            
            // Find minimum energy points in each cluster
            Coord3D min1 = get_cluster_min_coord(c1);
            Coord3D min2 = get_cluster_min_coord(c2);
            
            // Calculate cross vectors considering periodic boundaries
            // If clusters are on opposite sides of boundary, we need to account for wrapping
            fwd.cross = calculate_cross_vector(min1, min2);
            fwd.ts_cross = calculate_cross_vector(min1, tsgroup.min_coord);
            
            processes.push_back(fwd);
            
            // Reverse process: cluster2 -> cluster1
            Process rev;
            rev.from_basis = tsgroup.cluster2_id - 1;
            rev.to_basis = tsgroup.cluster1_id - 1;
            rev.rate = 0.0;
            rev.tunnel_id = tunnel.id;
            rev.tsgroup_id = tsgroup_id;
            rev.from_cluster_orig = tsgroup.cluster2_id;
            rev.to_cluster_orig = tsgroup.cluster1_id;
            
            // Reverse direction cross vectors
            CrossVector neg_cross = fwd.cross;
            neg_cross.i = -neg_cross.i;
            neg_cross.j = -neg_cross.j;
            neg_cross.k = -neg_cross.k;
            rev.cross = neg_cross;
            
            CrossVector rev_ts_cross = fwd.ts_cross;
            rev_ts_cross.i = fwd.ts_cross.i - fwd.cross.i;
            rev_ts_cross.j = fwd.ts_cross.j - fwd.cross.j;
            rev_ts_cross.k = fwd.ts_cross.k - fwd.cross.k;
            rev.ts_cross = rev_ts_cross;
            
            processes.push_back(rev);
        }
    }
    
    std::cout << "Generated " << processes.size() << " processes" << std::endl;
}

Coord3D TunnelManager::get_cluster_min_coord(const Cluster& cluster) {
    // Find the point with minimum energy in the cluster
    if (cluster.points.empty()) {
        return Coord3D(0, 0, 0);
    }
    
    int min_idx = 0;
    double min_e = std::numeric_limits<double>::max();
    for (size_t i = 0; i < cluster.points.size(); i++) {
        const auto& pt = cluster.points[i];
        double e = grid_->energy_at(pt.x, pt.y, pt.z);
        if (e < min_e) {
            min_e = e;
            min_idx = i;
        }
    }
    
    const auto& min_pt = cluster.points[min_idx];
    return Coord3D(min_pt.x, min_pt.y, min_pt.z);
}

CrossVector TunnelManager::calculate_cross_vector(const Coord3D& start, const Coord3D& end) {
    // Calculate the shortest path from start to end considering periodic boundaries
    // This determines how many times we cross boundaries in each direction
    
    int dx = end.x - start.x;
    int dy = end.y - start.y;
    int dz = end.z - start.z;
    
    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();
    
    // Find shortest path considering periodic wrapping
    // If distance is more than half the box, it's shorter to go the other way
    int cross_x = 0, cross_y = 0, cross_z = 0;
    
    if (dx > nx / 2) {
        cross_x = -1;  // Cross backward
    } else if (dx < -nx / 2) {
        cross_x = 1;   // Cross forward
    }
    
    if (dy > ny / 2) {
        cross_y = -1;
    } else if (dy < -ny / 2) {
        cross_y = 1;
    }
    
    if (dz > nz / 2) {
        cross_z = -1;
    } else if (dz < -nz / 2) {
        cross_z = 1;
    }
    
    return CrossVector(cross_x, cross_y, cross_z);
}

bool TunnelManager::check_tunnel_dimension(int dim, const std::vector<int>& cluster_ids) {
    // Check if tunnel crosses periodic boundary in given dimension
    // Simplified version
    return true;
}
