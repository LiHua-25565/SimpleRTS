#include "move_system.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>
#include <queue>

// 全局流场缓存（因为多个子步骤需要共享）
static std::unordered_map<uint64_t, std::vector<std::vector<Vector2>>> goal_flow_cache;

// 局部流场缓存（键：个人目标坐标键）
static std::unordered_map<uint64_t, std::vector<std::vector<Vector2>>> local_flow_cache;

// 工具函数 
static uint64_t vec_to_key(const Vector2& vec)
{
    return (static_cast<uint64_t>(static_cast<int>(vec.x * 100.f)) << 32
        | static_cast<uint64_t>(static_cast<int>(vec.y * 100.f)));
}

// 射线与 AABB 矩形的交点距离
static float ray_intersect_rect(const Vector2& origin, const Vector2& dir,
    float left, float top, float right, float bottom)
{
    float t1 = (left - origin.x) / dir.x;
    float t2 = (right - origin.x) / dir.x;
    if (dir.x == 0) { t1 = -1e30f; t2 = 1e30f; }
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);

    float t3 = (top - origin.y) / dir.y;
    float t4 = (bottom - origin.y) / dir.y;
    if (dir.y == 0) { t3 = -1e30f; t4 = 1e30f; }
    tmin = std::max(tmin, std::min(t3, t4));
    tmax = std::min(tmax, std::max(t3, t4));

    if (tmax >= tmin && tmax > 0)
        return (tmin > 0) ? tmin : tmax;
    return -1.0f;
}

// 为同一目标的编队设置不同真实目标点
std::unordered_map<GameObject*, Vector2> compute_formation_targets(
    const std::vector<GameObject*>& selected_units,
    const Vector2& command_center,
    const GameMap* map)
{
    std::unordered_map<GameObject*, Vector2> targets;
    if (selected_units.empty()) return targets;

    // 1. 按类别分组
    std::map<UnitCategory, std::vector<GameObject*>> groups;
    for (auto* unit : selected_units) {
        auto* type = unit->get_component<UnitType>();
        UnitCategory cat = type ? type->category : UnitCategory::Melee; // 默认近战
        groups[cat].push_back(unit);
    }

    const float spacing = map->get_cell_size() * 2.5f;  // 单位间距（2.5格）
    const int MAX_COLS = 8;                            // 最大列数
    float row_offset_y = 0.0f;                         // 不同类别之间的纵向偏移

    for (auto& [cat, units] : groups) {
        int N = (int)units.size();
        int cols = std::min(N, MAX_COLS);
        int rows = (N + cols - 1) / cols;                 // 向上取整

        float formation_width = (cols - 1) * spacing;
        float formation_height = (rows - 1) * spacing;

        // 左上角（世界坐标），之后整体居中于命令中心，并加上类别纵向偏移
        Vector2 top_left = command_center
            + Vector2(0.0f, row_offset_y)
            - Vector2(formation_width * 0.5f, formation_height * 0.5f);

        for (int r = 0; r < rows; ++r) {
            int row_count = (r == rows - 1) ? (N - r * cols) : cols;
            float row_width = (row_count - 1) * spacing;
            float row_start_x = top_left.x + (formation_width - row_width) * 0.5f;

            for (int c = 0; c < row_count; ++c) {
                GameObject* unit = units[r * cols + c];
                Vector2 ideal = { row_start_x + c * spacing,
                                  top_left.y + r * spacing };
                // 如果理想点不可通行（如水中），就近修正
                Vector2 final = map->find_nearest_passable(ideal);
                targets[unit] = final;
            }
        }
        // 为下一类别增加纵向偏移，避免重叠
        row_offset_y += (rows + 1) * spacing;
    }
    return targets;
}

