#ifndef CLUSTER_H
#define CLUSTER_H

#include "types.h"
#include "grid.h"
#include <vector>
#include <memory>
#include <map>

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
                      double energy_step,
                      std::vector<TSPoint>* ts_list_all = nullptr);
    
    // Get cluster by ID
    Cluster& get_cluster(int id);
    const Cluster& get_cluster(int id) const;
    
    // Get all clusters
    std::vector<Cluster>& clusters() { return clusters_; }
    const std::vector<Cluster>& clusters() const { return clusters_; }
    
    int num_clusters() const { return clusters_.size(); }
    
    // Get grid
    std::shared_ptr<Grid> grid() const { return grid_; }
    
    // Get tunnel directions that have achieved breakthrough
    const std::vector<std::array<int,3>>& tunnel_directions() const { return tunnel_directions_; }
    void add_tunnel_direction(const std::array<int,3>& dir) { tunnel_directions_.push_back(dir); }
    
    // Compact clusters: filter out merged clusters (id==0) and renumber sequentially
    // Returns mapping from old cluster ID to new cluster ID
    std::map<int, int> compact_clusters(std::vector<TSPoint>& ts_list_all);
    
    // Get merge groups (for tunnel organization)
    const std::vector<std::vector<int>>& merge_groups() const { return merge_groups_; }
    
private:
    std::shared_ptr<Grid> grid_;
    std::vector<Cluster> clusters_;
    int next_cluster_id_;
    std::vector<std::array<int,3>> tunnel_directions_;
    
    // Merge groups tracking (MATLAB list.M equivalent)
    // Each vector contains cluster IDs that have been merged together
    std::vector<std::vector<int>> merge_groups_;
    
    // Initialize merge groups for a cluster
    void init_merge_group(int cluster_id);
    
    // Find which merge group a cluster belongs to
    int find_merge_group(int cluster_id);
    
    // Merge two clusters (full implementation)
    void merge_clusters(int cluster1_id, int cluster2_id, int idiff, int jdiff, int kdiff, 
                       std::vector<TSPoint>& ts_list,
                       std::vector<TSPoint>* ts_list_all = nullptr);
    
    // Merge merge groups only (when creating TS between clusters)
    // This is needed to match MATLAB behavior where clusters connected by TS
    // are in the same merge group even though they're not fully merged
    void merge_only_merge_groups(int cluster1_id, int cluster2_id, int idiff, int jdiff, int kdiff);
};

#endif // CLUSTER_H
