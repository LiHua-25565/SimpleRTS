#include "move_system.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "rvo_adapter.h"
#include <map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <queue>

// 全局流场缓存
static std::unordered_map<uint64_t, std::vector<std::vector<Vector2>>> goal_flow_cache;
// 局部流场缓存
static std::unordered_map<uint64_t, std::vector<std::vector<Vector2>>> local_flow_cache;

// 工具函数
static uint64_t vec_to_key(const Vector2& vec) {
    return (static_cast<uint64_t>(static_cast<int>(vec.x * 100.f)) << 32
        | static_cast<uint64_t>(static_cast<int>(vec.y * 100.f)));
}

static inline float dot(const Vector2& a, const Vector2& b) {
    return a.x * b.x + a.y * b.y;
}

static int calc_max_cols(int N) {
    return std::max(4, (int)std::ceil(std::sqrt(N) * 1.2f));
}

// 可达性修正（按需 BFS，避开水和动态实体）
static bool is_cell_blocked_by_entity(int gx, int gy, const GameMap* map) {
    float cell_size = (float)map->get_cell_size();
    CollisionBox cell_box{ { gx * cell_size, gy * cell_size }, cell_size, cell_size };
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (const auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Structure>() && !obj->get_component<Harvestable>())
            continue;
        if (obj->get_collision_box().intersects(cell_box))
            return true;
    }
    return false;
}

static Vector2 make_target_reachable(const Vector2& ideal, const GameMap* map) {
    int cell_size = map->get_cell_size();
    int w = map->get_width(), h = map->get_height();
    int gx = (int)(ideal.x / cell_size), gy = (int)(ideal.y / cell_size);
    gx = std::clamp(gx, 0, w - 1); gy = std::clamp(gy, 0, h - 1);

    if (map->is_cell_passable(gx, gy) && !is_cell_blocked_by_entity(gx, gy, map))
        return ideal;

    std::vector<std::vector<bool>> visited(h, std::vector<bool>(w, false));
    std::queue<std::pair<int, int>> q;
    q.push({ gx, gy }); visited[gy][gx] = true;
    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        if (map->is_cell_passable(x, y) && !is_cell_blocked_by_entity(x, y, map)) {
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
    std::unordered_map<uint64_t, std::vector<GameObject*>> flow_units;
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (mv && mv->is_moving()) flow_units[vec_to_key(mv->flow_target)].push_back(obj);
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
    std::unordered_set<uint64_t> active;
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (mv && mv->is_moving()) active.insert(vec_to_key(mv->flow_target));
    }
    for (auto it = goal_flow_cache.begin(); it != goal_flow_cache.end(); ) {
        if (!active.count(it->first)) it = goal_flow_cache.erase(it);
        else ++it;
    }
    for (auto key : active) {
        if (!goal_flow_cache.count(key)) {
            float x = ((float)(int)(key >> 32)) / 100.0f;
            float y = ((float)(int)(key & 0xFFFFFFFF)) / 100.0f;
            goal_flow_cache[key] = map->generate_goal_flow_field({ x, y });
        }
    }
}

void MoveSystem::update_local_flow_cache() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    std::unordered_set<uint64_t> active_local;
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (!mv || !mv->is_moving()) continue;
        float dist = (mv->target - obj->get_collision_box().get_center_position()).length();
        if (dist < LOCAL_FLOW_RADIUS_CELLS * map->get_cell_size())
            active_local.insert(vec_to_key(mv->target));
    }
    for (auto it = local_flow_cache.begin(); it != local_flow_cache.end(); ) {
        if (!active_local.count(it->first)) it = local_flow_cache.erase(it);
        else ++it;
    }
}

