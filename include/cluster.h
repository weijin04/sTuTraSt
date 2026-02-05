#ifndef CLUSTER_H
#define CLUSTER_H

#include "types.h"
#include "grid.h"
#include <vector>
#include <memory>

class ClusterManager {
public:
    ClusterManager(std::shared_ptr<Grid> grid);
    
    // Initiate new clusters at a given energy level
    void initiate_clusters(int level);
    
    // Grow existing clusters by one level
    void grow_clusters(int level, std::vector<TSPoint>& ts_list,
                      std::vector<std::array<int,3>>& tunnel_list,
                      std::vector<int>& tunnel_cluster,
                      std::vector<std::array<int,3>>& tunnel_cluster_dim,
                      double energy_step);
    
    // Get cluster by ID
    Cluster& get_cluster(int id);
    const Cluster& get_cluster(int id) const;
    
    // Get all clusters
    std::vector<Cluster>& clusters() { return clusters_; }
    const std::vector<Cluster>& clusters() const { return clusters_; }
    
    int num_clusters() const { return clusters_.size(); }
    
    // Get tunnel directions that have achieved breakthrough
    const std::vector<std::array<int,3>>& tunnel_directions() const { return tunnel_directions_; }
    void add_tunnel_direction(const std::array<int,3>& dir) { tunnel_directions_.push_back(dir); }
    
private:
    std::shared_ptr<Grid> grid_;
    std::vector<Cluster> clusters_;
    int next_cluster_id_;
    std::vector<std::array<int,3>> tunnel_directions_;
    
    // Merge two clusters
    void merge_clusters(int cluster1_id, int cluster2_id);
};

#endif // CLUSTER_H