// ========== 墙壁斥力 ==========
Vector2 MoveSystem::compute_wall_repulsion(const GameObject* unit)
{
    if (!unit->get_component<Movable>())
        return { 0.0f, 0.0f };
    const float RAY_LENGTH = 4.0f;
    const float ANGLE_OFFSET = 30.0f;
    const float REPULSION_STRENGTH = 1.5f;

    Vector2 repulsion(0, 0);
    int cell_size = map->get_cell_size();
    Vector2 center_pos = unit->get_collision_box().get_center_position();

    Vector2 forward = unit->get_component<Movable>()->velocity;
    if (forward.length() < 0.01f) forward = Vector2(1, 0);
    forward = forward.normalize();

    float rad = ANGLE_OFFSET * 3.14159265f / 180.0f;
    Vector2 dirs[3] = {
        forward,
        Vector2(forward.x * cos(rad) - forward.y * sin(rad),
                forward.x * sin(rad) + forward.y * cos(rad)),
        Vector2(forward.x * cos(-rad) - forward.y * sin(-rad),
                forward.x * sin(-rad) + forward.y * cos(-rad))
    };

    for (int i = 0; i < 3; ++i) {
        Vector2 dir = dirs[i];
        float closest_t = RAY_LENGTH * cell_size;
        bool hit = false;

        Vector2 end = center_pos + dir * closest_t;
        float left = std::min(center_pos.x, end.x);
        float right = std::max(center_pos.x, end.x);
        float top = std::min(center_pos.y, end.y);
        float bottom = std::max(center_pos.y, end.y);

        int minx = std::max(0, (int)(left / cell_size) - 1);
        int maxx = std::min(map->get_width() - 1, (int)(right / cell_size) + 1);
        int miny = std::max(0, (int)(top / cell_size) - 1);
        int maxy = std::min(map->get_height() - 1, (int)(bottom / cell_size) + 1);

        for (int cy = miny; cy <= maxy; ++cy) {
            for (int cx = minx; cx <= maxx; ++cx) {
                if (map->is_cell_passable(cx, cy)) continue;
                float cell_left = cx * cell_size;
                float cell_right = cell_left + cell_size;
                float cell_top = cy * cell_size;
                float cell_bottom = cell_top + cell_size;
                float t = ray_intersect_rect(center_pos, dir, cell_left, cell_top, cell_right, cell_bottom);
                if (t > 0 && t < closest_t) {
                    closest_t = t;
                    hit = true;
                }
            }
        }

        if (hit) {
            float distance_grid = closest_t / cell_size;
            float force = REPULSION_STRENGTH / (distance_grid * distance_grid);
            Vector2 point = center_pos + dir * closest_t;
            Vector2 away = (center_pos - point).normalize();
            repulsion = repulsion + away * force;
        }
    }
    return repulsion;
}

// ========== 单位分离力 ==========
Vector2 MoveSystem::compute_separation(const GameObject* unit)
{
    Vector2 separation(0, 0);
    const float SEPARATION_RADIUS = 3.5f;
    const float BASE_REPULSION = 5.0f;

    const CollisionBox& self_box = unit->get_collision_box();
    Vector2 center = self_box.get_center_position();
    int cell_size = map->get_cell_size();

    float self_half_diagonal = sqrt(self_box.width * self_box.width + self_box.height * self_box.height) * 0.5f;
    float perception_radius = self_half_diagonal + SEPARATION_RADIUS * cell_size;

    CollisionBox query_area;
    query_area.position = { center.x - perception_radius, center.y - perception_radius };
    query_area.width = perception_radius * 2.0f;
    query_area.height = perception_radius * 2.0f;

    std::vector<GameObject*> neighbors;
    WorldEntityMgr::instance()->query_area(query_area, neighbors);

    float myPriority = std::max(unit->get_component<Movable>()->velocity.length(), 0.1f);
    for (GameObject* other : neighbors) {
        if (other == unit) continue;

        auto* otherMovable = other->get_component<Movable>();
        if (!otherMovable) continue;
        float otherPriority = std::max(otherMovable->velocity.length(), 0.1f);

        Vector2 otherPos = other->get_collision_box().get_center_position();
        float dx = center.x - otherPos.x;
        float dy = center.y - otherPos.y;
        float dist_px = sqrt(dx * dx + dy * dy);
        float dist_grid = dist_px / cell_size;
        if (dist_grid > SEPARATION_RADIUS) continue;
        if (dist_grid < 0.01f) dist_grid = 0.01f;

        float factor = 1.0f;
        if (myPriority > otherPriority) {
            factor = 0.2f * (otherPriority / myPriority);
        }
        else {
            float ratio = myPriority / otherPriority;
            factor = 1.0f + (1.0f - ratio) * 2.0f;
        }

        float force_mag = BASE_REPULSION / (dist_grid * dist_grid) * factor;
        Vector2 direction = Vector2(dx, dy);
        float len = direction.length();
        if (len < 0.01f) direction = Vector2(1, 0);
        else direction = direction * (1.0f / len);
        separation += direction * force_mag;
    }
    return separation;
}

