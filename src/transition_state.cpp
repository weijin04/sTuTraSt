#include "transition_state.h"
#include "pbc.h"
#include <queue>
#include <algorithm>

TransitionStateManager::TransitionStateManager(std::shared_ptr<Grid> grid)
    : grid_(grid) {
}

void TransitionStateManager::organize_ts_groups(const std::vector<TSPoint>& ts_list) {
    if (ts_list.empty()) return;
    
    std::vector<bool> visited(ts_list.size(), false);
    
    for (size_t i = 0; i < ts_list.size(); i++) {
        if (visited[i]) continue;
        
        TSGroup group;
        flood_fill_ts(ts_list, visited, i, group);
        
        if (!group.points.empty()) {
            ts_groups_.push_back(group);
        }
    }
}

void TransitionStateManager::flood_fill_ts(const std::vector<TSPoint>& ts_list,
                                          std::vector<bool>& visited,
                                          int start_idx,
                                          TSGroup& group) {
    std::queue<int> queue;
    queue.push(start_idx);
    visited[start_idx] = true;
    
    group.cluster1_id = ts_list[start_idx].cluster1_id;
    group.cluster2_id = ts_list[start_idx].cluster2_id;
    group.min_energy = ts_list[start_idx].energy;
    group.min_coord = Coord3D(ts_list[start_idx].x, 
                             ts_list[start_idx].y, 
                             ts_list[start_idx].z);
    
    while (!queue.empty()) {
        int curr_idx = queue.front();
        queue.pop();
        
        const TSPoint& curr = ts_list[curr_idx];
        group.points.push_back(curr);
        
        // Update min energy
        if (curr.energy < group.min_energy) {
            group.min_energy = curr.energy;
            group.min_coord = Coord3D(curr.x, curr.y, curr.z);
        }
        
        // Find neighboring TS points
        for (size_t j = 0; j < ts_list.size(); j++) {
            if (visited[j]) continue;
            
            const TSPoint& other = ts_list[j];
            
            // Check if same cluster pair
            bool same_pair = (curr.cluster1_id == other.cluster1_id && 
                             curr.cluster2_id == other.cluster2_id) ||
                            (curr.cluster1_id == other.cluster2_id && 
                             curr.cluster2_id == other.cluster1_id);
            
            if (!same_pair) continue;
            
            // Check if adjacent (6-connected)
            int dx = std::abs(curr.x - other.x);
            int dy = std::abs(curr.y - other.y);
            int dz = std::abs(curr.z - other.z);
            
            // Account for periodic boundaries
            if (dx > grid_->nx() / 2) dx = grid_->nx() - dx;
            if (dy > grid_->ny() / 2) dy = grid_->ny() - dy;
            if (dz > grid_->nz() / 2) dz = grid_->nz() - dz;
            
            bool adjacent = (dx + dy + dz == 1);
            
            if (adjacent) {
                visited[j] = true;
                queue.push(j);
            }
        }
    }
}

CrossVector TransitionStateManager::get_ts_cross_vector(const Coord3D& min1, 
                                                        const Coord3D& min2) const {
    CrossVector cv;
    
    int dx = min2.x - min1.x;
    int dy = min2.y - min1.y;
    int dz = min2.z - min1.z;
    
    // Determine shortest path considering periodic boundaries
    if (std::abs(dx) > grid_->nx() / 2) {
        cv.i = (dx > 0) ? -1 : 1;
    } else {
        cv.i = 0;
    }
    
    if (std::abs(dy) > grid_->ny() / 2) {
        cv.j = (dy > 0) ? -1 : 1;
    } else {
        cv.j = 0;
    }
    
    if (std::abs(dz) > grid_->nz() / 2) {
        cv.k = (dz > 0) ? -1 : 1;
    } else {
        cv.k = 0;
    }
    
    return cv;
}
