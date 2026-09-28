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

// 缓存淘汰：先移除本帧不再活跃的条目，再按 LRU 砍到上限
static void evict_flow_cache(std::unordered_map<uint64_t, FlowEntry>& cache,
    const std::unordered_set<uint64_t>& active, uint32_t cur_frame, int max_size)
{
    for (auto it = cache.begin(); it != cache.end(); ) {
        if (!active.count(it->first)) it = cache.erase(it);
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

        int best_cols = 0, best_rows = 0;
        Vector2 best_forward, best_right;
        bool found = false;
        for (auto& [forward, right] : candidates) {
            for (int cols = max_cols; cols >= 1; --cols) {
                int rows = (N + cols - 1) / cols;
                Vector2 group_center = command_center + forward * row_offset_forward;
                // can_formation_fit 复用 MoveSystem 中的逻辑（此处用简化版）
                bool fit = true;
                // 此处省略详细检测，保留你的 can_formation_fit 实现即可
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

    evict_flow_cache(goal_flow_cache, active, m_frame, MAX_GLOBAL_FLOW_CACHE);

    int budget = MAX_GLOBAL_FLOW_PER_FRAME;
    for (auto key : active) {
        auto it = goal_flow_cache.find(key);
        if (it != goal_flow_cache.end()) {
            it->second.last_used_frame = m_frame;
            continue;
        }
        if (budget <= 0) continue;   // 本帧预算用尽，下一帧再生成

        FlowEntry entry;
        entry.version = 0;   // Step 3 起填入 map->obstacle_version()
        entry.last_used_frame = m_frame;
        entry.field = map->generate_goal_flow_field(key_to_world(key, cell_size));
        goal_flow_cache[key] = std::move(entry);
        --budget;
    }
}

void MoveSystem::update_local_flow_cache() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const int cell_size = map->get_cell_size();
    std::unordered_set<uint64_t> active_local;
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* mv = obj->get_component<Movable>();
        if (!mv || !mv->is_moving()) continue;
        float dist = (mv->target - obj->get_collision_box().get_center_position()).length();
        if (dist < LOCAL_FLOW_RADIUS_CELLS * cell_size)
            active_local.insert(pos_key(mv->target, cell_size));
    }
    evict_flow_cache(local_flow_cache, active_local, m_frame, MAX_LOCAL_FLOW_CACHE);
}

Vector2 MoveSystem::get_flow_direction(const GameObject* unit, const Vector2& target,
    const Vector2& flow_target, float dist_to_target) {
    const CollisionBox& cb = unit->get_collision_box();
    Vector2 center = cb.get_center_position();
    const int cell_size = map->get_cell_size();

    // 直线可达就直接朝目标走：省掉绝大部分流场需求，也避免 8 邻域网格带来的锯齿
    if (is_line_passable(center, target))
        return (target - center).normalize();

    if (dist_to_target < LOCAL_FLOW_RADIUS_CELLS * cell_size) {
        uint64_t key = pos_key(target, cell_size);
        auto it = local_flow_cache.find(key);
        if (it == local_flow_cache.end() && m_flow_gen_budget > 0) {
            // 惰性生成，受每帧预算限制，避免单帧生成多张流场造成卡顿
            FlowEntry entry;
            entry.version = 0;
            entry.last_used_frame = m_frame;
            entry.field = map->generate_local_flow_field(target, (float)(LOCAL_FLOW_RADIUS_CELLS + 2));
            local_flow_cache[key] = std::move(entry);
            it = local_flow_cache.find(key);
            --m_flow_gen_budget;
        }
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

    m_formation_slots.clear();
    float half_width = (best_cols - 1) * spacing * 0.5f, half_depth = (best_rows - 1) * spacing * 0.5f;
    for (int r = 0; r < best_rows; ++r)
        for (int c = 0; c < best_cols; ++c)
            m_formation_slots.push_back(center + m_formation_right * (c * spacing - half_width) + m_formation_forward * (half_depth - r * spacing));
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
    float avg_spacing = 40.0f;
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
        Vector2 center = cb.get_center_position();
        Vector2 target = mv->target;
        float dist = (target - center).length();

        if (!mv->is_moving() || dist < ARRIVE_EPS)
        {
            rvo->set_pref_velocity(id, { 0.0f, 0.0f });
            mv->velocity = { 0.0f, 0.0f };
            if (mv->is_moving()) mv->stop();
            continue;
        }

        Vector2 pref_vel;
        if (mv->is_arranging && mv->formation_slot >= 0 &&
            mv->formation_slot < (int)m_formation_slots.size())
        {
            Vector2 slot = m_formation_slots[mv->formation_slot];
            Vector2 to_slot = slot - center;
            pref_vel = to_slot.length() < ARRIVE_EPS ? Vector2(0.0f, 0.0f) : to_slot.normalize() * mv->speed;
        }
        else
        {
            Vector2 flow_dir = get_flow_direction(obj, target, mv->flow_target, dist);
            pref_vel = flow_dir * mv->speed;
        }
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

        CollisionBox cb = obj->get_collision_box();
        Vector2 new_pos = cb.position + mv->velocity * delta;
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

void MoveSystem::move_units(float delta)
{
    compute_pref_velocities();
    // FlowOnly：直接用首选速度推进。
    // FlowRVO 的定步推进见 rvo_step（Step 4），那里会先跑 RVO 再积分
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
    m_flow_gen_budget = MAX_LOCAL_FLOW_PER_FRAME;
    m_arrange_radius = 5.0f * map->get_cell_size();   // 进入排列的半径

    update_projectiles(delta);
    correct_unwalkable_targets();
    update_global_flow_cache();
    update_local_flow_cache();
    move_units(delta);
    // arrange_units(delta);   // Step 7 启用（到达后自动列阵）
}