#include "cluster.h"
#include "pbc.h"
#include <queue>
#include <iostream>
#include <algorithm>

ClusterManager::ClusterManager(std::shared_ptr<Grid> grid)
    : grid_(grid), next_cluster_id_(1) {
}

void ClusterManager::initiate_clusters(int level) {
    // Find all points at current level that haven't been assigned to a cluster
    // Only create new cluster if point and all its neighbors are unassigned (minID_L == 0)
    // This matches the Octave logic: if sum(neighbor_id)==0
    int clusters_added = 0;
    int candidates_checked = 0;
    int candidates_skipped = 0;
    
    for (int z = 0; z < grid_->nz(); z++) {
        for (int y = 0; y < grid_->ny(); y++) {
            for (int x = 0; x < grid_->nx(); x++) {
                // Check if point is at current level AND hasn't been assigned yet
                // Changed from minID_C to minID_L to match Octave logic:
                // - minID_L tracks the level at which a point was first assigned
                // - minID_C tracks the cluster ID
                // - We check minID_L here because we want points never assigned at any level
                if (grid_->level_at(x, y, z) == level && 
                    grid_->minID_L(x, y, z) == 0) {
                    
                    candidates_checked++;
                    
                    // Check if all 6 neighbors have minID_L == 0
                    int ip, im, jp, jm, kp, km;
                    CrossVector cv;
                    PBC::apply(x, y, z, 
                              grid_->nx(), grid_->ny(), grid_->nz(),
                              ip, im, jp, jm, kp, km, cv);
                    
                    bool all_neighbors_unassigned = 
                        (grid_->minID_L(ip, y, z) == 0) &&
                        (grid_->minID_L(im, y, z) == 0) &&
                        (grid_->minID_L(x, jp, z) == 0) &&
                        (grid_->minID_L(x, jm, z) == 0) &&
                        (grid_->minID_L(x, y, kp) == 0) &&
                        (grid_->minID_L(x, y, km) == 0);
                    
                    if (!all_neighbors_unassigned) {
                        candidates_skipped++;
                        continue;  // Skip this point - it will be added during cluster growth
                    }
                    
                    // Start new cluster with flood fill
                    Cluster new_cluster;
                    new_cluster.id = next_cluster_id_++;
                    new_cluster.min_energy = grid_->energy_at(x, y, z);
                    
                    std::queue<Coord3D> queue;
                    queue.push(Coord3D(x, y, z));
                    grid_->minID_L(x, y, z) = level;  // Set level assignment
                    grid_->minID_C(x, y, z) = new_cluster.id;  // Set cluster assignment
                    // Initialize cross vector for first point
                    grid_->cross_i(x, y, z) = 0;
                    grid_->cross_j(x, y, z) = 0;
                    grid_->cross_k(x, y, z) = 0;
                    
                    int cluster_size = 0;
                    while (!queue.empty()) {
                        Coord3D curr = queue.front();
                        queue.pop();
                        
                        ClusterPoint pt;
                        pt.x = curr.x;
                        pt.y = curr.y;
                        pt.z = curr.z;
                        pt.level = level;
                        pt.boundary = 1;  // Initially all are boundary
                        pt.cross_i = grid_->cross_i(curr.x, curr.y, curr.z);
                        pt.cross_j = grid_->cross_j(curr.x, curr.y, curr.z);
                        pt.cross_k = grid_->cross_k(curr.x, curr.y, curr.z);
                        new_cluster.points.push_back(pt);
                        cluster_size++;
                        
                        // Update min energy
                        double e = grid_->energy_at(curr.x, curr.y, curr.z);
                        if (e < new_cluster.min_energy) {
                            new_cluster.min_energy = e;
                        }
                        
                        // Check 6-connected neighbors
                        int ip, im, jp, jm, kp, km;
                        CrossVector cv;
                        PBC::apply(curr.x, curr.y, curr.z, 
                                  grid_->nx(), grid_->ny(), grid_->nz(),
                                  ip, im, jp, jm, kp, km, cv);
                        
                        // Get current point's cross vector
                        int curr_cross_i = grid_->cross_i(curr.x, curr.y, curr.z);
                        int curr_cross_j = grid_->cross_j(curr.x, curr.y, curr.z);
                        int curr_cross_k = grid_->cross_k(curr.x, curr.y, curr.z);
                        
                        // Check neighbors with proper cross vector assignment
                        // cv.vals indices: 0=x, 1=ip, 2=im, 3=y, 4=jp, 5=jm, 6=z, 7=kp, 8=km
                        struct NeighborInfo {
                            Coord3D coord;
                            int cross_i, cross_j, cross_k;
                        };
                        
                        std::vector<NeighborInfo> neighbors = {
                            {Coord3D(ip, curr.y, curr.z), curr_cross_i + cv.vals[1], curr_cross_j + cv.vals[3], curr_cross_k + cv.vals[6]},  // ip
                            {Coord3D(im, curr.y, curr.z), curr_cross_i + cv.vals[2], curr_cross_j + cv.vals[3], curr_cross_k + cv.vals[6]},  // im
                            {Coord3D(curr.x, jp, curr.z), curr_cross_i + cv.vals[0], curr_cross_j + cv.vals[4], curr_cross_k + cv.vals[6]},  // jp
                            {Coord3D(curr.x, jm, curr.z), curr_cross_i + cv.vals[0], curr_cross_j + cv.vals[5], curr_cross_k + cv.vals[6]},  // jm
                            {Coord3D(curr.x, curr.y, kp), curr_cross_i + cv.vals[0], curr_cross_j + cv.vals[3], curr_cross_k + cv.vals[7]},  // kp
                            {Coord3D(curr.x, curr.y, km), curr_cross_i + cv.vals[0], curr_cross_j + cv.vals[3], curr_cross_k + cv.vals[8]}   // km
                        };
                        
                        for (const auto& nb_info : neighbors) {
                            const auto& nb = nb_info.coord;
                            if (grid_->level_at(nb.x, nb.y, nb.z) == level &&
                                grid_->minID_L(nb.x, nb.y, nb.z) == 0) {
                                grid_->minID_L(nb.x, nb.y, nb.z) = level;  // Set level
                                grid_->minID_C(nb.x, nb.y, nb.z) = new_cluster.id;  // Set cluster
                                // Set cross vector for neighbor
                                grid_->cross_i(nb.x, nb.y, nb.z) = nb_info.cross_i;
                                grid_->cross_j(nb.x, nb.y, nb.z) = nb_info.cross_j;
                                grid_->cross_k(nb.x, nb.y, nb.z) = nb_info.cross_k;
                                queue.push(nb);
                            }
                        }
                    }
                    
                    if (cluster_size > 0) {
                        clusters_.push_back(new_cluster);
                        init_merge_group(new_cluster.id);  // Initialize merge group
                        clusters_added++;
                    }
                }
            }
        }
    }
    if (clusters_added > 0 || candidates_checked > 0) {
        std::cout << "  Initiated " << clusters_added << " new cluster(s) at this level" << std::endl;
        if (candidates_checked > 0) {
            std::cout << "    (checked " << candidates_checked << " candidates, skipped " << candidates_skipped << ")" << std::endl;
        }
    }
}

