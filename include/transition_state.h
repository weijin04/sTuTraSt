#ifndef TRANSITION_STATE_H
#define TRANSITION_STATE_H

#include "types.h"
#include "grid.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>

class TransitionStateManager {
public:
    TransitionStateManager(std::shared_ptr<Grid> grid);

    // Organize TS points into groups (surfaces)
    void organize_ts_groups(const std::vector<TSPoint>& ts_list);

    // Get all TS groups
    const std::vector<TSGroup>& ts_groups() const { return ts_groups_; }

private:
    std::shared_ptr<Grid> grid_;
    std::vector<TSGroup> ts_groups_;

    // Find connected TS points (flood fill) - uses spatial index for O(1) neighbor lookup
    void flood_fill_ts(const std::vector<TSPoint>& ts_list,
                      std::vector<bool>& visited,
                      int start_idx,
                      TSGroup& group,
                      const std::unordered_map<int64_t, std::vector<size_t>>& ts_spatial_index);
};

#endif // TRANSITION_STATE_H