// 定义一个函数，根据单位位置和目标选择正确的流场和方向
Vector2 MoveSystem::get_flow_direction(const GameObject* unit, const Vector2& target,
    const Vector2& flow_target, float dist_to_target)
{
    const CollisionBox& cb = unit->get_collision_box();
    auto* movable = unit->get_component<Movable>();

    // 1. 距离个人目标较近时，尝试使用局部流场
    if (dist_to_target < LOCAL_FLOW_RADIUS_CELLS * map->get_cell_size()) {
        uint64_t key = vec_to_key(target);   // 个人目标键
        auto it = local_flow_cache.find(key);
        if (it == local_flow_cache.end()) {
            // 生成局部流场（限制扩散半径 = LOCAL_FLOW_RADIUS_CELLS + 2 保证覆盖）
            int radius_cells = (int)(LOCAL_FLOW_RADIUS_CELLS + 2);
            local_flow_cache[key] = map->generate_local_flow_field(target, (float)radius_cells);
            it = local_flow_cache.find(key);
        }
        if (it != local_flow_cache.end()) {
            Vector2 dir = map->sample_flow_from_box(it->second, cb);
            // 如果局部流场在此位置有效（非零），直接使用
            if (dir.length() > 0.01f)
            {
                return dir;
            }
        }
    }

    // 2. 回退到全局流场（远距离，或局部流场未覆盖）
    auto it_global = goal_flow_cache.find(vec_to_key(flow_target));
    if (it_global != goal_flow_cache.end()) {
        Vector2 dir = map->sample_flow_from_box(it_global->second, cb);
        if (dir.length() > 0.01f)
            return dir;
    }

    // 3. 都获取不到，返回零向量
    return { 0.0f, 0.0f };
}

// ========== 步骤1：修正水中目标 ==========
void MoveSystem::correct_unwalkable_targets() {
    auto& all_objects = WorldEntityMgr::instance()->get_object_set();

    std::unordered_map<uint64_t, std::vector<GameObject*>> flow_to_units;
    for (auto* obj : all_objects) {
        auto* movable = obj->get_component<Movable>();
        if (!movable || !movable->is_moving()) continue;
        flow_to_units[vec_to_key(movable->flow_target)].push_back(obj);
    }

    for (auto& [key, units] : flow_to_units) {
        if (units.empty()) continue;
        Vector2 original_flow = units[0]->get_component<Movable>()->flow_target;
        Vector2 corrected = map->find_nearest_passable(original_flow);
        if (corrected.x != original_flow.x || corrected.y != original_flow.y) {
            for (auto* u : units) {
                auto* mv = u->get_component<Movable>();
                // 保持相对偏移
                Vector2 offset = mv->target - original_flow;
                mv->flow_target = corrected;
                mv->target = map->find_nearest_passable(corrected + offset);
            }
        }
    }
}

// ========== 步骤2：更新流场缓存 ==========
void MoveSystem::update_global_flow_cache() {
    auto& all_objects = WorldEntityMgr::instance()->get_object_set();

    std::unordered_set<uint64_t> active_goals;
    for (auto* obj : all_objects) {
        auto* movable = obj->get_component<Movable>();
        if (movable && movable->is_moving()) {
            active_goals.insert(vec_to_key(movable->flow_target));
        }
    }

    // 清理
    for (auto it = goal_flow_cache.begin(); it != goal_flow_cache.end(); ) {
        if (active_goals.find(it->first) == active_goals.end())
            it = goal_flow_cache.erase(it);
        else
            ++it;
    }

    // 生成
    for (auto key : active_goals) {
        if (goal_flow_cache.find(key) == goal_flow_cache.end()) {
            float x = ((float)(static_cast<int>(key >> 32))) / 100.0f;
            float y = ((float)(static_cast<int>(key & 0xFFFFFFFF))) / 100.0f;
            goal_flow_cache[key] = map->generate_goal_flow_field({ x, y });
        }
    }
}

