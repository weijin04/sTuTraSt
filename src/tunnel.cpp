#include "tunnel.h"
#include "pbc.h"
#include <set>
#include <map>
#include <queue>
#include <algorithm>
#include <iostream>
#include <limits>

TunnelManager::TunnelManager(std::shared_ptr<Grid> grid,
                            std::shared_ptr<ClusterManager> cluster_mgr,
                            std::shared_ptr<TransitionStateManager> ts_mgr)
    : grid_(grid), cluster_mgr_(cluster_mgr), ts_mgr_(ts_mgr) {
}

void TunnelManager::organize_tunnels(const std::vector<int>& tunnel_cluster,
                                      const std::vector<std::array<int,3>>& tunnel_cluster_dim) {
    const auto& merge_groups = cluster_mgr_->merge_groups();
    const auto& ts_groups = ts_mgr_->ts_groups();
    const auto& clusters = cluster_mgr_->clusters();

    // Build set of cluster IDs that have TS connections
    std::set<int> clusters_with_ts;
    for (const auto& ts_group : ts_groups) {
        clusters_with_ts.insert(ts_group.cluster1_id);
        clusters_with_ts.insert(ts_group.cluster2_id);
    }

    // Also add clusters from tunnel_cluster (single-cluster self-tunnels)
    for (int cluster_id : tunnel_cluster) {
        clusters_with_ts.insert(cluster_id);
    }

    // Build lookup: cluster_id -> indices in tunnel_cluster/tunnel_cluster_dim
    std::map<int, std::vector<size_t>> tc_lookup;
    for (size_t i = 0; i < tunnel_cluster.size(); i++) {
        tc_lookup[tunnel_cluster[i]].push_back(i);
    }

    int tunnel_id = 1;
    std::set<size_t> processed_tsgroups;

    for (const auto& merge_group : merge_groups) {
        // Check if any cluster in this merge group has TS connections
        bool has_tunnel = false;
        for (int cluster_id : merge_group) {
            if (clusters_with_ts.count(cluster_id) > 0) {
                has_tunnel = true;
                break;
            }
        }

        if (!has_tunnel) continue;

        Tunnel tunnel;
        tunnel.id = tunnel_id;
        tunnel.cluster_ids = merge_group;

        // Find all TS groups connecting clusters in this merge group
        for (size_t i = 0; i < ts_groups.size(); i++) {
            if (processed_tsgroups.find(i) != processed_tsgroups.end()) {
                continue;
            }

            bool c1_in_group = std::find(merge_group.begin(), merge_group.end(),
                                        ts_groups[i].cluster1_id) != merge_group.end();
            bool c2_in_group = std::find(merge_group.begin(), merge_group.end(),
                                        ts_groups[i].cluster2_id) != merge_group.end();

            if (c1_in_group || c2_in_group) {
                tunnel.tsgroup_ids.push_back(i);
                processed_tsgroups.insert(i);
            }
        }

        // Collect self_cross_dirs from tunnel_cluster
        for (int cluster_id : merge_group) {
            if (tc_lookup.count(cluster_id)) {
                for (size_t idx : tc_lookup[cluster_id]) {
                    tunnel.self_cross_dirs.push_back({cluster_id, tunnel_cluster_dim[idx]});
                }
            }
        }
        if (tunnel.tsgroup_ids.empty() && !tunnel.self_cross_dirs.empty()) {
            tunnel.is_self_tunnel = true;
        }

        // Assign same tunnel_id to all clusters in merge group
        for (int cluster_id : merge_group) {
            if (cluster_id > 0 && cluster_id <= static_cast<int>(clusters.size())) {
                auto& cluster = cluster_mgr_->get_cluster(cluster_id);
                if (cluster.id != 0) {
                    cluster.tunnel_id = tunnel_id;
                }
            }
        }

        // Find minimum energy among all TS groups in tunnel
        tunnel.min_energy = std::numeric_limits<double>::max();
        for (int ts_group_idx : tunnel.tsgroup_ids) {
            if (ts_groups[ts_group_idx].min_energy < tunnel.min_energy) {
                tunnel.min_energy = ts_groups[ts_group_idx].min_energy;
            }
        }

        tunnel.dimensions = {1, 1, 1};

        tunnels_.push_back(tunnel);
        tunnel_id++;
    }

    std::cout << "Found " << tunnels_.size() << " tunnels from "
              << merge_groups.size() << " merge groups" << std::endl;
}

