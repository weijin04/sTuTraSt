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
    void organize_tunnels();
    
    // Generate processes (basis transitions) from tunnels
    void generate_processes(std::vector<Process>& processes);
    
    // Get all tunnels
    const std::vector<Tunnel>& tunnels() const { return tunnels_; }
    
private:
    std::shared_ptr<Grid> grid_;
    std::shared_ptr<ClusterManager> cluster_mgr_;
    std::shared_ptr<TransitionStateManager> ts_mgr_;
    std::vector<Tunnel> tunnels_;
    
    // Check if tunnel exists in a given dimension
    bool check_tunnel_dimension(int dim, const std::vector<int>& cluster_ids);
};

#endif // TUNNEL_H
