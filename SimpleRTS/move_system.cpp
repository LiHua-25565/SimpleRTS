#include "move_system.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "rvo_adapter.h"
#include "util.h"
#include <map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <queue>

// 流场缓存条目
// version        : 障碍版本号，地图障碍变化后据此失效（Step 3 启用）
// last_used_frame: 最近使用帧，用于超上限时按 LRU 淘汰
struct FlowEntry {
    uint32_t version = 0;
    uint32_t last_used_frame = 0;
    std::vector<std::vector<Vector2>> field;
};

// 全局流场缓存
static std::unordered_map<uint64_t, FlowEntry> goal_flow_cache;
// 局部流场缓存
static std::unordered_map<uint64_t, FlowEntry> local_flow_cache;

// 工具函数
static uint64_t cell_key(int gx, int gy) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(gx)) << 32)
        | static_cast<uint64_t>(static_cast<uint32_t>(gy));
}

// 位置 → 格子键。按格子量化，避免浮点抖动导致缓存永不命中
static uint64_t pos_key(const Vector2& vec, int cell_size) {
    return cell_key((int)(vec.x / cell_size), (int)(vec.y / cell_size));
}

// 格子键 → 该格中心的世界坐标
static Vector2 key_to_world(uint64_t key, int cell_size) {
    int gx = (int)(key >> 32);
    int gy = (int)(key & 0xFFFFFFFFu);
    return Vector2(gx * cell_size + cell_size * 0.5f, gy * cell_size + cell_size * 0.5f);
}

// 缓存淘汰：先移除本帧不再活跃或已过期（障碍版本变化）的条目，再按 LRU 砍到上限
static void evict_flow_cache(std::unordered_map<uint64_t, FlowEntry>& cache,
    const std::unordered_set<uint64_t>& active, uint32_t cur_frame, uint32_t cur_version, int max_size)
{
    for (auto it = cache.begin(); it != cache.end(); ) {
        if (!active.count(it->first) || it->second.version != cur_version) it = cache.erase(it);
        else ++it;
    }
    while ((int)cache.size() > max_size) {
        auto victim = cache.end();
        uint32_t oldest = 0xFFFFFFFFu;
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.last_used_frame < oldest) {
                oldest = it->second.last_used_frame;
                victim = it;
            }
        }
        if (victim == cache.end()) break;
        cache.erase(victim);
    }
}

// 单位中心所在格是否可通行。
// 不用整个碰撞盒：单位（20~32px）比格子（10px）大，贴着障碍走时必然部分重叠，
// 用整个盒子判断会把正常的贴边行走误判为不可通行；精细分离交给 RVO。
static bool center_passable(const Vector2& center, const GameMap* map)
{
    int cs = map->get_cell_size();
    return map->is_cell_passable((int)(center.x / cs), (int)(center.y / cs));
}

static inline float dot(const Vector2& a, const Vector2& b) {
    return a.x * b.x + a.y * b.y;
}

// 从 center 沿 dir 走，最多走 max_dist，返回真正"走得通"的距离。
// 沿路径多点采样（只检查终点会漏掉中途擦到的障碍格）。
// 这是切向逃逸的"传感器"：告诉转向逻辑前方还有多少余量
static float clearance(const GameMap* map, const Vector2& center, const Vector2& dir, float max_dist)
{
    const float step = (float)map->get_cell_size() * 0.5f;
    for (float t = step; t <= max_dist; t += step)
        if (!center_passable(center + dir * t, map))
            return t - step;          // 该点之前都是通的
    return max_dist;                  // 全程通畅
}

// 沿障碍物边缘的切向逃逸方向（所有偏转候选都被堵死时的兜底）。
// 用周围一圈的不可通行方向之和估计障碍法线 n，再取 n 的垂线作为切线，
// 并选与原前进方向同侧的那一头，保证"绕着走"而不是"往回走"
static Vector2 wall_tangent(const GameMap* map, const Vector2& center, const Vector2& desired_dir)
{
    const float r = (float)map->get_cell_size() * 2.0f;
    Vector2 n(0.0f, 0.0f);
    for (int k = 0; k < 8; ++k) {
        float a = k * 0.7853982f;                 // 45° 一档
        Vector2 d(std::cos(a), std::sin(a));
        if (!center_passable(center + d * r, map)) n = n + d;
    }
    Vector2 t;
    if (n.length() > 0.01f) {
        Vector2 nn = n.normalize();
        t = Vector2(-nn.y, nn.x);                 // 法线的垂线 = 障碍边缘方向
    }
    else {
        t = Vector2(-desired_dir.y, desired_dir.x);
    }
    if (dot(t, desired_dir) < 0.0f) t = Vector2(-t.x, -t.y);
    return t.length() > 0.01f ? t.normalize() : desired_dir;
}

static int calc_max_cols(int N) {
    return std::max(4, (int)std::ceil(std::sqrt(N) * 1.2f));
}