void TunnelManager::generate_processes(std::vector<Process>& processes) {
    const auto& ts_groups = ts_mgr_->ts_groups();

    for (const auto& tunnel : tunnels_) {
        // Generate processes from TS groups
        for (size_t ts_idx = 0; ts_idx < tunnel.tsgroup_ids.size(); ts_idx++) {
            int tsgroup_id = tunnel.tsgroup_ids[ts_idx];
            const TSGroup& tsgroup = ts_groups[tsgroup_id];

            const Cluster& c1 = cluster_mgr_->get_cluster(tsgroup.cluster1_id);
            const Cluster& c2 = cluster_mgr_->get_cluster(tsgroup.cluster2_id);

            Coord3D min1 = get_cluster_min_coord(c1);
            Coord3D min2 = get_cluster_min_coord(c2);
            Coord3D ts_min = tsgroup.min_coord;

            // Match MATLAB: compute cross vectors only when at least one side is a boundary cluster.
            bool use_cross_vectors = is_cluster_boundary(c1) || is_cluster_boundary(c2);

            CrossVector cv;
            CrossVector ts_cv;

            if (use_cross_vectors) {
                std::vector<Coord3D> process_coords;
                process_coords.reserve(c1.points.size() + tsgroup.points.size() + c2.points.size());
                for (const auto& pt : c1.points) {
                    if (pt.ts_flag != 0) continue;
                    process_coords.emplace_back(pt.x, pt.y, pt.z);
                }
                for (const auto& pt : tsgroup.points) process_coords.emplace_back(pt.x, pt.y, pt.z);
                for (const auto& pt : c2.points) {
                    if (pt.ts_flag != 0) continue;
                    process_coords.emplace_back(pt.x, pt.y, pt.z);
                }

                std::vector<Coord3D> ts_coords;
                ts_coords.reserve(c1.points.size() + tsgroup.points.size());
                for (const auto& pt : c1.points) {
                    if (pt.ts_flag != 0) continue;
                    ts_coords.emplace_back(pt.x, pt.y, pt.z);
                }
                for (const auto& pt : tsgroup.points) ts_coords.emplace_back(pt.x, pt.y, pt.z);

                if (tsgroup.cluster1_id != tsgroup.cluster2_id) {
                    cv = calculate_cross_vector_path(min1, min2, process_coords, false);
                    ts_cv = calculate_cross_vector_path(min1, ts_min, ts_coords, false);
                } else {
                    cv = calculate_cross_vector_path(min1, min1, process_coords, true);
                    ts_cv = cv;
                }
            }

            // Forward process: cluster1 -> cluster2
            Process fwd;
            fwd.from_basis = tsgroup.cluster1_id - 1;
            fwd.to_basis = tsgroup.cluster2_id - 1;
            fwd.rate = 0.0;
            fwd.tunnel_id = tunnel.id;
            fwd.tsgroup_id = tsgroup_id;
            fwd.from_cluster_orig = tsgroup.cluster1_id;
            fwd.to_cluster_orig = tsgroup.cluster2_id;
            fwd.cross = cv;

            fwd.ts_cross = ts_cv;

            processes.push_back(fwd);

            // Reverse process: cluster2 -> cluster1
            Process rev;
            rev.from_basis = tsgroup.cluster2_id - 1;
            rev.to_basis = tsgroup.cluster1_id - 1;
            rev.rate = 0.0;
            rev.tunnel_id = tunnel.id;
            rev.tsgroup_id = tsgroup_id;
            rev.from_cluster_orig = tsgroup.cluster2_id;
            rev.to_cluster_orig = tsgroup.cluster1_id;

            CrossVector neg_cross;
            neg_cross.i = -cv.i;
            neg_cross.j = -cv.j;
            neg_cross.k = -cv.k;
            rev.cross = neg_cross;

            CrossVector rev_ts_cross;
            rev_ts_cross.i = ts_cv.i - cv.i;
            rev_ts_cross.j = ts_cv.j - cv.j;
            rev_ts_cross.k = ts_cv.k - cv.k;
            rev.ts_cross = rev_ts_cross;

            processes.push_back(rev);
        }
    }

    std::cout << "Generated " << processes.size() << " processes" << std::endl;
}

