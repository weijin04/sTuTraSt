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
                      std::vector<int>& tunnel_list);
    
    // Check a neighbor point during cluster growth
    void check_neighbor(int i, int j, int k, int in, int jn, int kn,
                       int level, int cluster_id, bool& boundary,
                       std::vector<ClusterPoint>& temp_points,
                       std::vector<TSPoint>& ts_list,
                       std::vector<int>& tunnel_list,
                       int& cluster_to_remove);
    
    // Get cluster by ID
    Cluster& get_cluster(int id);
    const Cluster& get_cluster(int id) const;
    
    // Get all clusters
    std::vector<Cluster>& clusters() { return clusters_; }
    const std::vector<Cluster>& clusters() const { return clusters_; }
    
    int num_clusters() const { return clusters_.size(); }
    
private:
    std::shared_ptr<Grid> grid_;
    std::vector<Cluster> clusters_;
    int next_cluster_id_;
    
    // Merge two clusters
    void merge_clusters(int cluster1_id, int cluster2_id);
};

#endif // CLUSTER_H