// 可达性修正（按需 BFS）。建筑与资源已写入 dynamic_obstacle_field，
// 因此直接用 is_cell_passable 即可，无需再遍历实体
static Vector2 make_target_reachable(const Vector2& ideal, const GameMap* map) {
    int cell_size = map->get_cell_size();
    int w = map->get_width(), h = map->get_height();
    int gx = (int)(ideal.x / cell_size), gy = (int)(ideal.y / cell_size);
    gx = std::clamp(gx, 0, w - 1); gy = std::clamp(gy, 0, h - 1);

    if (map->is_cell_passable(gx, gy))
        return ideal;

    std::vector<std::vector<bool>> visited(h, std::vector<bool>(w, false));
    std::queue<std::pair<int, int>> q;
    q.push({ gx, gy }); visited[gy][gx] = true;
    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        if (map->is_cell_passable(x, y)) {
            return { x * cell_size + cell_size * 0.5f, y * cell_size + cell_size * 0.5f };
        }
        for (auto d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (!visited[ny][nx]) { visited[ny][nx] = true; q.push({ nx, ny }); }
        }
    }
    return ideal;
}

// 以 center 为中心生成 cols × rows 的矩形阵列槽位
static std::vector<Vector2> make_grid_slots(const Vector2& center, const Vector2& fwd,
    const Vector2& right, int cols, int rows, float spacing)
{
    std::vector<Vector2> slots;
    slots.reserve((size_t)cols * (size_t)rows);
    float half_width = (cols - 1) * spacing * 0.5f;
    float half_depth = (rows - 1) * spacing * 0.5f;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            slots.push_back(center + right * (c * spacing - half_width) + fwd * (half_depth - r * spacing));
    return slots;
}

// 该阵列是否放得下：槽位大多可通行，且不压在建筑/资源上。
// obstacles 由调用方一次性查询后传入，避免每个候选方向都查一次四叉树
static bool can_formation_fit(const std::vector<Vector2>& slots, const GameMap* map,
    const std::vector<GameObject*>& obstacles, float margin)
{
    if (slots.empty()) return false;

    int passable = 0;
    for (const Vector2& s : slots) {
        int gx = (int)(s.x / map->get_cell_size());
        int gy = (int)(s.y / map->get_cell_size());
        if (map->is_cell_passable(gx, gy)) ++passable;
    }
    if ((float)passable / (float)slots.size() < 0.8f) return false;

    for (GameObject* o : obstacles) {
        if (!o || !o->check_valid()) continue;   // 四叉树可能返回已销毁的实体
        if (!o->get_component<Structure>() && !o->get_component<Harvestable>()) continue;
        const CollisionBox& ob = o->get_collision_box();
        float l = ob.position.x - margin, r = ob.position.x + ob.width + margin;
        float t = ob.position.y - margin, b = ob.position.y + ob.height + margin;
        for (const Vector2& s : slots) {
            if (s.x >= l && s.x <= r && s.y >= t && s.y <= b) return false;
        }
    }
    return true;
}

