# sTuTraStcc 优化说明

本目录是 `sTuTraSt/` 的优化版本，所有改动保证与原始代码的**物理等价性**：相同输入产生完全相同的输出文件。

---

## 背景

原始程序在运行长步骤 KMC 模拟时存在内存过大和运行时间过长的问题。经过代码分析，归纳出以下两类瓶颈：

- **内存瓶颈**：Grid 和 ClusterPoint 的数据类型选择过于保守，大量布尔值和小整数使用 `int` 存储。
- **时间瓶颈**：多处 O(N²) 算法、O(N) 线性查找嵌入热循环、以及在主循环中重复执行全网格扫描。

---

## 优化内容

### 1. Grid 矩阵类型收窄（`include/grid.h`, `src/grid.cpp`）

将 Grid 中 6 个矩阵的存储类型从 `int`（4 字节）改为更窄的类型：

| 矩阵 | 原类型 | 新类型 | 依据 |
|------|--------|--------|------|
| `TS_matrix_` | `int` | `uint8_t` | 仅存 0/1 |
| `TS_ever_matrix_` | `int` | `uint8_t` | 仅存 0/1 |
| `cross_i_matrix_` | `int` | `int8_t` | PBC 偏移，实际范围约 ±20 |
| `cross_j_matrix_` | `int` | `int8_t` | 同上 |
| `cross_k_matrix_` | `int` | `int8_t` | 同上 |
| `minID_L_matrix_` | `int` | `int16_t` | 存 energy level，上限 level_stop ≤ 32767 |

**效果**：每 voxel 节省 17 字节（40 → 23 字节，减少 42.5%）。150³ 网格约节省 57 MB。

未收窄的矩阵：
- `level_matrix_`：哨兵值 `10×cutoff` 可能超过 int16 范围，保留 `int`。
- `minID_C_matrix_`：cluster ID 数量不确定，保留 `int`。

---

### 2. ClusterPoint 结构体收窄（`include/types.h`）

将 `ClusterPoint` 的 9 个字段从全部 `int` 改为窄类型：

```
// 原：9 × int = 36 字节
// 新：int16_t×4 + int8_t×5 = 13 字节（对齐后 14 字节）

int16_t x, y, z      // 网格坐标，上限 32767 足够
int16_t level         // energy level
int8_t  boundary      // 0 或 1
int8_t  ts_flag       // 0 或 1
int8_t  cross_i/j/k  // PBC 偏移，同 Grid 矩阵
```

**效果**：每个 cluster 点节省 22 字节，内存占用减少 61%。系统规模越大，收益越显著。

---

### 3. get_cluster() 改为 O(1) 哈希查找（`include/cluster.h`, `src/cluster.cpp`）

原 `get_cluster(id)` 通过遍历 `clusters_` 向量线性查找，在 `grow_clusters` 热循环中对每个邻居都调用一次，是明显的时间瓶颈。

**改法**：新增 `std::unordered_map<int, size_t> cluster_index_`，记录 cluster ID 到向量下标的映射。在以下位置维护该索引：
- `initiate_clusters()`：新建 cluster 后写入
- `merge_clusters()`：删除被合并 cluster 的条目
- `compact_clusters()`：重新编号后完整重建

**效果**：O(N_clusters) → O(1)，消除热循环中最主要的线性扫描。

---

### 4. find_merge_group() 改为 O(1) 哈希查找（`src/cluster.cpp`）

原 `find_merge_group(id)` 和 `init_merge_group(id)` 通过双层嵌套循环扫描所有 merge group 及其成员，复杂度 O(N_groups × group_size)。

**改法**：新增 `std::unordered_map<int, int> cluster_to_merge_group_`，记录 cluster ID 到所在 merge group 下标的映射。在所有 group 合并和 swap-with-last 操作中维护该映射，`compact_clusters()` 后完整重建。

**效果**：每次邻居相遇时的 merge group 查找从 O(N·M) 降至 O(1)。

---

### 5. tunnel_cluster 成员检查改为 O(1)（`src/cluster.cpp`）

`grow_clusters` 中检查某 cluster 是否已在 `tunnel_cluster` 列表里，原为线性遍历。

**改法**：在 `grow_clusters` 开始时从 `tunnel_cluster` 向量构建一个局部 `std::unordered_set<int> tunnel_cluster_set`，插入新条目时同步更新 set 和 vector。

**效果**：O(N) 成员检查 → O(1)。

---

### 6. 热循环内邻居数组改为栈分配（`src/cluster.cpp`）