Coord3D TunnelManager::get_cluster_min_coord(const Cluster& cluster) {
    if (cluster.points.empty()) {
        return Coord3D(0, 0, 0);
    }

    int min_idx = 0;
    double min_e = std::numeric_limits<double>::max();
    for (size_t i = 0; i < cluster.points.size(); i++) {
        const auto& pt = cluster.points[i];
        double e = grid_->energy_at(pt.x, pt.y, pt.z);
        if (e < min_e) {
            min_e = e;
            min_idx = i;
        }
    }

    const auto& min_pt = cluster.points[min_idx];
    return Coord3D(min_pt.x, min_pt.y, min_pt.z);
}

bool TunnelManager::is_cluster_boundary(const Cluster& cluster) const {
    int cnt_x0 = 0, cnt_xn = 0, cnt_y0 = 0, cnt_yn = 0, cnt_z0 = 0, cnt_zn = 0;
    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();

    for (const auto& pt : cluster.points) {
        if (pt.ts_flag != 0) continue;
        if (pt.x == 0) cnt_x0++;
        if (pt.x == nx - 1) cnt_xn++;
        if (pt.y == 0) cnt_y0++;
        if (pt.y == ny - 1) cnt_yn++;
        if (pt.z == 0) cnt_z0++;
        if (pt.z == nz - 1) cnt_zn++;
    }

    // MATLAB uses boundary=1 when any side has more than one point on the boundary plane.
    return (cnt_x0 > 1) || (cnt_xn > 1) ||
           (cnt_y0 > 1) || (cnt_yn > 1) ||
           (cnt_z0 > 1) || (cnt_zn > 1);
}

CrossVector TunnelManager::calculate_cross_vector_simple(const Coord3D& start, const Coord3D& end) {
    int dx = end.x - start.x;
    int dy = end.y - start.y;
    int dz = end.z - start.z;

    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();

    int cross_x = 0, cross_y = 0, cross_z = 0;

    if (dx > nx / 2) {
        cross_x = -1;
    } else if (dx < -nx / 2) {
        cross_x = 1;
    }

    if (dy > ny / 2) {
        cross_y = -1;
    } else if (dy < -ny / 2) {
        cross_y = 1;
    }

    if (dz > nz / 2) {
        cross_z = -1;
    } else if (dz < -nz / 2) {
        cross_z = 1;
    }

    return CrossVector(cross_x, cross_y, cross_z);
}

CrossVector TunnelManager::calculate_cross_vector_path(const Coord3D& start,
                                                       const Coord3D& end,
                                                       const std::vector<Coord3D>& coord_list,
                                                       bool self_tunnel) {
    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();

    auto key = [nx, ny](int x, int y, int z) -> long long {
        return static_cast<long long>(x) +
               static_cast<long long>(nx) * y +
               static_cast<long long>(nx) * static_cast<long long>(ny) * z;
    };

    struct PointInfo {
        bool valid = false;
        bool visited = false;
        int cross_i = 0;
        int cross_j = 0;
        int cross_k = 0;
    };

    std::map<long long, PointInfo> point_map;
    for (const auto& pt : coord_list) {
        point_map[key(pt.x, pt.y, pt.z)].valid = true;
    }

    long long start_key = key(start.x, start.y, start.z);
    if (point_map.find(start_key) == point_map.end()) {
        return CrossVector(0, 0, 0);
    }

    struct BFSNode {
        int x, y, z;
        int cross_i, cross_j, cross_k;
    };

    std::queue<BFSNode> bfs_queue;
    point_map[start_key].visited = true;
    point_map[start_key].cross_i = 0;
    point_map[start_key].cross_j = 0;
    point_map[start_key].cross_k = 0;
    bfs_queue.push({start.x, start.y, start.z, 0, 0, 0});

    while (!bfs_queue.empty()) {
        BFSNode curr = bfs_queue.front();
        bfs_queue.pop();

        int ip, im, jp, jm, kp, km;
        CrossVector cv;
        PBC::apply(curr.x, curr.y, curr.z, nx, ny, nz, ip, im, jp, jm, kp, km, cv);

        struct NbInfo {
            int x, y, z;
            int ci, cj, ck;
        };

        // MATLAB neighbor order: +k, -k, +j, -j, +i, -i
        NbInfo neighbors[6] = {
            {curr.x, curr.y, kp, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[7]},
            {curr.x, curr.y, km, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[8]},
            {curr.x, jp, curr.z, curr.cross_i, curr.cross_j + cv.vals[4], curr.cross_k},
            {curr.x, jm, curr.z, curr.cross_i, curr.cross_j + cv.vals[5], curr.cross_k},
            {ip, curr.y, curr.z, curr.cross_i + cv.vals[1], curr.cross_j, curr.cross_k},
            {im, curr.y, curr.z, curr.cross_i + cv.vals[2], curr.cross_j, curr.cross_k}
        };

        for (const auto& nb : neighbors) {
            long long nb_key = key(nb.x, nb.y, nb.z);
            auto it = point_map.find(nb_key);
            if (it == point_map.end() || !it->second.valid) {
                continue;
            }

            if (!it->second.visited) {
                it->second.visited = true;
                it->second.cross_i = nb.ci;
                it->second.cross_j = nb.cj;
                it->second.cross_k = nb.ck;

                if (!self_tunnel && nb.x == end.x && nb.y == end.y && nb.z == end.z) {
                    return CrossVector(nb.ci, nb.cj, nb.ck);
                }

                bfs_queue.push({nb.x, nb.y, nb.z, nb.ci, nb.cj, nb.ck});
            } else if (self_tunnel) {
                int di = it->second.cross_i - nb.ci;
                int dj = it->second.cross_j - nb.cj;
                int dk = it->second.cross_k - nb.ck;
                if (di != 0 || dj != 0 || dk != 0) {
                    return CrossVector(di, dj, dk);
                }
            }
        }
    }

    if (!self_tunnel) {
        return calculate_cross_vector_simple(start, end);
    }
    return CrossVector(0, 0, 0);
}