// ========== 编队目标点计算（输入系统使用） ==========
std::unordered_map<GameObject*, Vector2> compute_formation_targets(
    const std::vector<GameObject*>& selected_units,
    const Vector2& command_center,
    const GameMap* map)
{
    std::unordered_map<GameObject*, Vector2> targets;
    if (selected_units.empty()) return targets;

    struct GroupKey {
        UnitEntityType category; int width, height;
        bool operator<(const GroupKey& o) const {
            if (category != o.category) return category < o.category;
            if (width != o.width) return width < o.width;
            return height < o.height;
        }
    };
    std::map<GroupKey, std::vector<GameObject*>> groups;
    for (auto* unit : selected_units) {
        auto* type = unit->get_component<UnitType>();
        UnitEntityType cat = type ? type->type : UnitEntityType::Villager;
        const auto& box = unit->get_collision_box();
        int w = (int)(box.width / 2) * 2, h = (int)(box.height / 2) * 2;
        groups[{cat, w, h}].push_back(unit);
    }

    Vector2 overall_center(0.0f, 0.0f);
    for (auto* u : selected_units) overall_center += u->get_collision_box().get_center_position();
    overall_center.x /= selected_units.size(); overall_center.y /= selected_units.size();
    Vector2 to_target = command_center - overall_center;
    Vector2 default_forward(1.0f, 0.0f);
    if (to_target.length() > 10.0f) default_forward = to_target.normalize();
    Vector2 default_right(default_forward.y, -default_forward.x);

    std::vector<std::pair<Vector2, Vector2>> candidates;
    candidates.push_back({ default_forward, default_right });
    const Vector2 dirs[8] = {
        {1,0}, {0,1}, {-1,0}, {0,-1},
        {0.707f,0.707f}, {-0.707f,0.707f}, {-0.707f,-0.707f}, {0.707f,-0.707f}
    };
    for (auto& d : dirs) {
        Vector2 r(d.y, -d.x);
        bool dup = false;
        for (auto& c : candidates) if (std::abs(c.first.x - d.x) < 0.01f && std::abs(c.first.y - d.y) < 0.01f) dup = true;
        if (!dup) candidates.push_back({ d, r });
    }

    float row_offset_forward = 0.0f;
    for (auto& [key, units] : groups) {
        int N = (int)units.size();
        float spacing = std::max(key.width, key.height) * 1.8f;
        int max_cols = calc_max_cols(N);

        // 只查一次附近建筑/资源，供下面所有候选方向复用
        float extent = max_cols * spacing;
        CollisionBox search_area{ { command_center.x - extent, command_center.y - extent },
                                  extent * 2.0f, extent * 2.0f };
        std::vector<GameObject*> obstacles;
        WorldEntityMgr::instance()->query_area(search_area, obstacles);

        int best_cols = 0, best_rows = 0;
        Vector2 best_forward, best_right;
        bool found = false;
        for (auto& [forward, right] : candidates) {
            for (int cols = max_cols; cols >= 1; --cols) {
                int rows = (N + cols - 1) / cols;
                Vector2 group_center = command_center + forward * row_offset_forward;
                bool fit = can_formation_fit(make_grid_slots(group_center, forward, right, cols, rows, spacing),
                    map, obstacles, spacing * 0.5f);
                if (fit) {
                    if (!found || cols > best_cols || (cols == best_cols && rows < best_rows)) {
                        best_cols = cols; best_rows = rows;
                        best_forward = forward; best_right = right;
                        found = true;
                    }
                }
            }
        }
        if (!found) { best_cols = 1; best_rows = N; best_forward = default_forward; best_right = default_right; }

        Vector2 group_center(0.0f, 0.0f);
        for (auto* u : units) group_center += u->get_collision_box().get_center_position();
        group_center.x /= N; group_center.y /= N;

        struct Proj { GameObject* unit; float f, r; };
        std::vector<Proj> projs;
        for (auto* u : units) {
            Vector2 rel = u->get_collision_box().get_center_position() - group_center;
            projs.push_back({ u, dot(rel, best_forward), dot(rel, best_right) });
        }
        std::sort(projs.begin(), projs.end(), [](const Proj& a, const Proj& b) {
            if (std::abs(a.f - b.f) > 0.1f) return a.f > b.f;
            return a.r < b.r;
            });

        int base = N / best_rows, rem = N % best_rows;
        std::vector<int> row_counts(best_rows, base);
        for (int i = 0; i < rem; ++i) row_counts[i]++;

        std::vector<Vector2> grid_points;
        float total_width = (best_cols - 1) * spacing, total_depth = (best_rows - 1) * spacing;
        Vector2 start_corner = command_center + best_forward * row_offset_forward
            + best_right * (-total_width * 0.5f) + best_forward * (total_depth * 0.5f);
        for (int r = 0; r < best_rows; ++r) {
            int row_count = row_counts[r];
            float row_width = (row_count - 1) * spacing;
            float row_start_offset = (total_width - row_width) * 0.5f;
            Vector2 row_start = start_corner - best_forward * (r * spacing) + best_right * row_start_offset;
            for (int c = 0; c < row_count; ++c)
                grid_points.push_back(row_start + best_right * (c * spacing));
        }

        for (int i = 0; i < N; ++i)
            targets[projs[i].unit] = make_target_reachable(grid_points[i], map);

        row_offset_forward += (best_rows + 1.5f) * spacing;
    }
    return targets;
}

// ======================= MoveSystem 成员函数 =======================

void MoveSystem::reset_caches() {
    goal_flow_cache.clear();
    local_flow_cache.clear();
    m_avoid_side.clear();
    m_line_ok.clear();
    m_frame = 0;
    m_rvo_accumulator = 0.0f;
}

// 带 TTL 的视线查询。
// is_line_passable 沿直线按 5px 采样，长距离下每单位每帧要查上百个格子，
// 60 个单位就是上万次 —— 实测占了移动系统约 1.5ms/帧。
// 而视线结果在 8 帧（≈55ms，单位只移动 3px）内几乎不会变化，
// 因此缓存它，让"流场准入判断"与"直线短路判断"共用同一次结果
bool MoveSystem::line_of_sight(uint64_t id, const Vector2& from, const Vector2& to)
{
    static constexpr uint32_t TTL = 8;
    auto it = m_line_ok.find(id);
    if (it != m_line_ok.end() && m_frame - it->second.first < TTL)
        return it->second.second;

    bool ok = is_line_passable(from, to);
    if (m_line_ok.size() > 512) m_line_ok.clear();   // 防止已销毁实体的条目堆积
    m_line_ok[id] = { m_frame, ok };
    return ok;
}

