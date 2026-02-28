#include "cluster.h"
#include "pbc.h"
#include <queue>
#include <iostream>
#include <algorithm>
#include <map>
#include <cstdlib>
#include <string>

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
    
    // Debug: count total points at this level
    int total_at_level = 0;
    for (int z = 0; z < grid_->nz(); z++) {
        for (int y = 0; y < grid_->ny(); y++) {
            for (int x = 0; x < grid_->nx(); x++) {
                if (grid_->level_at(x, y, z) == level) {
                    total_at_level++;
                }
            }
        }
    }
    std::cout << "  Total points at level " << level << ": " << total_at_level << std::endl;
    
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
                            // Match MATLAB logic: check minID_L==0 FIRST, then level
                            // If unassigned but different level, don't add to cluster (boundary)
                            // If unassigned and same level, add to cluster
                            if (grid_->minID_L(nb.x, nb.y, nb.z) == 0) {
                                if (grid_->level_at(nb.x, nb.y, nb.z) == level) {
                                    grid_->minID_L(nb.x, nb.y, nb.z) = level;  // Set level
                                    grid_->minID_C(nb.x, nb.y, nb.z) = new_cluster.id;  // Set cluster
                                    // Set cross vector for neighbor
                                    grid_->cross_i(nb.x, nb.y, nb.z) = nb_info.cross_i;
                                    grid_->cross_j(nb.x, nb.y, nb.z) = nb_info.cross_j;
                                    grid_->cross_k(nb.x, nb.y, nb.z) = nb_info.cross_k;
                                    queue.push(nb);
                                }
                                // else: neighbor is unassigned but at different level
                                // This marks current point as boundary (matches MATLAB line 44, 61, etc.)
                            }
                        }
                    }
                    
                    if (cluster_size > 0) {
                        clusters_.push_back(new_cluster);
                        init_merge_group(new_cluster.id);  // Initialize merge group
                        clusters_added++;
                        std::cout << "    Cluster " << new_cluster.id << ": " << cluster_size << " points" << std::endl;
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
                                   double energy_step,
                                   std::vector<TSPoint>* ts_list_all) {
    bool trace_local = false;
    if (const char* env = std::getenv("TUTRAST_TRACE_QUPX21"); env && std::string(env) == "1") {
        trace_local = true;
    }
    auto in_trace_box = [&](int x, int y, int z) {
        // MATLAB 1-based local mismatch patch around level 21, converted to C++ 0-based.
        bool box1 = (x >= 27 && x <= 33 && y >= 9 && y <= 12 && z >= 68 && z <= 72);
        bool box2 = (x >= 120 && x <= 123 && y >= 13 && y <= 15 && z >= 84 && z <= 88);
        return box1 || box2;
    };
    auto trace_pair = [&](const char* tag, const ClusterPoint& pt, const Coord3D& nb,
                          double pt_energy, double nb_energy, double dE,
                          int cid1, int cid2) {
        if (!trace_local || level != 21) return;
        if (!(in_trace_box(pt.x, pt.y, pt.z) || in_trace_box(nb.x, nb.y, nb.z))) return;
        std::cout << "[TRACE21] " << tag
                  << " host=(" << (pt.x + 1) << "," << (pt.y + 1) << "," << (pt.z + 1) << ")"
                  << " nb=(" << (nb.x + 1) << "," << (nb.y + 1) << "," << (nb.z + 1) << ")"
                  << " cid=(" << cid1 << "," << cid2 << ")"
                  << " E=(" << pt_energy << "," << nb_energy << ")"
                  << " dE=" << dE << std::endl;
    };
    // MATLAB uses a per-cluster `filled` flag: clusters that fail to grow in one pass
    // are not revisited later in the same level.
    std::vector<int> filled(clusters_.size(), 0);
    for (size_t ci = 0; ci < clusters_.size(); ++ci) {
        if (clusters_[ci].id == 0) filled[ci] = 1;
    }
    while (true) {
        bool any_growth = false;
        bool all_filled = true;
        
        // Grow each existing cluster
        for (size_t ci = 0; ci < clusters_.size(); ++ci) {
            auto& cluster = clusters_[ci];
            if (cluster.id == 0) {
                filled[ci] = 1;
                continue;  // Skip removed clusters
            }
            if (filled[ci] != 0) {
                continue;
            }
            all_filled = false;
            
            std::vector<ClusterPoint> new_points;

        // Check boundary points (use index-based loop and copy point data
        // because merge_clusters can insert into cluster.points, causing reallocation)
        size_t n_points = cluster.points.size();
        for (size_t pi = 0; pi < n_points; pi++) {
            // Copy point data - do NOT hold a reference, as merge_clusters
            // may reallocate cluster.points via insert()
            ClusterPoint pt = cluster.points[pi];
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
                int delta_i, delta_j, delta_k;
            };
            
            // MATLAB neighbor order: +k, -k, +j, -j, +i, -i
            // (from triple loop for l=1:3, m=4:6, n=7:9 with checkneighbor filter)
            std::vector<NeighborInfo> neighbors = {
                {Coord3D(pt.x, pt.y, kp), cv.vals[0], cv.vals[3], cv.vals[7]},
                {Coord3D(pt.x, pt.y, km), cv.vals[0], cv.vals[3], cv.vals[8]},
                {Coord3D(pt.x, jp, pt.z), cv.vals[0], cv.vals[4], cv.vals[6]},
                {Coord3D(pt.x, jm, pt.z), cv.vals[0], cv.vals[5], cv.vals[6]},
                {Coord3D(ip, pt.y, pt.z), cv.vals[1], cv.vals[3], cv.vals[6]},
                {Coord3D(im, pt.y, pt.z), cv.vals[2], cv.vals[3], cv.vals[6]}
            };
            
            for (const auto& nb_info : neighbors) {
                // MATLAB checks TS_matrix(i,j,k)==0 inside the neighbor loop.
                // If host becomes TS during this scan, remaining neighbors are skipped.
                if (grid_->ts_matrix(pt.x, pt.y, pt.z) != 0) {
                    continue;
                }

                const auto& nb = nb_info.coord;
                // MATLAB computes crossi/crossj/crossk from the live cross_matrix for each neighbor.
                // The host point cross vector can change mid-loop due to merges, so do not use cached pt.cross_*.
                int nb_cross_i = grid_->cross_i(pt.x, pt.y, pt.z) + nb_info.delta_i;
                int nb_cross_j = grid_->cross_j(pt.x, pt.y, pt.z) + nb_info.delta_j;
                int nb_cross_k = grid_->cross_k(pt.x, pt.y, pt.z) + nb_info.delta_k;
                int nb_level = grid_->level_at(nb.x, nb.y, nb.z);
                int nb_cluster = grid_->minID_C(nb.x, nb.y, nb.z);
                int nb_minID_L = grid_->minID_L(nb.x, nb.y, nb.z);

                if (nb_minID_L == 0) {
                    // Unassigned neighbor - check if same level to grow
                    if (nb_level == level) {
                        // Add to cluster
                        grid_->minID_L(nb.x, nb.y, nb.z) = level;
                        grid_->minID_C(nb.x, nb.y, nb.z) = cluster.id;
                        grid_->cross_i(nb.x, nb.y, nb.z) = nb_cross_i;
                        grid_->cross_j(nb.x, nb.y, nb.z) = nb_cross_j;
                        grid_->cross_k(nb.x, nb.y, nb.z) = nb_cross_k;

                        ClusterPoint new_pt;
                        new_pt.x = nb.x;
                        new_pt.y = nb.y;
                        new_pt.z = nb.z;
                        new_pt.level = level;
                        new_pt.boundary = 1;
                        new_pt.cross_i = nb_cross_i;
                        new_pt.cross_j = nb_cross_j;
                        new_pt.cross_k = nb_cross_k;
                        new_points.push_back(new_pt);

                        // Update cluster min energy
                        double e = grid_->energy_at(nb.x, nb.y, nb.z);
                        if (e < cluster.min_energy) {
                            cluster.min_energy = e;
                        }
                    } else {
                        // Unassigned but different level - boundary
                        still_boundary = true;
                    }
                }
                else if (nb_cluster == cluster.id) {
                    // Same cluster - check for tunnel (different cross vectors)
                    int idiff = nb_cross_i - grid_->cross_i(nb.x, nb.y, nb.z);
                    int jdiff = nb_cross_j - grid_->cross_j(nb.x, nb.y, nb.z);
                    int kdiff = nb_cross_k - grid_->cross_k(nb.x, nb.y, nb.z);

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

                        // MATLAB also creates TS points for same-cluster PBC encounters
                        // (check_neighbors.m falls through to TS creation at lines 170-198)
                        // MATLAB line 184: both host AND neighbor must be unmarked
                        if (grid_->ts_matrix(nb.x, nb.y, nb.z) == 0 &&
                            grid_->ts_matrix(pt.x, pt.y, pt.z) == 0) {
                            double nb_energy = grid_->energy_at(nb.x, nb.y, nb.z);
                            double pt_energy = grid_->energy_at(pt.x, pt.y, pt.z);
                            trace_pair("same-cluster-ts", pt, nb, pt_energy, nb_energy, 0.0,
                                       cluster.id, cluster.id);

                            int ts_x, ts_y, ts_z;
                            if (nb_energy >= pt_energy) {
                                ts_x = nb.x; ts_y = nb.y; ts_z = nb.z;
                                // MATLAB: find neighbor row in C_connect (same cluster), set info(line,6)=1
                                for (auto& cp : cluster.points) {
                                    if (cp.x == ts_x && cp.y == ts_y && cp.z == ts_z) {
                                        cp.ts_flag = 1;
                                        break;
                                    }
                                }
                            } else {
                                ts_x = pt.x; ts_y = pt.y; ts_z = pt.z;
                                // MATLAB: set info(index_list,6)=1 on host point
                                cluster.points[pi].ts_flag = 1;
                            }

                            TSPoint ts;
                            ts.x = ts_x;
                            ts.y = ts_y;
                            ts.z = ts_z;
                            ts.energy = grid_->energy_at(ts_x, ts_y, ts_z);
                            ts.level = grid_->level_at(ts_x, ts_y, ts_z);
                            ts.cluster1_id = cluster.id;
                            ts.cluster2_id = cluster.id;
                            ts.cross_i = nb_cross_i;
                            ts.cross_j = nb_cross_j;
                            ts.cross_k = nb_cross_k;
                            ts.cross_diff_i = idiff;
                            ts.cross_diff_j = jdiff;
                            ts.cross_diff_k = kdiff;
                            ts_list.push_back(ts);

                            grid_->ts_matrix(ts_x, ts_y, ts_z) = 1;
                            grid_->ts_ever(ts_x, ts_y, ts_z) = 1;
                        }
                    }
                }
                else if (nb_cluster > 0 && nb_cluster != cluster.id) {
                    // Different cluster - check if should merge or create TS
                    double nb_energy = grid_->energy_at(nb.x, nb.y, nb.z);
                    double pt_energy = grid_->energy_at(pt.x, pt.y, pt.z);

                    // Get neighbor cluster min energy
                    Cluster& nb_clust = get_cluster(nb_cluster);
                    double dE_current = nb_energy - cluster.min_energy;
                    double dE_connect = nb_energy - nb_clust.min_energy;
                    double dE = std::min(dE_current, dE_connect);

                    // Check cross vector difference for tunnel detection
                    int idiff = nb_cross_i - grid_->cross_i(nb.x, nb.y, nb.z);
                    int jdiff = nb_cross_j - grid_->cross_j(nb.x, nb.y, nb.z);
                    int kdiff = nb_cross_k - grid_->cross_k(nb.x, nb.y, nb.z);

                    // MATLAB-compatible behavior:
                    // For different clusters, only count a tunnel vector when both clusters are
                    // already in the same merge group (connect_Mx == current_Mx).
                    int group_current = find_merge_group(cluster.id);
                    int group_connect = find_merge_group(nb_cluster);
                    bool same_merge_group = (group_current >= 0 && group_current == group_connect);
                    if ((idiff != 0 || jdiff != 0 || kdiff != 0) && same_merge_group) {
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
                        trace_pair("merge", pt, nb, pt_energy, nb_energy, dE, cluster.id, nb_cluster);
                        merge_clusters(cluster.id, nb_cluster, idiff, jdiff, kdiff, ts_list,
                                       &tunnel_cluster, ts_list_all);
                    } else {
                        // Create transition state
                        // MATLAB line 184/202: both host AND neighbor must be unmarked
                        if (grid_->ts_matrix(nb.x, nb.y, nb.z) == 0 &&
                            grid_->ts_matrix(pt.x, pt.y, pt.z) == 0) {
                            trace_pair("diff-cluster-ts", pt, nb, pt_energy, nb_energy, dE,
                                       cluster.id, nb_cluster);
                            // Choose higher energy point as TS
                            int ts_x, ts_y, ts_z;
                            if (nb_energy >= pt_energy) {
                                ts_x = nb.x; ts_y = nb.y; ts_z = nb.z;
                                // MATLAB: find neighbor row in C_connect, set info(line,6)=1
                                for (auto& cp : nb_clust.points) {
                                    if (cp.x == ts_x && cp.y == ts_y && cp.z == ts_z) {
                                        cp.ts_flag = 1;
                                        break;
                                    }
                                }
                            } else {
                                ts_x = pt.x; ts_y = pt.y; ts_z = pt.z;
                                // MATLAB: set info(index_list,6)=1 on host point
                                cluster.points[pi].ts_flag = 1;
                            }

                            TSPoint ts;
                            ts.x = ts_x;
                            ts.y = ts_y;
                            ts.z = ts_z;
                            ts.energy = grid_->energy_at(ts_x, ts_y, ts_z);
                            ts.level = grid_->level_at(ts_x, ts_y, ts_z);
                            ts.cluster1_id = std::min(cluster.id, nb_cluster);
                            ts.cluster2_id = std::max(cluster.id, nb_cluster);
                            ts.cross_i = nb_cross_i;
                            ts.cross_j = nb_cross_j;
                            ts.cross_k = nb_cross_k;
                            // cross_diff is from cluster.id -> nb_cluster perspective
                            // If IDs were swapped (cluster.id > nb_cluster), negate to get cluster1 -> cluster2
                            if (cluster.id > nb_cluster) {
                                ts.cross_diff_i = -idiff;
                                ts.cross_diff_j = -jdiff;
                                ts.cross_diff_k = -kdiff;
                            } else {
                                ts.cross_diff_i = idiff;
                                ts.cross_diff_j = jdiff;
                                ts.cross_diff_k = kdiff;
                            }
                            ts_list.push_back(ts);

                            grid_->ts_matrix(ts_x, ts_y, ts_z) = 1;
                            grid_->ts_ever(ts_x, ts_y, ts_z) = 1;
                        }

                        // Merge merge groups (MATLAB behavior)
                        merge_only_merge_groups(cluster.id, nb_cluster, idiff, jdiff, kdiff);
                    }

                }
                else {
                    // Assigned neighbor (any other case) does not mark boundary in MATLAB logic.
                }
            }

            cluster.points[pi].boundary = still_boundary ? 1 : 0;
        }
        
        // Add new points to cluster
        if (!new_points.empty()) {
            any_growth = true;  // Mark that growth happened
            cluster.points.insert(cluster.points.end(), 
                                 new_points.begin(), new_points.end());
        } else {
            filled[ci] = 1;
        }
    }
        for (size_t ci = 0; ci < clusters_.size(); ++ci) {
            if (clusters_[ci].id == 0) filled[ci] = 1;
            if (filled[ci] == 0) all_filled = false;
        }
        if (all_filled) {
            break;
        }
        if (!any_growth) {
            // Safety break; should not happen if all_filled tracking is correct.
            break;
        }
    } // End MATLAB-style filled loop
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
                                    std::vector<TSPoint>& ts_list,
                                    std::vector<int>* tunnel_cluster,
                                    std::vector<TSPoint>* ts_list_all) {
    (void)tunnel_cluster;
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
    
    // Merge-group bookkeeping (match MATLAB check_neighbors.m):
    // - If both clusters are already in the same merge group, remove cluster2 from that group.
    // - If in different groups, merge groups.
    // - If one side is missing, attach it.
    if (group1 >= 0 && group2 >= 0 && group1 == group2) {
        auto& g = merge_groups_[group1];
        g.erase(std::remove(g.begin(), g.end(), cluster2_id), g.end());
    } else if (group1 >= 0 && group2 >= 0 && group1 != group2) {
        // MATLAB semantics: merge into the lower group index, then remove the higher
        // by moving the last group into its slot (not stable erase).
        int low = std::min(group1, group2);
        int high = std::max(group1, group2);
        merge_groups_[low].insert(merge_groups_[low].end(),
                                  merge_groups_[high].begin(),
                                  merge_groups_[high].end());

        int last = static_cast<int>(merge_groups_.size()) - 1;
        if (high != last) {
            merge_groups_[high] = merge_groups_[last];
        }
        merge_groups_.pop_back();
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

    // Intentionally do not rewrite tunnel_cluster IDs during physical merges.
    // Final tunnel_cluster remapping is done after compacting clusters.
    
    // Mark cluster2 as removed
    c2->id = 0;
    c2->points.clear();
    
    auto remap_and_prune_ts = [this, cluster1_id, cluster2_id](std::vector<TSPoint>& vec) {
        for (auto& ts : vec) {
            if (ts.cluster1_id == cluster2_id) {
                ts.cluster1_id = cluster1_id;
            }
            if (ts.cluster2_id == cluster2_id) {
                ts.cluster2_id = cluster1_id;
            }
        }

        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [this](const TSPoint& ts) {
                    if (ts.cluster1_id == ts.cluster2_id) {
                        grid_->ts_matrix(ts.x, ts.y, ts.z) = 0;
                        return true;
                    }
                    return false;
                }),
            vec.end()
        );
    };

    // MATLAB updates both current-level TS list and accumulated TS_list_all during merges.
    remap_and_prune_ts(ts_list);
    if (ts_list_all != nullptr) {
        remap_and_prune_ts(*ts_list_all);
    }
}

