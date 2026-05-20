#include "util.h"
#include "world_entity_mgr.h"
#include "components.h"
#include <algorithm>
#include <queue>

// 检查格子 (x,y) 是否可通行，若该格子被 ignore 实体占据，则视为可通行
static bool is_cell_passable_ignore(const GameMap* map, int x, int y, const GameObject* ignore) {
    if (!map->is_cell_passable(x, y)) {
        // 如果不可通行，检查是否仅因为 ignore 实体占据导致
        if (!ignore) return false;
        const CollisionBox& box = ignore->get_collision_box();
        int minx = (int)(box.position.x / map->get_cell_size());
        int miny = (int)(box.position.y / map->get_cell_size());
        int maxx = (int)((box.position.x + box.width) / map->get_cell_size());
        int maxy = (int)((box.position.y + box.height) / map->get_cell_size());
        if (x >= minx && x <= maxx && y >= miny && y <= maxy)
            return true; // 是 ignore 实体占据，视为可通过
        return false;
    }
    return true;
}

// 直线路径是否完全可通行（可选择忽略某个实体）
bool is_line_passable(const Vector2& start, const Vector2& end, const GameObject* ignore) {
    GameMap* map = WorldEntityMgr::instance()->get_map();
    float cell_size = (float)map->get_cell_size();
    float dist = (end - start).length();
    if (dist < 0.01f) return true;

    Vector2 dir = (end - start).normalize();
    float step = cell_size * 0.5f;
    float traveled = 0.0f;

    while (traveled < dist) {
        Vector2 pt = start + dir * traveled;
        int cx = (int)(pt.x / cell_size);
        int cy = (int)(pt.y / cell_size);
        if (!is_cell_passable_ignore(map, cx, cy, ignore))
            return false;
        traveled += step;
    }
    // 检查终点
    int cx = (int)(end.x / cell_size);
    int cy = (int)(end.y / cell_size);
    return is_cell_passable_ignore(map, cx, cy, ignore);
}

// BFS 计算实际路径距离（像素），限制最大步数 max_range_cells（格子数）
float bfs_path_distance(const Vector2& start, const Vector2& end, const GameMap* map,
    float max_range_cells, const GameObject* ignore = nullptr) {
    int cell_size = map->get_cell_size();
    int sx = std::clamp((int)(start.x / cell_size), 0, map->get_width() - 1);
    int sy = std::clamp((int)(start.y / cell_size), 0, map->get_height() - 1);
    int ex = std::clamp((int)(end.x / cell_size), 0, map->get_width() - 1);
    int ey = std::clamp((int)(end.y / cell_size), 0, map->get_height() - 1);

    if (sx == ex && sy == ey) return 0.0f;

    std::vector<std::vector<bool>> visited(map->get_height(), std::vector<bool>(map->get_width(), false));
    std::queue<std::tuple<int, int, int>> q; // x, y, steps
    q.push({ sx, sy, 0 });
    visited[sy][sx] = true;

    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };

    while (!q.empty()) {
        auto [x, y, step] = q.front(); q.pop();
        if (x == ex && y == ey)
            return step * cell_size;  // 简单格子距离，精确可用对角线长度，这里暂用曼哈顿等效

        if (step >= (int)max_range_cells) continue;

        for (auto d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= map->get_width() || ny < 0 || ny >= map->get_height()) continue;
            if (!is_cell_passable_ignore(map, nx, ny, ignore)) continue;
            if (!visited[ny][nx]) {
                visited[ny][nx] = true;
                q.push({ nx, ny, step + 1 });
            }
        }
    }
    return -1.0f; // 不可达
}

// 查找最近的可攻击敌人（基于 team_id，直线优先，被阻则 BFS）
GameObject* find_nearest_enemy(const Vector2& center, int attacker_team_id, float radius_cells)
{
    GameMap* map = WorldEntityMgr::instance()->get_map();
    if (!map) return nullptr;

    float cell_size = (float)map->get_cell_size();
    float radius = radius_cells * cell_size;

    CollisionBox search_box{
        { center.x - radius, center.y - radius },
        radius * 2.0f, radius * 2.0f
    };
    std::vector<GameObject*> candidates;
    WorldEntityMgr::instance()->query_area(search_box, candidates);

    GameObject* best = nullptr;
    float best_dist = 1e9f;

    for (auto* obj : candidates)
    {
        if (!obj->check_valid()) continue;

        // 必须有血量且 >0
        auto* health = obj->get_component<Health>();
        if (!health || health->current_health <= 0) continue;

        // 敌我判定
        auto* ownership = obj->get_component<Ownership>();
        if (ownership)
        {
            // 同 team_id 为盟友，不能攻击
            if (ownership->team_id == attacker_team_id) continue;
        }
        // 没有 Ownership 的实体视为中立，可攻击（不跳过）

        Vector2 target_center = obj->get_collision_box().get_center_position();
        float direct_dist = (target_center - center).length();

        // 1. 直线可见
        if (is_line_passable(center, target_center))
        {
            if (direct_dist < best_dist)
            {
                best_dist = direct_dist;
                best = obj;
            }
            continue;
        }

        // 2. 直线被阻，尝试 BFS 绕路（限制 1.5 倍半径）
        float actual_dist = bfs_path_distance(center, target_center, map, radius_cells * 1.5f);
        if (actual_dist >= 0.0f && actual_dist < best_dist)
        {
            best_dist = actual_dist;
            best = obj;
        }
    }
    return best;
}

