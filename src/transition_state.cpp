#include "transition_state.h"
#include "pbc.h"
#include <queue>
#include <algorithm>
#include <iostream>
#include <unordered_set>

TransitionStateManager::TransitionStateManager(std::shared_ptr<Grid> grid)
    : grid_(grid) {
}

// 5A: TSKey struct with hash for O(1) deduplication
struct TSKey {
    int x, y, z, c1, c2;
    bool operator==(const TSKey& o) const {
        return x==o.x && y==o.y && z==o.z && c1==o.c1 && c2==o.c2;
    }
};
struct TSKeyHash {
    size_t operator()(const TSKey& k) const {
        size_t h = std::hash<int>{}(k.x);
        h ^= std::hash<int>{}(k.y) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(k.z) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(k.c1) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(k.c2) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }
};

void TransitionStateManager::organize_ts_groups(const std::vector<TSPoint>& ts_list) {
    if (ts_list.empty()) return;

    std::cout << "Organizing TS points into groups (26-neighbor connectivity)..." << '\n';
    std::cout << "  Total TS points before deduplication: " << ts_list.size() << '\n';

    // 5A: O(N) deduplication using hash set (replaces O(N^2) nested loop)
    std::unordered_set<TSKey, TSKeyHash> seen;
    std::vector<bool> is_duplicate(ts_list.size(), false);
    for (size_t i = 0; i < ts_list.size(); i++) {
        int c1 = std::min(ts_list[i].cluster1_id, ts_list[i].cluster2_id);
        int c2 = std::max(ts_list[i].cluster1_id, ts_list[i].cluster2_id);
        TSKey key{ts_list[i].x, ts_list[i].y, ts_list[i].z, c1, c2};
        if (!seen.insert(key).second) {
            is_duplicate[i] = true;
        }
    }

    // Count non-duplicates
    int non_duplicate_count = 0;
    for (bool dup : is_duplicate) {
        if (!dup) non_duplicate_count++;
    }
    std::cout << "  TS points after deduplication: " << non_duplicate_count << '\n';

    // 5B: Build spatial index for O(1) neighbor lookup in flood_fill_ts
    auto pos_key = [](int x, int y, int z) -> int64_t {
        return static_cast<int64_t>(x) +
               static_cast<int64_t>(100000) * y +
               static_cast<int64_t>(100000) * static_cast<int64_t>(100000) * z;
    };
    std::unordered_map<int64_t, std::vector<size_t>> ts_spatial_index;
    for (size_t i = 0; i < ts_list.size(); i++) {
        if (!is_duplicate[i]) {
            ts_spatial_index[pos_key(ts_list[i].x, ts_list[i].y, ts_list[i].z)].push_back(i);
        }
    }

    std::vector<bool> visited(ts_list.size(), false);

    // Mark duplicates as visited so they won't be grouped
    for (size_t i = 0; i < ts_list.size(); i++) {
        if (is_duplicate[i]) visited[i] = true;
    }

    for (size_t i = 0; i < ts_list.size(); i++) {
        if (visited[i]) continue;

        TSGroup group;
        flood_fill_ts(ts_list, visited, i, group, ts_spatial_index);

        if (!group.points.empty()) {
            ts_groups_.push_back(group);
        }
    }
}

void TransitionStateManager::flood_fill_ts(const std::vector<TSPoint>& ts_list,
                                          std::vector<bool>& visited,
                                          int start_idx,
                                          TSGroup& group,
                                          const std::unordered_map<int64_t, std::vector<size_t>>& ts_spatial_index) {
    std::queue<int> queue;
    queue.push(start_idx);
    visited[start_idx] = true;

    // MATLAB organize_TS groups by (cluster1,cluster2) loop with cluster1<=cluster2.
    // Keep a canonical ordering so downstream tunnel assignment and process direction match.
    group.cluster1_id = std::min(ts_list[start_idx].cluster1_id, ts_list[start_idx].cluster2_id);
    group.cluster2_id = std::max(ts_list[start_idx].cluster1_id, ts_list[start_idx].cluster2_id);
    group.min_energy = ts_list[start_idx].energy;
    group.min_coord = Coord3D(ts_list[start_idx].x,
                             ts_list[start_idx].y,
                             ts_list[start_idx].z);
    group.cross_diff_i = ts_list[start_idx].cross_diff_i;
    group.cross_diff_j = ts_list[start_idx].cross_diff_j;
    group.cross_diff_k = ts_list[start_idx].cross_diff_k;

    // 5B: pos_key lambda for spatial index lookup
    auto pos_key = [](int x, int y, int z) -> int64_t {
        return static_cast<int64_t>(x) +
               static_cast<int64_t>(100000) * y +
               static_cast<int64_t>(100000) * static_cast<int64_t>(100000) * z;
    };

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

        // 5B: Find neighboring TS points using 26-neighbor connectivity via spatial index
        // Check all 26 neighbors (3x3x3 cube excluding center)
        for (int dz = -1; dz <= 1; dz++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    int nx = curr.x + dx;
                    int ny = curr.y + dy;
                    int nz = curr.z + dz;
                    // Apply PBC wrapping
                    if (nx < 0) nx += grid_->nx(); else if (nx >= grid_->nx()) nx -= grid_->nx();
                    if (ny < 0) ny += grid_->ny(); else if (ny >= grid_->ny()) ny -= grid_->ny();
                    if (nz < 0) nz += grid_->nz(); else if (nz >= grid_->nz()) nz -= grid_->nz();

                    auto it = ts_spatial_index.find(pos_key(nx, ny, nz));
                    if (it == ts_spatial_index.end()) continue;
                    for (size_t j : it->second) {
                        if (visited[j]) continue;
                        // Check if same cluster pair
                        bool same_pair = (ts_list[curr_idx].cluster1_id == ts_list[j].cluster1_id &&
                                          ts_list[curr_idx].cluster2_id == ts_list[j].cluster2_id) ||
                                         (ts_list[curr_idx].cluster1_id == ts_list[j].cluster2_id &&
                                          ts_list[curr_idx].cluster2_id == ts_list[j].cluster1_id);
                        if (same_pair) {
                            visited[j] = true;
                            queue.push(static_cast<int>(j));
                        }
                    }
                }
            }
        }
    }
}
