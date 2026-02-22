#ifndef TUNNEL_H
#define TUNNEL_H

#include "types.h"
#include "grid.h"
#include "cluster.h"
#include "transition_state.h"
#include <vector>
#include <memory>

class TunnelManager {
public:
    TunnelManager(std::shared_ptr<Grid> grid,
                 std::shared_ptr<ClusterManager> cluster_mgr,
                 std::shared_ptr<TransitionStateManager> ts_mgr);

    // Organize tunnels from cluster and TS information
    void organize_tunnels(const std::vector<int>& tunnel_cluster = std::vector<int>(),
                          const std::vector<std::array<int,3>>& tunnel_cluster_dim = std::vector<std::array<int,3>>());

    // Generate processes (basis transitions) from tunnels
    void generate_processes(std::vector<Process>& processes);

    // Get all tunnels
    const std::vector<Tunnel>& tunnels() const { return tunnels_; }

private:
    std::shared_ptr<Grid> grid_;
    std::shared_ptr<ClusterManager> cluster_mgr_;
    std::shared_ptr<TransitionStateManager> ts_mgr_;
    std::vector<Tunnel> tunnels_;

    // Helper functions
    Coord3D get_cluster_min_coord(const Cluster& cluster);
    bool is_cluster_boundary(const Cluster& cluster) const;
    CrossVector calculate_cross_vector_simple(const Coord3D& start, const Coord3D& end);
    CrossVector calculate_cross_vector_path(const Coord3D& start,
                                            const Coord3D& end,
                                            const std::vector<Coord3D>& coord_list,
                                            bool self_tunnel);
    CrossVector calculate_ts_cross_vector_bfs(const Cluster& c1, const TSGroup& tsgroup);
    CrossVector calculate_self_cross_vector_bfs(const Cluster& cluster);
};

#endif // TUNNEL_H