void MoveSystem::correct_unwalkable_targets() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const int cell_size = map->get_cell_size();
    std::unordered_map<uint64_t, std::vector<GameObject*>> flow_units;
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv || !mv->is_moving()) continue;
        if (mv->flow_target.x < 0.0f) continue;
        flow_units[pos_key(mv->flow_target, cell_size)].push_back(obj);
    }
    for (auto& [key, units] : flow_units) {
        if (units.empty()) continue;
        Vector2 orig = units[0]->get_component<Movable>()->flow_target;
        Vector2 corrected = map->find_nearest_passable(orig);
        if (corrected != orig) {
            for (auto* u : units) {
                auto* mv = u->get_component<Movable>();
                Vector2 offset = mv->target - orig;
                mv->flow_target = corrected;
                mv->target = map->find_nearest_passable(corrected + offset);
            }
        }
    }
}

void MoveSystem::update_global_flow_cache() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const int cell_size = map->get_cell_size();
    std::unordered_set<uint64_t> active;
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv || !mv->is_moving()) continue;
        // 只有编队命令（命令中心 != 个人目标点）才值得建全图流场。
        // 攻击/采集/送货时两者相同，走直线或局部流场即可，避免每单位一张全图流场
        if (mv->flow_target.x < 0.0f || mv->flow_target == mv->target) continue;
        active.insert(pos_key(mv->flow_target, cell_size));
    }

    evict_flow_cache(goal_flow_cache, active, m_frame, map->obstacle_version(), MAX_GLOBAL_FLOW_CACHE);

    int budget = MAX_GLOBAL_FLOW_PER_FRAME;
    for (auto key : active) {
        auto it = goal_flow_cache.find(key);
        if (it != goal_flow_cache.end()) {
            it->second.last_used_frame = m_frame;
            continue;
        }
        if (budget <= 0) continue;   // 本帧预算用尽，下一帧再生成

        FlowEntry entry;
        entry.version = map->obstacle_version();
        entry.last_used_frame = m_frame;
        entry.field = map->generate_goal_flow_field(key_to_world(key, cell_size));
        goal_flow_cache[key] = std::move(entry);
        --budget;
    }
}

void MoveSystem::update_local_flow_cache() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const int cell_size = map->get_cell_size();

    // key -> 该目标点最近单位的距离（用于按急迫程度排优先级）与最远距离（用于决定半径）
    std::unordered_map<uint64_t, float> active_min;
    std::unordered_map<uint64_t, float> active_max;
    std::unordered_map<uint64_t, Vector2> key_target;
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv || !mv->is_moving()) continue;
        Vector2 center = obj->get_collision_box().get_center_position();
        float dist = (mv->target - center).length();

        // 准入条件：目标在局部半径内，
        // **或者** 直线走不通（障碍横在中间）。
        // 第二条是关键：单位/障碍/目标连成一线时，若因距离太远而不生成流场，
        // get_flow_direction 就只能退化成"直冲障碍"，前面又只有 RVO 硬挡，
        // 最终表现为顶在墙上不动 —— 这就是"一线停滞"的结构性成因
        // 近距离单位本来就准入，不必再付一次射线检测的开销
        bool need_local = (dist < LOCAL_FLOW_RADIUS_CELLS * cell_size);
        if (!need_local && !line_of_sight(id, center, mv->target))
            need_local = true;
        if (!need_local) continue;

        uint64_t key = pos_key(mv->target, cell_size);
        auto it = active_min.find(key);
        if (it == active_min.end() || dist < it->second) active_min[key] = dist;
        auto it2 = active_max.find(key);
        if (it2 == active_max.end() || dist > it2->second) active_max[key] = dist;
        key_target[key] = mv->target;
    }

    std::unordered_set<uint64_t> active_keys;
    active_keys.reserve(active_min.size());
    for (auto& [k, d] : active_min) active_keys.insert(k);

    evict_flow_cache(local_flow_cache, active_keys, m_frame, map->obstacle_version(), MAX_LOCAL_FLOW_CACHE);

    // 集中生成（原来是在 get_flow_direction 里惰性生成，单帧可能同时生成多个造成卡顿尖峰）
    // 按距离由近到远优先，受每帧预算限制；未就绪的单位本帧回退到全局流场/直线
    std::vector<std::pair<float, uint64_t>> pending;
    pending.reserve(active_min.size());
    for (auto& [k, d] : active_min)
        if (!local_flow_cache.count(k)) pending.push_back({ d, k });
    std::sort(pending.begin(), pending.end());

    int budget = MAX_LOCAL_FLOW_PER_FRAME;
    for (auto& [d, k] : pending) {
        if (budget <= 0) break;
        // 半径自适应：必须覆盖到最远那个单位，否则它采样到的是全零场。
        // 上限 MAX_LOCAL_FLOW_RADIUS_CELLS 防止个别超远距离把 Dijkstra 撑爆
        float radius_cells = std::clamp(active_max[k] / (float)cell_size + 6.0f,
            LOCAL_FLOW_RADIUS_CELLS + 2.0f, MAX_LOCAL_FLOW_RADIUS_CELLS);
        FlowEntry entry;
        entry.version = map->obstacle_version();
        entry.last_used_frame = m_frame;
        entry.field = map->generate_local_flow_field(key_target[k], radius_cells);
        local_flow_cache[k] = std::move(entry);
        --budget;
    }
}