Vector2 MoveSystem::get_flow_direction(const GameObject* unit, const Vector2& target,
    const Vector2& flow_target, float dist_to_target) {
    const CollisionBox& cb = unit->get_collision_box();
    if (dist_to_target < LOCAL_FLOW_RADIUS_CELLS * map->get_cell_size()) {
        uint64_t key = vec_to_key(target);
        auto it = local_flow_cache.find(key);
        if (it == local_flow_cache.end()) {
            int radius_cells = (int)(LOCAL_FLOW_RADIUS_CELLS + 2);
            local_flow_cache[key] = map->generate_local_flow_field(target, (float)radius_cells);
            it = local_flow_cache.find(key);
        }
        if (it != local_flow_cache.end()) {
            Vector2 dir = map->sample_flow_from_box(it->second, cb);
            if (dir.length() > 0.01f) return dir;
        }
    }
    auto it_global = goal_flow_cache.find(vec_to_key(flow_target));
    if (it_global != goal_flow_cache.end()) {
        Vector2 dir = map->sample_flow_from_box(it_global->second, cb);
        if (dir.length() > 0.01f) return dir;
    }
    return { 0.0f, 0.0f };
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

// ========== 核心移动逻辑（RVO 驱动） ==========
void MoveSystem::move_units() {
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    auto* rvo = RVOAdapter::instance();
    const float fixed_dt = rvo->get_fixed_timestep();  // 获取固定步长

    // 1. 确保模拟器就绪（必要时重建）
    rvo->ensure_sim_ready();

    // 2. 计算首选速度并设置
    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (!mv) continue;

        const CollisionBox& cb = obj->get_collision_box();
        Vector2 center = cb.get_center_position();
        Vector2 target = mv->target;
        float dist = (target - center).length();

        if (!mv->is_moving() || dist < 5.0f) {
            rvo->set_pref_velocity(id, { 0,0 });
            if (dist < 5.0f) {
                mv->stop();
                mv->velocity = { 0,0 };
            }
            continue;
        }

        Vector2 pref_vel;
        if (mv->is_arranging && mv->formation_slot >= 0 &&
            mv->formation_slot < (int)m_formation_slots.size()) {
            Vector2 slot = m_formation_slots[mv->formation_slot];
            Vector2 to_slot = slot - center;
            if (to_slot.length() < 5.0f)
                pref_vel = { 0,0 };
            else
                pref_vel = to_slot.normalize() * mv->speed;
        }
        else {
            Vector2 flow_dir = get_flow_direction(obj, target, mv->flow_target, dist);
            if (flow_dir.length() < 0.01f) {
                rvo->set_pref_velocity(id, { 0,0 });
                mv->velocity = { 0,0 };
                continue;
            }
            pref_vel = flow_dir * mv->speed;
        }
        rvo->set_pref_velocity(id, pref_vel);
    }

    // 3. 执行 RVO 步进
    rvo->do_step();

    // 4. 回写速度并用固定步长更新位置
    float map_w = (float)map->get_width() * map->get_cell_size();
    float map_h = (float)map->get_height() * map->get_cell_size();

    for (auto& [id, obj] : pool) {
        auto* mv = obj->get_component<Movable>();
        if (!mv) continue;

        Vector2 rvo_vel = rvo->get_agent_velocity(id);
        mv->velocity = rvo_vel;

        if (!mv->is_moving()) continue;

        CollisionBox cb = obj->get_collision_box();
        Vector2 new_pos = cb.position + mv->velocity * fixed_dt;  // 固定步长
        new_pos.x = std::max(0.0f, std::min(new_pos.x, map_w - cb.width));
        new_pos.y = std::max(0.0f, std::min(new_pos.y, map_h - cb.height));
        cb.position = new_pos;
        obj->set_collision_box(cb);
    }
}
// 主更新入口
//void MoveSystem::on_update(float delta) {
//    if (!map) return;
//    m_arrange_radius = 5.0f * map->get_cell_size();  // 进入排列的半径
//
//    correct_unwalkable_targets();
//    update_global_flow_cache();
//    update_local_flow_cache();
//    move_units();
//    arrange_units(delta);   // 分配槽位，设置 is_arranging
//}

void MoveSystem::on_update(float delta)
{
    auto obj_pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id,obj] : obj_pool)
    {
        if (!obj->check_valid()) continue;
        auto movable = obj->get_component<Movable>();
        if (!movable || !movable->is_moving()) continue;

        auto animation = obj->get_component<ImpactAnimation>();
        if (animation)
            animation->is_attacking = false;

        Vector2 center_pos = obj->get_collision_box().get_center_position();
        Vector2 dir = movable->target - center_pos;
        float dist = dir.length();
        if (dist < 1.0f) {
            movable->stop();
            continue;
        }

        float step = movable->speed * delta;
        if (step > dist) step = dist;

        Vector2 pos = obj->get_collision_box().position;
        movable->velocity = dir.normalize() * step;
        pos += movable->velocity;

        obj->set_position(pos);
    }
}