CrossVector TunnelManager::calculate_ts_cross_vector_bfs(const Cluster& c1,
                                                          const TSGroup& tsgroup) {
    // BFS from cluster1_min to TS_min through C1 + TS points
    // Matches MATLAB: TS_coord_list = [C1_points; TS_points]
    //                 get_TS_cross_vector(start_min, TS_coord_min, TS_coord_list, ...)

    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();

    Coord3D min1 = get_cluster_min_coord(c1);
    Coord3D ts_min = tsgroup.min_coord;

    auto key = [nx, ny](int x, int y, int z) -> long long {
        return (long long)x + (long long)nx * y + (long long)nx * ny * z;
    };

    struct PointInfo {
        bool valid = false;
        bool visited = false;
    };

    std::map<long long, PointInfo> point_map;

    // Add cluster1 points
    for (const auto& pt : c1.points) {
        point_map[key(pt.x, pt.y, pt.z)].valid = true;
    }

    // Add TS group points
    for (const auto& pt : tsgroup.points) {
        point_map[key(pt.x, pt.y, pt.z)].valid = true;
    }

    // BFS from min1
    struct BFSNode {
        int x, y, z;
        int cross_i, cross_j, cross_k;
    };

    std::queue<BFSNode> bfs_queue;
    long long start_key = key(min1.x, min1.y, min1.z);
    point_map[start_key].visited = true;
    bfs_queue.push({min1.x, min1.y, min1.z, 0, 0, 0});

    long long target_key = key(ts_min.x, ts_min.y, ts_min.z);

    while (!bfs_queue.empty()) {
        BFSNode curr = bfs_queue.front();
        bfs_queue.pop();

        if (key(curr.x, curr.y, curr.z) == target_key) {
            return CrossVector(curr.cross_i, curr.cross_j, curr.cross_k);
        }

        // Get 6-connected neighbors with PBC
        int ip, im, jp, jm, kp, km;
        CrossVector cv;
        PBC::apply(curr.x, curr.y, curr.z, nx, ny, nz, ip, im, jp, jm, kp, km, cv);

        struct NbInfo {
            int x, y, z;
            int ci, cj, ck;
        };

        // MATLAB neighbor order: +k, -k, +j, -j, +i, -i
        NbInfo neighbors[6] = {
            {curr.x, curr.y, kp, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[7]},
            {curr.x, curr.y, km, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[8]},
            {curr.x, jp, curr.z, curr.cross_i, curr.cross_j + cv.vals[4], curr.cross_k},
            {curr.x, jm, curr.z, curr.cross_i, curr.cross_j + cv.vals[5], curr.cross_k},
            {ip, curr.y, curr.z, curr.cross_i + cv.vals[1], curr.cross_j, curr.cross_k},
            {im, curr.y, curr.z, curr.cross_i + cv.vals[2], curr.cross_j, curr.cross_k}
        };

        for (const auto& nb : neighbors) {
            long long nb_key = key(nb.x, nb.y, nb.z);
            auto it = point_map.find(nb_key);
            if (it != point_map.end() && it->second.valid && !it->second.visited) {
                it->second.visited = true;
                bfs_queue.push({nb.x, nb.y, nb.z, nb.ci, nb.cj, nb.ck});
            }
        }
    }

    // Fallback
    return calculate_cross_vector_simple(min1, ts_min);
}