Vector2 MoveSystem::get_flow_direction(const GameObject* unit, const Vector2& target,
    const Vector2& flow_target, float dist_to_target) {
    const CollisionBox& cb = unit->get_collision_box();
    Vector2 center = cb.get_center_position();
    const int cell_size = map->get_cell_size();

    // 直线可达就直接朝目标走：省掉绝大部分流场需求，也避免 8 邻域网格带来的锯齿
    if (line_of_sight(unit->get_id(), center, target))
        return (target - center).normalize();

    if (dist_to_target < LOCAL_FLOW_RADIUS_CELLS * cell_size) {
        uint64_t key = pos_key(target, cell_size);
        auto it = local_flow_cache.find(key);
        if (it != local_flow_cache.end()) {
            it->second.last_used_frame = m_frame;
            Vector2 dir = map->sample_flow_from_box(it->second.field, cb);
            if (dir.length() > 0.01f) return dir;
        }
    }

    if (flow_target.x >= 0.0f) {
        auto it_global = goal_flow_cache.find(pos_key(flow_target, cell_size));
        if (it_global != goal_flow_cache.end()) {
            it_global->second.last_used_frame = m_frame;
            Vector2 dir = map->sample_flow_from_box(it_global->second.field, cb);
            if (dir.length() > 0.01f) return dir;
        }
    }

    // 兜底：流场不可用时仍朝目标方向走，碰撞交给 RVO 与分轴回退处理，
    // 直接返回零向量会让单位永久冻结
    Vector2 fallback = target - center;
    return fallback.length() > 0.01f ? fallback.normalize() : Vector2(0.0f, 0.0f);
}

// 排列系统（与之前相同，直接复用）
void MoveSystem::build_formation_grid(Vector2 center, int total_units, float spacing) {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    Vector2 group_center(0, 0);
    int count = 0;
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (mv && mv->is_arranging) { group_center += obj->get_collision_box().get_center_position(); count++; }
    }
    if (count == 0) { m_formation_forward = Vector2(1, 0); m_formation_right = Vector2(0, -1); }
    else {
        group_center.x /= count; group_center.y /= count;
        Vector2 to_target = center - group_center;
        if (to_target.length() > 0.01f) m_formation_forward = to_target.normalize();
        else m_formation_forward = Vector2(1, 0);
        m_formation_right = Vector2(m_formation_forward.y, -m_formation_forward.x);
    }
    m_formation_spacing = spacing;

    int max_cols = calc_max_cols(total_units);
    int best_cols = max_cols, best_rows = (total_units + best_cols - 1) / best_cols;
    while (best_cols > 1 && !map->is_cell_passable(center.x / map->get_cell_size(), center.y / map->get_cell_size())) {
        best_cols--; best_rows = (total_units + best_cols - 1) / best_cols;
    }
    m_formation_cols = best_cols; m_formation_rows = best_rows;

    m_formation_slots = make_grid_slots(center, m_formation_forward, m_formation_right,
        best_cols, best_rows, spacing);
    m_slot_occupied.assign(m_formation_slots.size(), false);
}

void MoveSystem::arrange_units(float delta) {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    std::vector<GameObject*> arranging;
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (mv && mv->is_arranging) arranging.push_back(obj);
    }
    if (arranging.empty()) return;

    Vector2 cmd_center = arranging[0]->get_component<Movable>()->target;
    // RVO 的 agent 半径约为外接圆半径的 0.8（32px 单位约 18px），
    // 间距必须明显大于两个半径之和，否则列阵时会持续互相推挤
    float avg_size = 0.0f;
    for (GameObject* u : arranging)
        avg_size += std::max(u->get_collision_box().width, u->get_collision_box().height);
    avg_size /= (float)arranging.size();
    float avg_spacing = avg_size * 1.8f;

    build_formation_grid(cmd_center, (int)arranging.size(), avg_spacing);

    std::sort(arranging.begin(), arranging.end(), [&](GameObject* a, GameObject* b) {
        float da = (cmd_center - a->get_collision_box().get_center_position()).length();
        float db = (cmd_center - b->get_collision_box().get_center_position()).length();
        return da < db;
        });

    for (auto* unit : arranging) {
        Vector2 pos = unit->get_collision_box().get_center_position();
        int best = -1; float best_dist = 1e9f;
        for (int i = 0; i < (int)m_formation_slots.size(); ++i) {
            if (m_slot_occupied[i]) continue;
            Vector2 s = m_formation_slots[i];
            if (!map->is_cell_passable(s.x / map->get_cell_size(), s.y / map->get_cell_size())) continue;
            float d = (s - pos).length();
            if (d < best_dist) { best_dist = d; best = i; }
        }
        if (best != -1) {
            m_slot_occupied[best] = true;
            unit->get_component<Movable>()->formation_slot = best;
        }
    }
}