// 查找最近的可采集资源（直线优先，若被阻则 BFS 计算绕路距离）
GameObject* find_nearest_resource_of_type(ResourceType type, const Vector2& center, float radius) {
    GameMap* map = WorldEntityMgr::instance()->get_map();
    float cell_size = (float)map->get_cell_size();
    float search_radius = radius * cell_size;

    CollisionBox search_box{ {center.x - search_radius, center.y - search_radius},
                             search_radius * 2.0f, search_radius * 2.0f };
    std::vector<GameObject*> candidates;
    WorldEntityMgr::instance()->query_area(search_box, candidates);

    GameObject* best = nullptr;
    float best_dist = 1e9f;
    for (auto* obj : candidates) {
        if (!obj->check_valid()) continue;
        auto* harv = obj->get_component<Harvestable>();
        if (!harv || harv->output_type != type) continue;
        auto* h = obj->get_component<Health>();
        if (!h || h->current_health <= 0) continue;

        Vector2 res_center = obj->get_collision_box().get_center_position();
        float direct_dist = (res_center - center).length();

        // 1. 直线可见
        if (is_line_passable(center, res_center, obj)) {
            if (direct_dist < best_dist) {
                best_dist = direct_dist;
                best = obj;
            }
            continue;
        }

        // 2. 直线被阻，尝试 BFS 绕路（不超过 1.5 倍 radius 格子）
        float actual_dist = bfs_path_distance(center, res_center, map, radius * 1.5f, obj);
        if (actual_dist >= 0.0f && actual_dist < best_dist) {
            best_dist = actual_dist;
            best = obj;
        }
    }
    return best;
}

// 查找最近的可提交建筑（直线优先，被阻则 BFS）
GameObject* find_nearest_dropoff(const Vector2& center, int player_id, ResourceType carried_type, float radius_cells) {
    GameMap* map = WorldEntityMgr::instance()->get_map();
    if (!map) return nullptr;

    float cell_size = (float)map->get_cell_size();
    float radius = radius_cells * cell_size;

    CollisionBox search_box{ {center.x - radius, center.y - radius}, radius * 2.0f, radius * 2.0f };
    std::vector<GameObject*> candidates;
    WorldEntityMgr::instance()->query_area(search_box, candidates);

    GameObject* best = nullptr;
    float best_dist = 1e9f;

    int type_index = static_cast<int>(carried_type);
    if (type_index <= 0) return nullptr;
    uint8_t mask = 1 << (type_index - 1);

    for (auto* obj : candidates) {
        if (!obj->check_valid()) continue;
        auto* dropoff = obj->get_component<ResourceDropoff>();
        if (!dropoff || !(dropoff->accept_mask & mask)) continue;
        auto* ownership = obj->get_component<Ownership>();
        if (!ownership || ownership->player_id != player_id) continue;

        Vector2 building_center = obj->get_collision_box().get_center_position();
        float direct_dist = (building_center - center).length();

        // 1. 直线可见
        if (is_line_passable(center, building_center, obj)) {
            if (direct_dist < best_dist) {
                best_dist = direct_dist;
                best = obj;
            }
            continue;
        }

        // 2. BFS 绕路（半径 1.5 倍）
        float actual_dist = bfs_path_distance(center, building_center, map, radius_cells * 1.5f, obj);
        if (actual_dist >= 0.0f && actual_dist < best_dist) {
            best_dist = actual_dist;
            best = obj;
        }
    }
    return best;
}

// ... 射线检测实现 ...
bool ray_intersects_box(const Vector2& origin, const Vector2& dir, float length, const CollisionBox& box)
{
    Vector2 end = origin + dir * length;
    // 快速 AABB 排除
    float min_x = std::min(origin.x, end.x);
    float max_x = std::max(origin.x, end.x);
    float min_y = std::min(origin.y, end.y);
    float max_y = std::max(origin.y, end.y);
    if (max_x < box.position.x || min_x > box.position.x + box.width ||
        max_y < box.position.y || min_y > box.position.y + box.height) {
        return false;
    }

    // 参数化线段与矩形边的交点
    float t_min = 0.0f, t_max = length;
    if (std::abs(dir.x) > 0.0001f) {
        float t1 = (box.position.x - origin.x) / dir.x;
        float t2 = (box.position.x + box.width - origin.x) / dir.x;
        if (t1 > t2) std::swap(t1, t2);
        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        if (t_min > t_max) return false;
    }
    else if (origin.x < box.position.x || origin.x > box.position.x + box.width) {
        return false;
    }
    if (std::abs(dir.y) > 0.0001f) {
        float t1 = (box.position.y - origin.y) / dir.y;
        float t2 = (box.position.y + box.height - origin.y) / dir.y;
        if (t1 > t2) std::swap(t1, t2);
        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        if (t_min > t_max) return false;
    }
    else if (origin.y < box.position.y || origin.y > box.position.y + box.height) {
        return false;
    }
    return true;
}

Vector2 compute_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float extra_margin)
{
    // 从目标中心指向单位中心的方向
    Vector2 dir = unit_center - target_center;
    if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
    dir = dir.normalize();

    // 距离 = 目标半边长 + 单位半边长 + 额外间距
    float dist = unit_box.width * 0.5f + target_box.width * 0.5f + extra_margin;

    return target_center + dir * dist;
}

// 计算远程单位应停在射程边缘的位置
Vector2 compute_ranged_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    float range,
    float extra_margin)
{
    Vector2 dir = unit_center - target_center;
    if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
    dir = dir.normalize();
    float dist = range * 0.85f;  // 停在射程的 85% 处，留一点余量
    if (dist < 0.01f) dist = 1.0f;
    return target_center + dir * dist;
}