// 更新局部流场缓存
void MoveSystem::update_local_flow_cache() {
    auto& all_objects = WorldEntityMgr::instance()->get_object_set();
    std::unordered_set<uint64_t> active_local_goals;
    for (auto* obj : all_objects) {
        auto* movable = obj->get_component<Movable>();
        if (!movable || !movable->is_moving()) continue;
        float dist = (movable->target - obj->get_collision_box().get_center_position()).length();
        if (dist < LOCAL_FLOW_RADIUS_CELLS * map->get_cell_size()) {
            active_local_goals.insert(vec_to_key(movable->target));
        }
    }
    for (auto it = local_flow_cache.begin(); it != local_flow_cache.end(); ) {
        if (active_local_goals.find(it->first) == active_local_goals.end())
            it = local_flow_cache.erase(it);
        else
            ++it;
    }
}

// ========== 步骤3：移动正在前往目标的单位 ==========
void MoveSystem::move_units(float delta) {
    auto& all_objects = WorldEntityMgr::instance()->get_object_set();
    float map_w = (float)map->get_width() * map->get_cell_size();
    float map_h = (float)map->get_height() * map->get_cell_size();

    for (auto* obj : all_objects) {
        auto* movable = obj->get_component<Movable>();
        if (!movable || !movable->is_moving()) continue;

        const CollisionBox& cb = obj->get_collision_box();
        Vector2 center = cb.get_center_position();
        Vector2 target = movable->target;           // 个人精确停止点
        Vector2 flow_target = movable->flow_target; // 命令中心

        float dist_to_target = (target - center).length();

        // 到达判断
        if (dist_to_target < 5.0f) {
            movable->stop();
            movable->velocity = { 0.0f, 0.0f };
            continue;
        }

        // ---- 获取合适的流场方向（全局或局部） ----
        Vector2 flow_dir = get_flow_direction(obj, target, flow_target, dist_to_target);
        if (flow_dir.length() < 0.01f) {
            // 如果完全没有导航方向，保持原地，避免乱走
            movable->velocity = { 0.0f, 0.0f };
            continue;
        }

        // ---- 障斥力 & 分离力 ----
        Vector2 wall_rep = compute_wall_repulsion(obj);
        Vector2 separation_force = compute_separation(obj);

        // ---- 混合（流场为主，其他力作修正） ----
        Vector2 combined = flow_dir + wall_rep + separation_force;
        if (combined.length() < 0.01f) continue;
        combined = combined.normalize();

        movable->velocity = combined * movable->speed;

        Vector2 new_pos = cb.position + movable->velocity * delta;

        // 边界钳位
        if (new_pos.x < 0.0f) new_pos.x = 0.0f;
        if (new_pos.y < 0.0f) new_pos.y = 0.0f;
        if (new_pos.x > map_w - cb.width)  new_pos.x = map_w - cb.width;
        if (new_pos.y > map_h - cb.height) new_pos.y = map_h - cb.height;

        CollisionBox updated = cb;
        updated.position = new_pos;
        obj->set_collision_box(updated);
    }
}

// ========== 步骤4：静止单位被推动 ==========
void MoveSystem::push_idle_units()
{
    auto& all_objects = WorldEntityMgr::instance()->get_object_set();
    float map_w = (float)map->get_width() * map->get_cell_size();
    float map_h = (float)map->get_height() * map->get_cell_size();

    for (auto* obj : all_objects) {
        auto* movable = obj->get_component<Movable>();
        if (!movable) continue;
        if (movable->is_moving()) continue;           // 只处理已停止的单位

        const CollisionBox& cb = obj->get_collision_box();
        Vector2 center = cb.get_center_position();

        Vector2 sep = compute_separation(obj);
        Vector2 wall = compute_wall_repulsion(obj);

        Vector2 total_push = sep + wall;
        float push_strength = total_push.length();
        if (push_strength < 0.01f) continue;

        // 限制最大移动量，防止被推飞
        const float max_move = 20.0f;
        if (push_strength > max_move)
            total_push = total_push.normalize() * max_move;

        Vector2 new_pos = cb.position + total_push;

        if (new_pos.x < 0.0f) new_pos.x = 0.0f;
        if (new_pos.y < 0.0f) new_pos.y = 0.0f;
        if (new_pos.x > map_w - cb.width)  new_pos.x = map_w - cb.width;
        if (new_pos.y > map_h - cb.height) new_pos.y = map_h - cb.height;

        if (new_pos != cb.position) {
            CollisionBox updated = cb;
            updated.position = new_pos;
            obj->set_collision_box(updated);
        }
    }
}