`initiate_clusters()` 和 `grow_clusters()` 的内层循环中，每次迭代都用 `std::vector<NeighborInfo>` 存储 6 个固定邻居，导致频繁堆分配。

**改法**：改为 `NeighborInfo neighbors[6]`（栈数组），与 `tunnel.cpp` 中 `NbInfo neighbors[6]` 的既有风格一致。

**效果**：消除热循环中的堆分配开销，减少内存分配器压力。

---

### 7. 调试用全网格扫描加环境变量守卫（`src/cluster.cpp`）

`initiate_clusters()` 开头有一段仅用于打印调试信息的全网格扫描（遍历整个 grid 统计当前 level 的点数），在正常运行中毫无意义但每级都会执行。

**改法**：用环境变量 `TUTRAST_DEBUG_VERBOSE=1` 守卫，默认跳过。

---

### 8. Volume 计数改为预计算累积直方图（`src/main.cpp`）

主循环每级都执行一次完整的三重嵌套扫描来统计 `volume`（满足 `level_at <= current_level` 的点数），总复杂度 O(N_levels × N_grid)。

**改法**：在主循环之前一次性建立 level 频次直方图并计算累积和。循环内通过 `cum_volume[level]` 直接 O(1) 查询。

**效果**：O(N_levels × N_grid) → O(N_grid)（一次性预计算）+ O(1)/级。

---

### 9. TS 去重改为哈希集合（`src/transition_state.cpp`）

`organize_ts_groups()` 中对 TS 点去重的双层嵌套循环，复杂度 O(N²)。

**改法**：以 `(x, y, z, min(c1,c2), max(c1,c2))` 为 key 构造 `std::unordered_set`，单次遍历完成去重，语义完全等价（保留最先出现的条目）。

**效果**：O(N²) → O(N)。

---

### 10. flood_fill_ts 改为空间哈希索引（`src/transition_state.cpp`, `include/transition_state.h`）

`flood_fill_ts()` 的 BFS 中，每弹出一个点就遍历全部 TS 点寻找 26-邻居，复杂度 O(N_ts²)，是整个程序最严重的时间瓶颈之一。

**改法**：在 `organize_ts_groups()` 中预建空间哈希表 `unordered_map<int64_t, vector<size_t>>`，以编码后的 3D 坐标为 key。BFS 中将 26 个邻居坐标（含 PBC 折叠）逐一查表，每步复杂度 O(26)。

**效果**：O(N_ts²) → O(N_ts × 26) ≈ O(N_ts)。

---

### 11. KMC 内存诊断输出（`src/kmc.cpp`）

`run_and_compute_msd()` 中的 `position_history` 缓冲区大小为 `history_size × n_particles × 24 字节`，对长步骤 KMC（10⁷ 步）可达数 GB。原代码无任何提示。

**改法**：在分配前打印实际占用 MB 数，方便用户在运行前预判内存需求。

---

### 12. std::endl → '\n'（所有 .cpp 文件）

`std::endl` 每次调用都强制刷新缓冲区，在打印密集的循环中（如每级的 cluster 统计）会产生大量不必要的系统调用。

**改法**：将所有 `std::cout` 路径上的 `std::endl` 替换为 `'\n'`；`std::cerr` 错误路径保留 `std::endl`（需要即时输出）。

---

## 验证方法

对相同输入运行两个版本，用固定随机种子保证 KMC 可复现：

```bash
# 在输入文件目录下分别运行两个版本
TUTRAST_KMC_SEED=42 /path/to/sTuTraSt/build/tutrast
TUTRAST_KMC_SEED=42 /path/to/sTuTraStcc/build/tutrast

# diff 所有输出文件
diff processes_*.dat  BT.dat  basis.dat  TS_data.out  tunnel_info.out  Evol_*.dat  D_ave_*.dat  T*_msd*.dat
```

所有文件应完全一致。

---

## 未改动的部分

以下潜在优化因涉及逻辑改动或风险较高而未实施：

- **KMC position_history 缓冲区缩减**：该缓冲区是滑动窗口 MSD 算法的核心，缩减会改变输出。用户可通过控制 `n_steps` 来管理内存。
- **remap_and_prune_ts 延迟批量处理**：每次 merge 后 TS 重映射同时负责清除 `ts_matrix` 网格状态，无法简单延迟。
- **ClusterPoint 从 Cluster 中剥离 interior 点**：interior 点在 Boltzmann 分配函数计算中仍需使用，不可丢弃。
