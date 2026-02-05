#ifndef OUTPUT_WRITER_H
#define OUTPUT_WRITER_H

#include "types.h"
#include "cluster.h"
#include "tunnel.h"
#include "transition_state.h"
#include <string>
#include <vector>
#include <memory>

class OutputWriter {
public:
    OutputWriter(std::shared_ptr<ClusterManager> cluster_mgr,
                std::shared_ptr<TunnelManager> tunnel_mgr,
                std::shared_ptr<TransitionStateManager> ts_mgr);
    
    // Write basis sites
    void write_basis(const std::string& filename);
    
    // Write processes for a given temperature
    void write_processes(const std::string& filename,
                        const std::vector<Process>& processes);
    
    // Write tunnel information
    void write_tunnel_info(const std::string& filename);
    
    // Write TS data
    void write_ts_data(const std::string& filename);
    
    // Write breakthrough energies
    void write_breakthrough(const std::string& filename,
                           const std::array<double, 3>& bt_energies);
    
    // Write energy volume
    void write_energy_volume(const std::string& filename,
                            const std::vector<double>& e_vol);
    
private:
    std::shared_ptr<ClusterManager> cluster_mgr_;
    std::shared_ptr<TunnelManager> tunnel_mgr_;
    std::shared_ptr<TransitionStateManager> ts_mgr_;
};

#endif // OUTPUT_WRITER_H