// ========== 局部避障：切向逃逸 ==========
//
// 存在的问题（改进前）：
//   首选速度直接指向障碍时，RVO/ORCA 的可行速度空间会把"朝向障碍的分量"整块剪掉，
//   剩下的只有切向分量，其长度 = |pref|·cos(偏角)。正对障碍时 cos≈0，输出速度趋近 0。
//   而 RVO 只做"一步避让"，不会主动绕行，于是单位停在障碍前 —— 经典局部极小值。
//
// 做法：在把方向交给 RVO **之前**先做转向。发现前方受阻时，把前进方向偏转到一个
//   "既走得通、又尽量贴近原方向"的角度上，且**保持单位长度**（只改方向不降速）。
//   这样 RVO 拿到的是一个本来就安全的 pref，不需要再靠削减速度来避让。
Vector2 MoveSystem::steer_around_obstacles(uint64_t id, const Vector2& center,
    const Vector2& desired_dir, float probe_dist)
{
    if (desired_dir.length() < 0.01f) return desired_dir;

    // 快路径：前方通畅就原样返回。绝大多数帧走这里，开销只有一次 clearance
    if (clearance(map, center, desired_dir, probe_dist) >= probe_dist) {
        m_avoid_side.erase(id);
        return desired_dir;
    }

    // ---- 受阻：在 ±90° 范围内采样偏转方向 ----
    // CANDIDATES = 每侧候选数，MAX_DEFLECT = 最大偏转角。
    // 偏转角上限 90° 意味着宁可"横着贴墙走"也不后退，避免原地打转
    static constexpr int   CANDIDATES  = 4;
    static constexpr float MAX_DEFLECT = 1.5707963f;              // 90°
    const float step_angle = MAX_DEFLECT / (float)CANDIDATES;      // 22.5°

    const AvoidState* prev = nullptr;
    auto it = m_avoid_side.find(id);
    if (it != m_avoid_side.end() && it->second.until_frame > m_frame) prev = &it->second;
    const int locked_side = prev ? prev->side : 0;

    const float base_ang = std::atan2(desired_dir.y, desired_dir.x);
    float best_score = -1e9f, best_ang = base_ang;
    int   best_side = 0;

    for (int i = -CANDIDATES; i <= CANDIDATES; ++i) {
        if (i == 0) continue;
        const float ang = base_ang + i * step_angle;
        const Vector2 d(std::cos(ang), std::sin(ang));

        // 一出去就撞墙的方向直接弃用（至少要走完 2 格）
        const float clr = clearance(map, center, d, probe_dist);
        if (clr < (float)map->get_cell_size() * 2.0f) continue;

        const int side = (i > 0) ? 1 : -1;
        // 评分三项：
        //   clr/probe_dist          走得越远越好（优先选真正绕得出去的方向）
        //   cos(偏角)*0.35          越贴近原方向越好（避免绕远路）
        //   SIDE_BONUS              与上次绕行侧一致则加分，防止左右摇摆抖动
        float score = clr / probe_dist
            + std::cos(i * step_angle) * 0.35f
            + (locked_side != 0 && side == locked_side ? 0.30f : 0.0f);

        if (score > best_score) { best_score = score; best_ang = ang; best_side = side; }
    }

    Vector2 out;
    if (best_score < -1e8f) {
        // 全部候选都堵死（贴着墙的凹角、夹缝）：沿障碍边缘滑行
        out = wall_tangent(map, center, desired_dir);
        best_side = 0;
    }
    else {
        out = Vector2(std::cos(best_ang), std::sin(best_ang));
    }

    // 记忆绕行侧约 0.35 秒（144Hz ≈ 50 帧）。这是"迟滞"：一旦决定往左绕，
    // 短期内继续往左，直到前方重新通畅（快路径会清掉记忆），否则会左右抽搐
    m_avoid_side[id] = AvoidState{ best_side, m_frame + 50 };
    return out;
}

// ========== 核心移动逻辑 ==========

void MoveSystem::update_projectiles(float delta)
{
    auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
    float map_w = (float)map->get_width() * map->get_cell_size();
    float map_h = (float)map->get_height() * map->get_cell_size();

    for (auto& [id, obj] : obj_pool)
    {
        if (!obj->check_valid()) continue;
        auto* proj = obj->get_component<Projectile>();
        if (!proj) continue;
        auto* movable = obj->get_component<Movable>();
        if (!movable) continue;

        Vector2 new_pos = obj->get_collision_box().position + movable->velocity * delta;
        obj->set_position(new_pos);

        // 边界检查：超出地图则标记失效
        const auto& box = obj->get_collision_box();
        if (new_pos.x < -box.width - 100.0f || new_pos.y < -box.height - 100.0f ||
            new_pos.x > map_w + box.width + 100.0f || new_pos.y > map_h + box.height + 100.0f)
        {
            obj->set_valid(false);
        }
    }
}