CrossVector TunnelManager::calculate_self_cross_vector_bfs(const Cluster& cluster) {
    // BFS through cluster to find PBC wrapping direction
    // Matches MATLAB get_TS_cross_vector when C1==C2:
    // Walk through cluster, when we find a visited neighbor with different cross vector,
    // that difference is the wrapping direction.

    int nx = grid_->nx();
    int ny = grid_->ny();
    int nz = grid_->nz();

    Coord3D min1 = get_cluster_min_coord(cluster);

    auto key = [nx, ny](int x, int y, int z) -> long long {
        return (long long)x + (long long)nx * y + (long long)nx * ny * z;
    };

    struct PointInfo {
        bool valid = false;
        bool visited = false;
        int cross_i = 0, cross_j = 0, cross_k = 0;
    };

    std::map<long long, PointInfo> point_map;

    for (const auto& pt : cluster.points) {
        point_map[key(pt.x, pt.y, pt.z)].valid = true;
    }

    struct BFSNode {
        int x, y, z;
        int cross_i, cross_j, cross_k;
    };

    std::queue<BFSNode> bfs_queue;
    long long start_key = key(min1.x, min1.y, min1.z);
    auto& start_info = point_map[start_key];
    start_info.visited = true;
    start_info.cross_i = 0;
    start_info.cross_j = 0;
    start_info.cross_k = 0;
    bfs_queue.push({min1.x, min1.y, min1.z, 0, 0, 0});

    while (!bfs_queue.empty()) {
        BFSNode curr = bfs_queue.front();
        bfs_queue.pop();

        int ip, im, jp, jm, kp, km;
        CrossVector cv;
        PBC::apply(curr.x, curr.y, curr.z, nx, ny, nz, ip, im, jp, jm, kp, km, cv);

        struct NbInfo {
            int x, y, z;
            int ci, cj, ck;
        };

        // MATLAB neighbor order: +k, -k, +j, -j, +i, -i
        NbInfo neighbors[6] = {
            {curr.x, curr.y, kp, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[7]},
            {curr.x, curr.y, km, curr.cross_i, curr.cross_j, curr.cross_k + cv.vals[8]},
            {curr.x, jp, curr.z, curr.cross_i, curr.cross_j + cv.vals[4], curr.cross_k},
            {curr.x, jm, curr.z, curr.cross_i, curr.cross_j + cv.vals[5], curr.cross_k},
            {ip, curr.y, curr.z, curr.cross_i + cv.vals[1], curr.cross_j, curr.cross_k},
            {im, curr.y, curr.z, curr.cross_i + cv.vals[2], curr.cross_j, curr.cross_k}
        };

        for (const auto& nb : neighbors) {
            long long nb_key = key(nb.x, nb.y, nb.z);
            auto it = point_map.find(nb_key);
            if (it != point_map.end() && it->second.valid) {
                if (!it->second.visited) {
                    it->second.visited = true;
                    it->second.cross_i = nb.ci;
                    it->second.cross_j = nb.cj;
                    it->second.cross_k = nb.ck;
                    bfs_queue.push({nb.x, nb.y, nb.z, nb.ci, nb.cj, nb.ck});
                } else {
                    // Already visited - check if cross vectors differ
                    int di = it->second.cross_i - nb.ci;
                    int dj = it->second.cross_j - nb.cj;
                    int dk = it->second.cross_k - nb.ck;
                    if (di != 0 || dj != 0 || dk != 0) {
                        return CrossVector(di, dj, dk);
                    }
                }
            }
        }
    }

    // Fallback: no wrapping found
    return CrossVector(0, 0, 0);
}