void ClusterManager::grow_clusters(int level, std::vector<TSPoint>& ts_list,
                                   std::vector<std::array<int,3>>& tunnel_list,
                                   std::vector<int>& tunnel_cluster,
                                   std::vector<std::array<int,3>>& tunnel_cluster_dim,
                                   double energy_step) {
    // Grow each existing cluster
    for (auto& cluster : clusters_) {
        if (cluster.id == 0) continue;  // Skip removed clusters
        
        std::vector<ClusterPoint> new_points;
        
        // Check boundary points
        for (auto& pt : cluster.points) {
            if (pt.boundary == 0) continue;
            
            bool still_boundary = false;
            int ip, im, jp, jm, kp, km;
            CrossVector cv;
            PBC::apply(pt.x, pt.y, pt.z, 
                      grid_->nx(), grid_->ny(), grid_->nz(),
                      ip, im, jp, jm, kp, km, cv);
            
            // Neighbor info with cross vectors
            struct NeighborInfo {
                Coord3D coord;
                int cross_i, cross_j, cross_k;
            };
            
            std::vector<NeighborInfo> neighbors = {
                {Coord3D(ip, pt.y, pt.z), pt.cross_i + cv.vals[1], pt.cross_j + cv.vals[3], pt.cross_k + cv.vals[6]},
                {Coord3D(im, pt.y, pt.z), pt.cross_i + cv.vals[2], pt.cross_j + cv.vals[3], pt.cross_k + cv.vals[6]},
                {Coord3D(pt.x, jp, pt.z), pt.cross_i + cv.vals[0], pt.cross_j + cv.vals[4], pt.cross_k + cv.vals[6]},
                {Coord3D(pt.x, jm, pt.z), pt.cross_i + cv.vals[0], pt.cross_j + cv.vals[5], pt.cross_k + cv.vals[6]},
                {Coord3D(pt.x, pt.y, kp), pt.cross_i + cv.vals[0], pt.cross_j + cv.vals[3], pt.cross_k + cv.vals[7]},
                {Coord3D(pt.x, pt.y, km), pt.cross_i + cv.vals[0], pt.cross_j + cv.vals[3], pt.cross_k + cv.vals[8]}
            };
            
            for (const auto& nb_info : neighbors) {
                const auto& nb = nb_info.coord;
                int nb_level = grid_->level_at(nb.x, nb.y, nb.z);
                int nb_cluster = grid_->minID_C(nb.x, nb.y, nb.z);
                
                if (nb_level == level && nb_cluster == 0) {
                    // Check if minID_L is also 0 (not yet assigned at any level)
                    // This matches Octave: only grow into completely unassigned points
                    if (grid_->minID_L(nb.x, nb.y, nb.z) == 0) {
                        // Add to cluster
                        grid_->minID_L(nb.x, nb.y, nb.z) = level;  // Set level
                        grid_->minID_C(nb.x, nb.y, nb.z) = cluster.id;  // Set cluster
                        grid_->cross_i(nb.x, nb.y, nb.z) = nb_info.cross_i;
                        grid_->cross_j(nb.x, nb.y, nb.z) = nb_info.cross_j;
                        grid_->cross_k(nb.x, nb.y, nb.z) = nb_info.cross_k;
                        
                        ClusterPoint new_pt;
                        new_pt.x = nb.x;
                        new_pt.y = nb.y;
                        new_pt.z = nb.z;
                        new_pt.level = level;
                        new_pt.boundary = 1;
                        new_pt.cross_i = nb_info.cross_i;
                        new_pt.cross_j = nb_info.cross_j;
                        new_pt.cross_k = nb_info.cross_k;
                        new_points.push_back(new_pt);
                        
                        still_boundary = true;
                        
                        // Update cluster min energy
                        double e = grid_->energy_at(nb.x, nb.y, nb.z);
                        if (e < cluster.min_energy) {
                            cluster.min_energy = e;
                        }
                    }
                }
                else if (nb_cluster == cluster.id) {
                    // Same cluster - check for tunnel (different cross vectors)
                    int idiff = nb_info.cross_i - grid_->cross_i(nb.x, nb.y, nb.z);
                    int jdiff = nb_info.cross_j - grid_->cross_j(nb.x, nb.y, nb.z);
                    int kdiff = nb_info.cross_k - grid_->cross_k(nb.x, nb.y, nb.z);
                    
                    if (idiff != 0 || jdiff != 0 || kdiff != 0) {
                        // Found a tunnel - cluster wraps around periodic boundary
                        tunnel_list.push_back({idiff, jdiff, kdiff});
                        
                        // Add to tunnel_cluster if not already there
                        bool found = false;
                        for (int tc : tunnel_cluster) {
                            if (tc == cluster.id) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            tunnel_cluster.push_back(cluster.id);
                            tunnel_cluster_dim.push_back({idiff, jdiff, kdiff});
                        }
                    }
                }
                else if (nb_level == level && nb_cluster > 0 && nb_cluster != cluster.id) {
                    // Different cluster - check if should merge or create TS
                    double nb_energy = grid_->energy_at(nb.x, nb.y, nb.z);
                    double pt_energy = grid_->energy_at(pt.x, pt.y, pt.z);
                    
                    // Get neighbor cluster min energy
                    Cluster& nb_clust = get_cluster(nb_cluster);
                    double dE_current = nb_energy - cluster.min_energy;
                    double dE_connect = nb_energy - nb_clust.min_energy;
                    double dE = std::min(dE_current, dE_connect);
                    
                    // Check cross vector difference for tunnel detection
                    int idiff = nb_info.cross_i - grid_->cross_i(nb.x, nb.y, nb.z);
                    int jdiff = nb_info.cross_j - grid_->cross_j(nb.x, nb.y, nb.z);
                    int kdiff = nb_info.cross_k - grid_->cross_k(nb.x, nb.y, nb.z);
                    
                    // If cross vectors differ, check for tunnel
                    if (idiff != 0 || jdiff != 0 || kdiff != 0) {
                        tunnel_list.push_back({idiff, jdiff, kdiff});
                        
                        bool found = false;
                        for (int tc : tunnel_cluster) {
                            if (tc == cluster.id) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            tunnel_cluster.push_back(cluster.id);
                            tunnel_cluster_dim.push_back({idiff, jdiff, kdiff});
                        }
                    }
                    
                    if (dE < energy_step) {
                        // Merge clusters - barrier too small
                        merge_clusters(cluster.id, nb_cluster, idiff, jdiff, kdiff, ts_list);
                    } else {
                        // Create transition state
                        // Choose higher energy point as TS
                        int ts_x, ts_y, ts_z;
                        if (nb_energy >= pt_energy) {
                            ts_x = nb.x; ts_y = nb.y; ts_z = nb.z;
                        } else {
                            ts_x = pt.x; ts_y = pt.y; ts_z = pt.z;
                        }
                        
                        // Only add if not already marked as TS
                        if (grid_->ts_matrix(ts_x, ts_y, ts_z) == 0) {
                            TSPoint ts;
                            ts.x = ts_x;
                            ts.y = ts_y;
                            ts.z = ts_z;
                            ts.level = level;
                            ts.energy = grid_->energy_at(ts_x, ts_y, ts_z);
                            ts.cluster1_id = std::min(cluster.id, nb_cluster);
                            ts.cluster2_id = std::max(cluster.id, nb_cluster);
                            ts.cross_i = nb_info.cross_i;
                            ts.cross_j = nb_info.cross_j;
                            ts.cross_k = nb_info.cross_k;
                            ts_list.push_back(ts);
                            
                            grid_->ts_matrix(ts_x, ts_y, ts_z) = 1;
                        }
                    }
                    
                    still_boundary = true;
                }
                else if (nb_level > level) {
                    still_boundary = true;
                }
            }
            
            pt.boundary = still_boundary ? 1 : 0;
        }
        
        // Add new points to cluster
        cluster.points.insert(cluster.points.end(), 
                             new_points.begin(), new_points.end());
    }
}