void MoveSystem::compute_pref_velocities()
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    auto* rvo = RVOAdapter::instance();

    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        // 投射物由 update_projectiles 负责，不参与寻路
        if (obj->get_component<Projectile>()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv) continue;

        const CollisionBox& cb = obj->get_collision_box();
        const Vector2 center = cb.get_center_position();

        if (!mv->is_moving())
        {
            rvo->set_pref_velocity(id, { 0.0f, 0.0f });
            mv->velocity = { 0.0f, 0.0f };
            m_avoid_side.erase(id);
            continue;
        }

        // 到达：只有不在列阵状态时才判定结束（列阵有独立的目标点）
        if (!mv->is_arranging && (mv->target - center).length() < ARRIVE_EPS)
        {
            rvo->set_pref_velocity(id, { 0.0f, 0.0f });
            mv->velocity = { 0.0f, 0.0f };
            mv->stop();
            m_avoid_side.erase(id);
            continue;
        }

        Vector2 desired_dir;        // 期望前进方向（单位向量）
        float   dist_to_goal = 0.0f; // 到真正要去的点的距离（用于到达减速）

        if (mv->is_arranging && mv->formation_slot >= 0 &&
            mv->formation_slot < (int)m_formation_slots.size())
        {
            Vector2 to_slot = m_formation_slots[mv->formation_slot] - center;
            dist_to_goal = to_slot.length();
            desired_dir = dist_to_goal > 0.01f ? to_slot.normalize() : Vector2(0.0f, 0.0f);
        }
        else
        {
            float dist = (mv->target - center).length();
            dist_to_goal = dist;
            desired_dir = get_flow_direction(obj, mv->target, mv->flow_target, dist);
        }

        // ---- 切向逃逸：把撞墙的方向偏转到可通行的切向 ----
        // 只改方向、不降速。RVO 拿到的因此是本来就安全的首选速度，
        // 不需要靠削减速度大小来避让 —— 这是"靠近障碍就变慢"的治本手段
        float diagonal = std::sqrt(cb.width * cb.width + cb.height * cb.height);
        float probe_dist = std::max(diagonal, mv->speed * PROBE_TIME);
        desired_dir = steer_around_obstacles(id, center, desired_dir, probe_dist);

        // ---- 到达减速带 ----
        float slow_radius = mv->speed * ARRIVE_SLOW_TIME;
        float scale = (dist_to_goal < slow_radius)
            ? std::max(MIN_ARRIVE_SCALE, dist_to_goal / slow_radius) : 1.0f;

        Vector2 pref_vel = desired_dir * (mv->speed * scale);
        rvo->set_pref_velocity(id, pref_vel);
        // FlowOnly 直接采用首选速度；FlowRVO 会在 integrate 前用 RVO 结果覆盖
        mv->velocity = pref_vel;
    }
}

void MoveSystem::integrate_positions(float delta)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    auto* rvo = RVOAdapter::instance();
    float map_w = (float)map->get_width() * map->get_cell_size();
    float map_h = (float)map->get_height() * map->get_cell_size();

    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv) continue;

        // RVO 只负责出速度，位置始终以 ECS 为准
        if (mode_ == MoveModeKind::FlowRVO)
            mv->velocity = rvo->get_agent_velocity(id);

        if (!mv->is_moving()) continue;

        // ---- 速度限幅：每帧最多改变 max_dv，消除急停/急起与转向抖动 ----
        // RVO 每 0.05s 才刷新一次速度，定步切换的瞬间会有一个台阶；
        // 转向时方向突变也会产生抽搐。限幅后速度连续，观感平滑。
        // 注意只限制"变化量"，不降低目标速度，因此不影响通过能力
        {
            Vector2 dv = mv->velocity - mv->smooth_velocity;
            float max_dv = (std::max(mv->speed, 1.0f) / ACCEL_TIME) * delta;
            if (dv.length() > max_dv) dv = dv.length() > 0.01f ? dv.normalize() * max_dv : Vector2(0.0f, 0.0f);
            mv->smooth_velocity = mv->smooth_velocity + dv;
        }

        CollisionBox cb = obj->get_collision_box();
        Vector2 new_pos = cb.position + mv->smooth_velocity * delta;
        new_pos.x = std::max(0.0f, std::min(new_pos.x, map_w - cb.width));
        new_pos.y = std::max(0.0f, std::min(new_pos.y, map_h - cb.height));

        // 障碍回退：新位置的中心若进入不可通行格，先尝试单轴滑动，避免贴墙卡死。
        // 若当前中心本就不可通行（例如正站在资源格上），则放行，否则会永久卡住
        Vector2 half(cb.width * 0.5f, cb.height * 0.5f);
        if (center_passable(cb.position + half, map) && !center_passable(new_pos + half, map))
        {
            Vector2 try_x(new_pos.x, cb.position.y);
            Vector2 try_y(cb.position.x, new_pos.y);
            if (center_passable(try_x + half, map))      new_pos = try_x;
            else if (center_passable(try_y + half, map)) new_pos = try_y;
            else                                         new_pos = cb.position;
        }

        cb.position = new_pos;
        obj->set_collision_box(cb);
    }
}