void ClusterManager::merge_only_merge_groups(int cluster1_id, int cluster2_id, 
                                             int idiff, int jdiff, int kdiff) {
    // This function merges merge groups WITHOUT merging the actual clusters
    // This matches MATLAB behavior where clusters connected by TS (dE >= energy_step)
    // are kept separate but their merge groups are combined
    
    // Find merge groups
    int group1 = find_merge_group(cluster1_id);
    int group2 = find_merge_group(cluster2_id);
    
    // If clusters not in any merge group, initialize them
    if (group1 < 0) {
        init_merge_group(cluster1_id);
        group1 = find_merge_group(cluster1_id);
    }
    if (group2 < 0) {
        init_merge_group(cluster2_id);
        group2 = find_merge_group(cluster2_id);
    }
    
    // Check if already in same merge group
    if (group1 == group2) {
        // Already in same merge group, nothing to do
        return;
    }
    
    // Need to update cross vectors for cluster2's merge group if diff is non-zero
    if (idiff != 0 || jdiff != 0 || kdiff != 0) {
        // Get all clusters in group2
        std::vector<int> clusters_to_update = merge_groups_[group2];
        
        // Update cross vectors for all points in these clusters
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
    
    // MATLAB semantics: keep lower index group, remove higher index by swap-with-last.
    int low = std::min(group1, group2);
    int high = std::max(group1, group2);
    merge_groups_[low].insert(merge_groups_[low].end(),
                              merge_groups_[high].begin(),
                              merge_groups_[high].end());

    int last = static_cast<int>(merge_groups_.size()) - 1;
    if (high != last) {
        merge_groups_[high] = merge_groups_[last];
    }
    merge_groups_.pop_back();
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

std::map<int, int> ClusterManager::compact_clusters(std::vector<TSPoint>& ts_list_all) {
    std::cout << "\nCompacting clusters..." << std::endl;
    std::cout << "  Before: " << clusters_.size() << " clusters" << std::endl;
    
    // Count valid clusters (id != 0)
    int valid_count = 0;
    for (const auto& cluster : clusters_) {
        if (cluster.id != 0) {
            valid_count++;
        }
    }
    std::cout << "  Valid clusters (id != 0): " << valid_count << std::endl;
    
    // Create mapping from old cluster ID to new cluster ID
    std::map<int, int> id_mapping;
    std::vector<Cluster> valid_clusters;
    int new_id = 1;
    
    for (const auto& cluster : clusters_) {
        if (cluster.id != 0) {  // Only keep non-merged clusters
            Cluster new_cluster = cluster;
            id_mapping[cluster.id] = new_id;
            new_cluster.id = new_id;
            valid_clusters.push_back(new_cluster);
            new_id++;
        }
    }
    
    // Replace clusters vector with compacted version
    clusters_ = valid_clusters;
    // Update TS list cluster IDs
    for (auto& ts : ts_list_all) {
        if (id_mapping.find(ts.cluster1_id) != id_mapping.end()) {
            ts.cluster1_id = id_mapping[ts.cluster1_id];
        }
        if (id_mapping.find(ts.cluster2_id) != id_mapping.end()) {
            ts.cluster2_id = id_mapping[ts.cluster2_id];
        }
    }
    
    // Update minID_C matrix with new cluster IDs
    for (const auto& cluster : clusters_) {
        for (const auto& pt : cluster.points) {
            grid_->minID_C(pt.x, pt.y, pt.z) = cluster.id;
        }
    }
    
    // Update merge_groups with new cluster IDs
    for (auto& group : merge_groups_) {
        for (int& cluster_id : group) {
            if (id_mapping.find(cluster_id) != id_mapping.end()) {
                cluster_id = id_mapping[cluster_id];
            } else {
                // This cluster was merged away, mark as 0
                cluster_id = 0;
            }
        }
        // Remove zero entries from group
        group.erase(std::remove(group.begin(), group.end(), 0), group.end());
    }
    
    // Remove empty merge groups
    merge_groups_.erase(
        std::remove_if(merge_groups_.begin(), merge_groups_.end(),
                      [](const std::vector<int>& group) { return group.empty(); }),
        merge_groups_.end()
    );
    
    std::cout << "  After: " << clusters_.size() << " clusters (sequential IDs 1-" << clusters_.size() << ")" << std::endl;
    std::cout << "  Merge groups: " << merge_groups_.size() << std::endl;
    
    return id_mapping;
}