void ClusterManager::init_merge_group(int cluster_id) {
    // Check if cluster already in a merge group
    for (const auto& group : merge_groups_) {
        for (int cid : group) {
            if (cid == cluster_id) {
                return;  // Already in a group
            }
        }
    }
    // Create new merge group with just this cluster
    merge_groups_.push_back({cluster_id});
}

int ClusterManager::find_merge_group(int cluster_id) {
    for (size_t i = 0; i < merge_groups_.size(); i++) {
        for (int cid : merge_groups_[i]) {
            if (cid == cluster_id) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void ClusterManager::merge_clusters(int cluster1_id, int cluster2_id, 
                                    int idiff, int jdiff, int kdiff,
                                    std::vector<TSPoint>& ts_list) {
    std::cout << "    Merging clusters " << cluster2_id << " into " << cluster1_id 
              << " (cross diff: " << idiff << "," << jdiff << "," << kdiff << ")" << std::endl;
    
    // Find the clusters
    Cluster* c1 = nullptr;
    Cluster* c2 = nullptr;
    for (auto& cluster : clusters_) {
        if (cluster.id == cluster1_id) c1 = &cluster;
        if (cluster.id == cluster2_id) c2 = &cluster;
    }
    
    if (!c1 || !c2) {
        std::cerr << "Error: Clusters not found for merge" << std::endl;
        return;
    }
    
    // Find merge groups
    int group1 = find_merge_group(cluster1_id);
    int group2 = find_merge_group(cluster2_id);
    
    // Check if clusters are already in same merge group
    bool same_group = (group1 >= 0 && group1 == group2);
    
    // If cross vectors differ and not same group, need to update cross vectors
    if ((idiff != 0 || jdiff != 0 || kdiff != 0) && !same_group) {
        // Update cross vectors for all points in cluster2's merge group
        std::vector<int> clusters_to_update;
        if (group2 >= 0) {
            clusters_to_update = merge_groups_[group2];
        } else {
            clusters_to_update = {cluster2_id};
        }
        
        for (int cid : clusters_to_update) {
            for (auto& cluster : clusters_) {
                if (cluster.id == cid) {
                    for (auto& pt : cluster.points) {
                        grid_->cross_i(pt.x, pt.y, pt.z) += idiff;
                        grid_->cross_j(pt.x, pt.y, pt.z) += jdiff;
                        grid_->cross_k(pt.x, pt.y, pt.z) += kdiff;
                        pt.cross_i += idiff;
                        pt.cross_j += jdiff;
                        pt.cross_k += kdiff;
                    }
                    break;
                }
            }
        }
    }
    
    // Merge the merge groups if different
    if (group1 >= 0 && group2 >= 0 && group1 != group2) {
        // Merge group2 into group1
        merge_groups_[group1].insert(merge_groups_[group1].end(),
                                     merge_groups_[group2].begin(),
                                     merge_groups_[group2].end());
        // Remove group2
        merge_groups_.erase(merge_groups_.begin() + group2);
    } else if (group1 >= 0 && group2 < 0) {
        // Add cluster2 to group1
        merge_groups_[group1].push_back(cluster2_id);
    } else if (group1 < 0 && group2 >= 0) {
        // Add cluster1 to group2
        merge_groups_[group2].push_back(cluster1_id);
    } else {
        // Neither in a group, create new merged group
        merge_groups_.push_back({cluster1_id, cluster2_id});
    }
    
    // Transfer all points from cluster2 to cluster1
    c1->points.insert(c1->points.end(), c2->points.begin(), c2->points.end());
    
    // Update min energy
    if (c2->min_energy < c1->min_energy) {
        c1->min_energy = c2->min_energy;
    }
    
    // Update minID_matrix for all cluster2 points
    for (const auto& pt : c2->points) {
        grid_->minID_C(pt.x, pt.y, pt.z) = cluster1_id;
    }
    
    // Mark cluster2 as removed
    c2->id = 0;
    c2->points.clear();
    
    // Update TS list - change cluster2_id references to cluster1_id
    for (auto& ts : ts_list) {
        if (ts.cluster1_id == cluster2_id) {
            ts.cluster1_id = cluster1_id;
        }
        if (ts.cluster2_id == cluster2_id) {
            ts.cluster2_id = cluster1_id;
        }
    }
    
    // Remove TS points that are now internal (both clusters are the same)
    ts_list.erase(
        std::remove_if(ts_list.begin(), ts_list.end(),
            [this](const TSPoint& ts) {
                // Remove if both clusters are same or if TS point is now internal
                if (ts.cluster1_id == ts.cluster2_id) {
                    grid_->ts_matrix(ts.x, ts.y, ts.z) = 0;
                    return true;
                }
                return false;
            }),
        ts_list.end()
    );
}

Cluster& ClusterManager::get_cluster(int id) {
    for (auto& cluster : clusters_) {
        if (cluster.id == id) {
            return cluster;
        }
    }
    // Should not happen in normal operation - indicates programming error
    std::cerr << "Warning: Cluster ID " << id << " not found" << std::endl;
    static Cluster dummy;
    return dummy;
}

const Cluster& ClusterManager::get_cluster(int id) const {
    for (const auto& cluster : clusters_) {
        if (cluster.id == id) {
            return cluster;
        }
    }
    std::cerr << "Warning: Cluster ID " << id << " not found" << std::endl;
    static Cluster dummy;
    return dummy;
}