void MoveSystem::rvo_step(float fixed_dt)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    auto* rvo = RVOAdapter::instance();

    // 1. 确保模拟器就绪（实体增删后会重建）
    rvo->ensure_sim_ready();

    // 2. 位置权威始终在 ECS：先把当前位置同步给 RVO，
    //    否则 RVO 内部积分出来的位置会与实体位置漂移
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        if (!obj->get_component<Movable>()) continue;
        rvo->set_agent_position(id, obj->get_collision_box().get_center_position());
    }

    // 3. 流场给出首选速度
    compute_pref_velocities();

    // 4. RVO 定步求解
    rvo->do_step();

    // 5. 只取回速度，位置不在这里积分（由 integrate_positions 用真实 delta 积分）
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv) continue;
        mv->velocity = rvo->get_agent_velocity(id);
    }
}

void MoveSystem::move_units(float delta)
{
    compute_pref_velocities();
    // FlowOnly：直接用首选速度推进。
    // FlowRVO 的定步推进见 rvo_step，这里不再重复 RVO 流程
    integrate_positions(delta);
}
void MoveSystem::on_update(float delta)
{
    if (!map) return;

    // 防止切后台回来后单帧步长过大导致穿墙
    if (delta > 0.1f) delta = 0.1f;

    // ===== Legacy 模式：直线移动，保持切回完整移动前的基线行为 =====
    if (mode_ == MoveModeKind::Legacy)
    {
        auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
        float map_w = (float)map->get_width() * map->get_cell_size();
        float map_h = (float)map->get_height() * map->get_cell_size();

        for (auto& [id, obj] : obj_pool)
        {
            if (!obj->check_valid()) continue;

            // ---- 投射物处理：仅负责移动与边界检查 ----
            if (auto* proj = obj->get_component<Projectile>())
            {
                auto* movable = obj->get_component<Movable>();
                if (movable)
                {
                    // 根据速度移动投射物
                    Vector2 new_pos = obj->get_collision_box().position + movable->velocity * delta;
                    obj->set_position(new_pos);

                    // 边界检查：超出地图则标记失效
                    const auto& box = obj->get_collision_box();
                    if (new_pos.x < -box.width - 100.0f || new_pos.y < -box.height - 100.0f ||
                        new_pos.x > map_w + box.width + 100.0f || new_pos.y > map_h + box.height + 100.0f)
                    {
                        obj->set_valid(false);
                        continue;
                    }
                }
                // 投射物跳过普通单位的移动逻辑
                continue;
            }

            // ---- 普通单位移动 ----
            auto* movable = obj->get_component<Movable>();
            if (!movable || !movable->is_moving()) continue;

            Vector2 center_pos = obj->get_collision_box().get_center_position();
            Vector2 dir = movable->target - center_pos;
            float dist = dir.length();
            if (dist < 1.0f)
            {
                movable->stop();
                continue;
            }

            float step = movable->speed * delta;
            if (step > dist) step = dist;

            Vector2 pos = obj->get_collision_box().position;
            movable->velocity = dir.normalize() * step;
            pos += movable->velocity;

            // 边界钳位
            if (pos.x < 0.0f) pos.x = 0.0f;
            if (pos.y < 0.0f) pos.y = 0.0f;
            if (pos.x > map_w - obj->get_collision_box().width)  pos.x = map_w - obj->get_collision_box().width;
            if (pos.y > map_h - obj->get_collision_box().height) pos.y = map_h - obj->get_collision_box().height;

            obj->set_position(pos);
        }
        return;
    }

    // ===== 流场寻路模式 =====
    ++m_frame;
    m_arrange_radius = 5.0f * map->get_cell_size();   // 进入排列的半径

    update_projectiles(delta);
    correct_unwalkable_targets();
    update_global_flow_cache();
    update_local_flow_cache();
    if (mode_ == MoveModeKind::FlowRVO)
    {
        // RVO 用固定步长做决策，位置仍按每帧真实 delta 积分：
        // 144Hz 下单帧位移约 0.42px，天然平滑；若用 0.1s 定步积分会一次跳 6px
        const float fixed_dt = RVOAdapter::instance()->get_fixed_timestep();
        if (fixed_dt > 0.0f)
        {
            m_rvo_accumulator += delta;
            int steps = 0;
            while (m_rvo_accumulator >= fixed_dt && steps < 3)
            {
                rvo_step(fixed_dt);
                m_rvo_accumulator -= fixed_dt;
                ++steps;
            }
            if (steps == 3) m_rvo_accumulator = 0.0f;   // 防死亡螺旋
            integrate_positions(delta);
        }
        else
        {
            move_units(delta);
        }
    }
    else
    {
        move_units(delta);   // FlowOnly
    }
    // arrange_units(delta);   // Step 7 启用（到达后自动列阵）
}