// ========== 主更新接口 ==========
void MoveSystem::on_update(float delta) {
    if (!map) return;

    correct_unwalkable_targets();
    update_global_flow_cache();   // 全局流场缓存维护
    update_local_flow_cache();    // 局部流场缓存维护
    move_units(delta);
    push_idle_units();
}

//static Vector2 sample_goal_flow(const std::vector<std::vector<Vector2>>& flow,
//    const GameMap* map, const Vector2& pos) {
//    if (flow.empty() || flow[0].empty()) return { 0.0f, 0.0f };
//    return sample_flow_bilinear(flow, pos.x, pos.y, map->cell_size);
//}
//
//// 将目标点重新映射到附近可通行位置（针对目标为水时）
//static Vector2 find_nearest_passable(const GameMap* map, const Vector2& goal) {
//    int gx = (int)(goal.x / map->cell_size);
//    int gy = (int)(goal.y / map->cell_size);
//    gx = std::max(0, std::min(gx, map->width - 1));
//    gy = std::max(0, std::min(gy, map->height - 1));
//
//    if (map->grid[gy][gx] != TerrainType::Water) return goal; // 目标即可通行
//
//    // BFS 查找最近可通行格子
//    std::vector<std::vector<bool>> visited(map->height, std::vector<bool>(map->width, false));
//    std::queue<std::pair<int, int>> q;
//    q.push({ gx, gy });
//    visited[gy][gx] = true;
//    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
//    while (!q.empty()) {
//        auto [x, y] = q.front(); q.pop();
//        if (map->grid[y][x] != TerrainType::Water) {
//            return { x * map->cell_size + map->cell_size * 0.5f,
//                     y * map->cell_size + map->cell_size * 0.5f };
//        }
//        for (auto d : dirs) {
//            int nx = x + d[0], ny = y + d[1];
//            if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
//            if (!visited[ny][nx]) {
//                visited[ny][nx] = true;
//                q.push({ nx, ny });
//            }
//        }
//    }
//    return goal; // 极端情况
//}
//
//// ---- 各个 Steering 行为 ----
//
//// 1. Seek：利用目标流场产生朝向目标的力
//static Vector2 steer_seek(const std::vector<std::vector<Vector2>>& goal_flow,
//    const GameMap* map, const Vector2& pos,
//    const Vector2& current_velocity, float max_speed)
//{
//    // 从流场获取期望方向（归一化）
//    Vector2 desired_dir = sample_goal_flow(goal_flow, map, pos);
//    if (desired_dir.length() < 0.01f) return { 0.0f, 0.0f };
//
//    // 期望速度
//    Vector2 desired_velocity = desired_dir * max_speed;
//
//    // 转向力 = 期望速度 - 当前速度
//    Vector2 steering = desired_velocity - current_velocity;
//
//    // 可加上最大力限制，防止转向过猛
//    // 但为简单，可以不限（后续归一化合力时会控制）
//    return steering;
//}
//
//// 2. Separation：与附近单位保持距离
//static Vector2 steer_separation(const GameObject* self, const Vector2& pos,
//    float desired_sep, float strength = 1.5f) {
//    Vector2 force{};
//    float radius = desired_sep * 2.0f;
//    CollisionBox query_box{ {pos.x - radius, pos.y - radius}, radius * 2.0f, radius * 2.0f };
//    std::vector<GameObject*> nearby;
//    WorldEntityMgr::instance()->query_area(query_box, nearby);
//
//    int count = 0;
//    for (auto* other : nearby) {
//        if (other == self) continue;
//        const auto& box = other->get_collision_box();
//        Vector2 other_pos = { box.position.x + box.width * 0.5f,
//                              box.position.y + box.height * 0.5f };
//        Vector2 to_other = other_pos - pos;
//        float dist = to_other.length();
//        if (dist < 0.01f) dist = 0.01f;
//        if (dist < desired_sep) {
//            // 距离越近排斥越强
//            Vector2 repulsion = to_other * (-1.0f / (dist * dist));
//            force = force + repulsion;
//            count++;
//        }
//    }
//    if (count > 0) {
//        force = force * (1.0f / (float)count); // 平均
//        float len = force.length();
//        if (len > strength) // 限制最大排斥力
//            force = force * (strength / len);
//    }
//    return force;
//}
//
//// 3. Obstacle Avoidance（静态水障碍）：利用静态流场产生排斥力
//// 基于射线检测的 Water 避障：从中心向前方/斜前方发射3条射线，检测水边界
//static Vector2 steer_avoid_water(const GameMap* map, const Vector2& pos,
//    const Vector2& forward)
//{
//    if (map->static_flow_field.empty() || map->static_flow_field[0].empty())
//        return { 0.0f, 0.0f };
//
//    const float ray_length = 100.0f;                // 加长射线，提前发现水
//    const float angle_offset = 15.0f * 3.14159f / 180.0f; // 减小角度，更集中
//    const float force_weight = 8.0f;                // 增大斥力
//
//    Vector2 total_force{ 0.0f, 0.0f };
//
//    for (int i = 0; i < 3; ++i)
//    {
//        float angle = (i - 1) * angle_offset;
//        float cos_a = cos(angle), sin_a = sin(angle);
//        Vector2 ray_dir = {
//            forward.x * cos_a - forward.y * sin_a,
//            forward.x * sin_a + forward.y * cos_a
//        };
//
//        float step = map->cell_size * 0.5f;
//        float traveled = 0.0f;
//        bool hit = false;
//        Vector2 hit_point;
//
//        while (traveled < ray_length)
//        {
//            Vector2 sample = pos + ray_dir * traveled;
//            int cx = (int)(sample.x / map->cell_size);
//            int cy = (int)(sample.y / map->cell_size);
//            if (cx < 0 || cx >= map->width || cy < 0 || cy >= map->height)
//                break;
//
//            if (map->grid[cy][cx] == TerrainType::Water)
//            {
//                hit = true;
//                hit_point = sample;
//                break;
//            }
//            traveled += step;
//        }
//
//        if (hit)
//        {
//            int hx = (int)(hit_point.x / map->cell_size);
//            int hy = (int)(hit_point.y / map->cell_size);
//            hx = std::clamp(hx, 0, (int)map->static_flow_field[0].size() - 1);
//            hy = std::clamp(hy, 0, (int)map->static_flow_field.size() - 1);
//            Vector2 normal = map->static_flow_field[hy][hx];
//
//            if (normal.length() < 0.01f)
//            {
//                Vector2 water_center = {
//                    hx * map->cell_size + map->cell_size * 0.5f,
//                    hy * map->cell_size + map->cell_size * 0.5f
//                };
//                normal = hit_point - water_center;
//                float len = normal.length();
//                if (len > 0.01f) normal = normal * (1.0f / len);
//                else normal = { 1.0f, 0.0f };
//            }
//
//            // 力的大小：交点越近斥力越大，用二次方增加灵敏度
//            float t = traveled / ray_length; // 0~1，越小越近
//            float magnitude = force_weight * (1.0f - t) * (1.0f - t);
//            total_force = total_force + normal * magnitude;
//        }
//    }
//    return total_force;
//}
//
//// 4. Containment（地图边界约束）
//static Vector2 steer_containment(const Vector2& pos, float map_w, float map_h,
//    float margin = 20.0f, float strength = 2.0f) {
//    Vector2 force{};
//    if (pos.x < margin) force.x += (margin - pos.x) / margin * strength;
//    else if (pos.x > map_w - margin) force.x += (map_w - margin - pos.x) / margin * strength;
//    if (pos.y < margin) force.y += (margin - pos.y) / margin * strength;
//    else if (pos.y > map_h - margin) force.y += (map_h - margin - pos.y) / margin * strength;
//    return force;
//}
//
//// ---- 主更新 ----
//
//void MoveSystem::on_update(float delta) {
//    GameMap* map = WorldEntityMgr::instance()->get_map();
//    if (!map) return;
//
//    // 1. 收集活跃目标，生成/维护流场缓存（与之前一致）
//    std::unordered_set<uint64_t> active_goals;
//    auto& all_objects = WorldEntityMgr::instance()->get_object_set();
//    for (auto* obj : all_objects) {
//        auto* movable = obj->get_component<Movable>();
//        if (movable && movable->is_moving())
//            active_goals.insert(vec_to_key(movable->target));
//    }
//
//    static std::unordered_map<uint64_t, std::vector<std::vector<Vector2>>> goal_flow_cache;
//    for (auto it = goal_flow_cache.begin(); it != goal_flow_cache.end(); ) {
//        if (active_goals.find(it->first) == active_goals.end())
//            it = goal_flow_cache.erase(it);
//        else
//            ++it;
//    }
//
//    for (auto key : active_goals) {
//        if (goal_flow_cache.find(key) == goal_flow_cache.end()) {
//            float x = ((float)(int)(key >> 32)) / 100.0f;
//            float y = ((float)(int)(key & 0xFFFFFFFF)) / 100.0f;
//            goal_flow_cache[key] = map->generate_goal_flow_field({ x, y });
//        }
//    }
//
//    // 2. 为每个单位组合 Steering 力
//    float map_w = (float)map->width * map->cell_size;
//    float map_h = (float)map->height * map->cell_size;
//
//    for (auto* obj : all_objects) {
//        auto* movable = obj->get_component<Movable>();
//        if (!movable || !movable->is_moving()) continue;
//
//        Vector2 pos = obj->get_collision_box().position;
//        Vector2 target = movable->target;
//
//        auto key = vec_to_key(target);
//        auto it = goal_flow_cache.find(key);
//        if (it == goal_flow_cache.end()) continue;
//        const auto& goal_flow = it->second;
//
//        // 检查是否到达目标
//        if ((target - pos).length() < 5.0f) {
//            movable->stop();
//            movable->velocity = { 0.0f, 0.0f };  // 停止时清零速度
//            continue;
//        }
//
//        // 计算各 Steering 力
//        Vector2 seek_force = steer_seek(goal_flow, map, pos, movable->velocity, movable->speed);
//        Vector2 sep_force = steer_separation(obj, pos, obj->get_collision_box().width * 1.8f);
//        Vector2 contain_force = steer_containment(pos, map_w, map_h);
//
//        Vector2 forward;
//        if (movable->velocity.length() > 0.01f)
//        {
//            forward = movable->velocity.normalize();
//        }
//        else
//        {
//            // 静止时用目标流场的方向作为默认朝向
//            Vector2 desired_dir = sample_goal_flow(goal_flow, map, pos);
//            if (desired_dir.length() > 0.01f)
//                forward = desired_dir;
//            else
//                forward = { 1.0f, 0.0f };  // 兜底：向右
//        }
//
//        // 然后调用新版避障函数
//        Vector2 water_force = steer_avoid_water(map, pos, forward);
//
//        Vector2 steering = seek_force*0.8f + sep_force + water_force + contain_force;
//
//        // 更新速度
//        movable->velocity = movable->velocity + steering * delta;
//
//        // 速度阻尼（摩擦），使移动更沉稳
//        float damping_factor = 0.99f; // 每帧保留的速度比例，越小减速越快
//        movable->velocity = movable->velocity * damping_factor;
//
//        // 限制最大速率
//        float spd = movable->velocity.length();
//        if (spd > movable->speed && spd > 0.01f)
//            movable->velocity = movable->velocity * (movable->speed / spd);
//
//        // 用速度更新位置
//        Vector2 new_pos = pos + movable->velocity * delta;   // 改为 velocity * delta
//
//        // 硬边界钳位（安全网）
//        if (new_pos.x < 0.0f) new_pos.x = 0.0f;
//        if (new_pos.y < 0.0f) new_pos.y = 0.0f;
//        if (new_pos.x > map_w - obj->get_collision_box().width) new_pos.x = map_w - obj->get_collision_box().width;
//        if (new_pos.y > map_h - obj->get_collision_box().height) new_pos.y = map_h - obj->get_collision_box().height;
//
//        CollisionBox cb = obj->get_collision_box();
//        cb.position = new_pos;
//        obj->set_collision_box(cb);
//    }
//}