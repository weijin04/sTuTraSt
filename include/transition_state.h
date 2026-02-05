#ifndef TRANSITION_STATE_H
#define TRANSITION_STATE_H

#include "types.h"
#include "grid.h"
#include <vector>
#include <memory>

class TransitionStateManager {
public:
    TransitionStateManager(std::shared_ptr<Grid> grid);
    
    // Organize TS points into groups (surfaces)
    void organize_ts_groups(const std::vector<TSPoint>& ts_list);
    
    // Get TS cross vector between two clusters
    CrossVector get_ts_cross_vector(const Coord3D& min1, const Coord3D& min2) const;
    
    // Get all TS groups
    const std::vector<TSGroup>& ts_groups() const { return ts_groups_; }
    
private:
    std::shared_ptr<Grid> grid_;
    std::vector<TSGroup> ts_groups_;
    
    // Find connected TS points (flood fill)
    void flood_fill_ts(const std::vector<TSPoint>& ts_list,
                      std::vector<bool>& visited,
                      int start_idx,
                      TSGroup& group);
};

#endif // TRANSITION_STATE_H
