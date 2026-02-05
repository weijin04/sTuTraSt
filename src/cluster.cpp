#include "cluster.h"
#include "pbc.h"
#include <queue>
#include <iostream>

ClusterManager::ClusterManager(std::shared_ptr<Grid> grid)
    : grid_(grid), next_cluster_id_(1) {
}

void ClusterManager::initiate_clusters(int level) {
    // Find all points at current level that haven't been assigned to a cluster
    for (int z = 0; z < grid_->nz(); z++) {
        for (int y = 0; y < grid_->ny(); y++) {
            for (int x = 0; x < grid_->nx(); x++) {
                if (grid_->level_at(x, y, z) == level && 
                    grid_->minID_C(x, y, z) == 0) {
                    
                    // Start new cluster with flood fill
                    Cluster new_cluster;
                    new_cluster.id = next_cluster_id_++;
                    new_cluster.min_energy = grid_->energy_at(x, y, z);
                    
                    std::queue<Coord3D> queue;
                    queue.push(Coord3D(x, y, z));
                    grid_->minID_C(x, y, z) = new_cluster.id;
                    
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
                        
                        std::vector<Coord3D> neighbors = {
                            Coord3D(ip, curr.y, curr.z),
                            Coord3D(im, curr.y, curr.z),
                            Coord3D(curr.x, jp, curr.z),
                            Coord3D(curr.x, jm, curr.z),
                            Coord3D(curr.x, curr.y, kp),
                            Coord3D(curr.x, curr.y, km)
                        };
                        
                        for (const auto& nb : neighbors) {
                            if (grid_->level_at(nb.x, nb.y, nb.z) == level &&
                                grid_->minID_C(nb.x, nb.y, nb.z) == 0) {
                                grid_->minID_C(nb.x, nb.y, nb.z) = new_cluster.id;
                                queue.push(nb);
                            }
                        }
                    }
                    
                    clusters_.push_back(new_cluster);
                }
            }
        }
    }
}

void ClusterManager::grow_clusters(int level, std::vector<TSPoint>& ts_list,
                                   std::vector<int>& tunnel_list) {
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
            
            // Check the 6 face-connected neighbors
            std::vector<Coord3D> neighbors = {
                Coord3D(ip, pt.y, pt.z),
                Coord3D(im, pt.y, pt.z),
                Coord3D(pt.x, jp, pt.z),
                Coord3D(pt.x, jm, pt.z),
                Coord3D(pt.x, pt.y, kp),
                Coord3D(pt.x, pt.y, km)
            };
            
            for (const auto& nb : neighbors) {
                int nb_level = grid_->level_at(nb.x, nb.y, nb.z);
                int nb_cluster = grid_->minID_C(nb.x, nb.y, nb.z);
                
                if (nb_level == level && nb_cluster == 0) {
                    // Add to cluster
                    grid_->minID_C(nb.x, nb.y, nb.z) = cluster.id;
                    
                    ClusterPoint new_pt;
                    new_pt.x = nb.x;
                    new_pt.y = nb.y;
                    new_pt.z = nb.z;
                    new_pt.level = level;
                    new_pt.boundary = 1;
                    new_pt.cross_i = grid_->cross_i(nb.x, nb.y, nb.z);
                    new_pt.cross_j = grid_->cross_j(nb.x, nb.y, nb.z);
                    new_pt.cross_k = grid_->cross_k(nb.x, nb.y, nb.z);
                    new_points.push_back(new_pt);
                    
                    still_boundary = true;
                    
                    // Update cluster min energy
                    double e = grid_->energy_at(nb.x, nb.y, nb.z);
                    if (e < cluster.min_energy) {
                        cluster.min_energy = e;
                    }
                }
                else if (nb_level == level && nb_cluster != cluster.id && nb_cluster > 0) {
                    // Found transition state between two clusters
                    TSPoint ts;
                    ts.x = pt.x;
                    ts.y = pt.y;
                    ts.z = pt.z;
                    ts.level = level;
                    ts.energy = grid_->energy_at(pt.x, pt.y, pt.z);
                    ts.cluster1_id = cluster.id;
                    ts.cluster2_id = nb_cluster;
                    ts.cross_i = grid_->cross_i(pt.x, pt.y, pt.z);
                    ts.cross_j = grid_->cross_j(pt.x, pt.y, pt.z);
                    ts.cross_k = grid_->cross_k(pt.x, pt.y, pt.z);
                    ts_list.push_back(ts);
                    
                    grid_->ts_matrix(pt.x, pt.y, pt.z) = 1;
                    
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

void ClusterManager::check_neighbor(int i, int j, int k, int in, int jn, int kn,
                                   int level, int cluster_id, bool& boundary,
                                   std::vector<ClusterPoint>& temp_points,
                                   std::vector<TSPoint>& ts_list,
                                   std::vector<int>& tunnel_list,
                                   int& cluster_to_remove) {
    // Simplified neighbor checking
    int nb_level = grid_->level_at(in, jn, kn);
    int nb_cluster = grid_->minID_C(in, jn, kn);
    
    if (nb_level == level && nb_cluster == 0) {
        // Add to current cluster
        grid_->minID_C(in, jn, kn) = cluster_id;
        
        ClusterPoint pt;
        pt.x = in;
        pt.y = jn;
        pt.z = kn;
        pt.level = level;
        pt.boundary = 1;
        temp_points.push_back(pt);
        
        boundary = true;
    }
    else if (nb_level == level && nb_cluster != cluster_id && nb_cluster > 0) {
        // Transition state
        TSPoint ts;
        ts.x = i;
        ts.y = j;
        ts.z = k;
        ts.level = level;
        ts.energy = grid_->energy_at(i, j, k);
        ts.cluster1_id = cluster_id;
        ts.cluster2_id = nb_cluster;
        ts_list.push_back(ts);
        
        boundary = true;
    }
    else if (nb_level > level) {
        boundary = true;
    }
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

void ClusterManager::merge_clusters(int cluster1_id, int cluster2_id) {
    // Simplified merge: mark cluster2 as merged into cluster1
    for (auto& cluster : clusters_) {
        if (cluster.id == cluster2_id) {
            cluster.id = 0;  // Mark as removed
            break;
        }
    }
}
