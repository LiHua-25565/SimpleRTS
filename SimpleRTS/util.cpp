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

// 计算一个点 (px, py) 到矩形 (rect_x, rect_y, rect_w, rect_h) 的最短距离
float rect_closest_distance(const Vector2& point, const CollisionBox& rect)
{
    float dx = 0.0f;
    float dy = 0.0f;

    if (point.x < rect.position.x)
        dx = rect.position.x - point.x;
    else if (point.x > rect.position.x + rect.width)
        dx = point.x - (rect.position.x + rect.width);

    if (point.y < rect.position.y)
        dy = rect.position.y - point.y;
    else if (point.y > rect.position.y + rect.height)
        dy = point.y - (rect.position.y + rect.height);

    return std::sqrt(dx * dx + dy * dy);
}

// 将一个点钳制到矩形的最近边缘外（以膨胀后的矩形为基准）
static Vector2 clamp_to_rect_edge(const Vector2& point, const CollisionBox& inflated)
{
    // 先钳制到矩形内部
    Vector2 closest;
    closest.x = std::clamp(point.x, inflated.position.x, inflated.position.x + inflated.width);
    closest.y = std::clamp(point.y, inflated.position.y, inflated.position.y + inflated.height);

    // 如果已经在内部，推到最近边
    if (closest.x == point.x && closest.y == point.y)
    {
        float dx_min = point.x - inflated.position.x;
        float dx_max = inflated.position.x + inflated.width - point.x;
        float dy_min = point.y - inflated.position.y;
        float dy_max = inflated.position.y + inflated.height - point.y;
        float min_dist = std::min({ dx_min, dx_max, dy_min, dy_max });

        if (min_dist == dx_min)
            closest.x = inflated.position.x;
        else if (min_dist == dx_max)
            closest.x = inflated.position.x + inflated.width;
        else if (min_dist == dy_min)
            closest.y = inflated.position.y;
        else
            closest.y = inflated.position.y + inflated.height;
    }
    return closest;
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

// 找离 center 最近、任意类型且血量>0 的资源（供目标死亡后的自动换矿使用）
GameObject* find_nearest_any_resource(const Vector2& center, float radius) {
    GameObject* best = nullptr;
    float best_d = 1e9f;
    const ResourceType types[3] = { ResourceType::Food, ResourceType::Gold, ResourceType::Wood };
    for (ResourceType t : types) {
        GameObject* r = find_nearest_resource_of_type(t, center, radius);
        if (!r) continue;
        float d = (r->get_collision_box().get_center_position() - center).length();
        if (d < best_d) { best_d = d; best = r; }
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
    float expand = std::max(unit_box.width, unit_box.height) * 0.5f + extra_margin;
    CollisionBox inflated = target_box;
    inflated.position.x -= expand;
    inflated.position.y -= expand;
    inflated.width += expand * 2.0f;
    inflated.height += expand * 2.0f;

    return clamp_to_rect_edge(unit_center, inflated);
}

// 计算远程单位应停在射程边缘的位置
Vector2 compute_ranged_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float range,
    float extra_margin)
{
    // 计算单位中心到目标矩形最近点的方向
    float closest_x = std::clamp(unit_center.x, target_box.position.x, target_box.position.x + target_box.width);
    float closest_y = std::clamp(unit_center.y, target_box.position.y, target_box.position.y + target_box.height);
    Vector2 closest_point = { closest_x, closest_y };

    // 从目标最近点指向单位中心的方向
    Vector2 dir = unit_center - closest_point;
    float dist_to_closest = dir.length();

    // 如果单位已经在最近点附近，使用单位到目标中心的方向（避免零向量）
    if (dist_to_closest < 0.01f) {
        dir = unit_center - target_center;
        if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
    }
    dir = dir.normalize();

    // 目标位置 = 最近点 + 方向 * (射程 - 额外边距)
    float dist = range * 0.85f;   // 停在射程的 85% 处，留有余量
    if (dist < 0.01f) dist = 1.0f;

    // 注意：我们希望单位停在距离目标边缘 range 的位置，而不是距离中心 range
    // 所以直接以 closest_point 为基准，向远离目标的方向移动 range 距离
    return closest_point + dir * (range - extra_margin);
}

// ========== 周界站位点分配（多单位作业分散） ==========
//
// 背景：多个农民采集/提交时，原来每个人都用 compute_outer_target ——
// 各自取“离自己最近的边点”，结果一群农民全挤在资源/建筑的同一侧同一点，
// RVO 让先到的人占住位置，后来者在十几像素外被挡到速度归零，
// 又永远进不了采集判定距离，于是停工（经典拥堵）。
//
// 做法：把目标盒四周的周界（膨胀 half_unit + margin，约贴盒站立）均匀
// 采样成一圈站位点，给每个单位分配一个互不重复的点；单个单位换位时选
// “附近其它单位最少”的点。这样 n 个农民会自动围着资源/建筑散开。

// 生成周界站位点：沿膨胀矩形四边采样，间距 spacing；
// 每边两端各留 spacing*0.5，避开“站在角上”时斜向接近采集判定距离不够的边缘情形
static std::vector<Vector2> make_perimeter_slots(const CollisionBox& target_box,
    float expand, float spacing, const GameMap* map)
{
    std::vector<Vector2> slots;
    const float l = target_box.position.x - expand;
    const float r = target_box.position.x + target_box.width + expand;
    const float t = target_box.position.y - expand;
    const float b = target_box.position.y + target_box.height + expand;

    auto push = [&](float x, float y) {
        if (!map || map->is_cell_passable((int)(x / map->get_cell_size()),
                                          (int)(y / map->get_cell_size())))
            slots.push_back({ x, y });
    };

    for (float x = l + spacing * 0.5f; x <= r - spacing * 0.5f + 0.01f; x += spacing) push(x, t);
    for (float x = l + spacing * 0.5f; x <= r - spacing * 0.5f + 0.01f; x += spacing) push(x, b);
    for (float y = t + spacing * 0.5f; y <= b - spacing * 0.5f + 0.01f; y += spacing) push(r, y);
    for (float y = t + spacing * 0.5f; y <= b - spacing * 0.5f + 0.01f; y += spacing) push(l, y);

    if (slots.empty() && map) {
        // 全图都被障碍包围的退化情形：退回最近可通行点
        Vector2 c = target_box.get_center_position();
        slots.push_back(map->find_nearest_passable(c));
    }
    return slots;
}

std::unordered_map<GameObject*, Vector2> compute_perimeter_targets(
    const std::vector<GameObject*>& units,
    const CollisionBox& target_box,
    float extra_margin)
{
    std::unordered_map<GameObject*, Vector2> targets;
    if (units.empty()) return targets;

    // 站位间距必须大于软推挤半径之和（32px 单位 ≈ 41px），
    // 否则相邻站位者会整局互相推搡
    float unit_size = 0.0f;
    for (GameObject* u : units)
        unit_size = std::max(unit_size, std::max(u->get_collision_box().width,
                                                 u->get_collision_box().height));
    const float expand = unit_size * 0.5f + extra_margin;
    const float spacing = unit_size * 1.35f;

    std::vector<Vector2> slots = make_perimeter_slots(target_box, expand, spacing,
        WorldEntityMgr::instance()->get_map());

    std::vector<bool> taken(slots.size(), false);

    // 先给离目标最近的单位分配（就近原则），每个槽位只给一个人；
    // 人数多于槽位时允许复用最近槽位，由拥堵自愈逻辑继续分散
    std::vector<GameObject*> order = units;
    std::sort(order.begin(), order.end(), [&](GameObject* a, GameObject* b) {
        float da = (target_box.get_center_position() - a->get_collision_box().get_center_position()).length();
        float db = (target_box.get_center_position() - b->get_collision_box().get_center_position()).length();
        return da < db;
        });

    for (GameObject* u : order) {
        const Vector2 uc = u->get_collision_box().get_center_position();
        int best = -1; float best_d = 1e9f;
        // 第一轮找未占用的最近槽位
        for (int i = 0; i < (int)slots.size(); ++i) {
            if (taken[i]) continue;
            float d = (slots[i] - uc).length();
            if (d < best_d) { best_d = d; best = i; }
        }
        if (best < 0) {
            // 槽位全被占用：就近复用
            for (int i = 0; i < (int)slots.size(); ++i) {
                float d = (slots[i] - uc).length();
                if (d < best_d) { best_d = d; best = i; }
            }
        }
        if (best >= 0) {
            taken[best] = true;
            targets[u] = slots[best];
        }
    }
    return targets;
}

Vector2 compute_perimeter_target(uint64_t requester_id,
    const Vector2& unit_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float extra_margin,
    const Vector2& current_target)
{
    const float unit_size = std::max(unit_box.width, unit_box.height);
    const float expand = unit_size * 0.5f + extra_margin;
    const float spacing = unit_size * 1.35f;

    std::vector<Vector2> slots = make_perimeter_slots(target_box, expand, spacing,
        WorldEntityMgr::instance()->get_map());
    if (slots.empty()) return compute_outer_target(unit_center,
        target_box.get_center_position(), unit_box, target_box, extra_margin);

    // 评分用“最大最小距离”：每个槽位测量它到最近其它单位的距离，
    // 选该距离最大的槽位 —— 最宽敞、最可能真正走得到的点。
    // 单纯的“附近单位计数”分辨不出“槽位被占死”和“旁边路过”的区别，
    // 会反复把单位分到被占的槽位上造成兜圈子。
    // 另外把“别的行进单位正在赶往的槽位”视为已被预订，避免两个
    // 农民同时选中同一个空位、到了以后互相堵（双订）
    std::vector<float> clearance(slots.size(), spacing * 1.5f);
    std::vector<bool> claimed(slots.size(), false);
    for (int i = 0; i < (int)slots.size(); ++i) {
        CollisionBox area{
            { slots[i].x - spacing * 1.5f, slots[i].y - spacing * 1.5f },
            spacing * 3.0f, spacing * 3.0f };
        std::vector<GameObject*> nearby;
        WorldEntityMgr::instance()->query_area(area, nearby);
        for (GameObject* o : nearby) {
            if (!o || !o->check_valid() || o->get_id() == requester_id) continue;
            if (o->get_component<Projectile>()) continue;
            auto* omv = o->get_component<Movable>();
            if (!omv) continue;
            float d = (slots[i] - o->get_collision_box().get_center_position()).length();
            if (d < clearance[i]) clearance[i] = d;
            if (omv->is_moving() && (omv->target - slots[i]).length() < 6.0f)
                claimed[i] = true;
        }
    }

    int best = -1;
    float best_score = -1e9f;
    for (int i = 0; i < (int)slots.size(); ++i) {
        float score = clearance[i] - (slots[i] - unit_center).length() * 0.1f;
        // 已被别人预订的槽位重罚，自己正堵的点也要降权（避免原地重复选中）
        if (claimed[i]) score -= 2000.0f;
        if (current_target.x >= 0.0f && (slots[i] - current_target).length() < 6.0f)
            score -= 1000.0f;
        if (score > best_score) { best_score = score; best = i; }
    }
    return slots[best];
}


// 统计某玩家当前"正在采集"各资源类型的农民数量（资源面板角标用）
void count_gatherers_by_resource(int player_id, int* out_counts)
{
    if (!out_counts) return;

    const int type_count = static_cast<int>(ResourceType::Count);
    for (int i = 0; i < type_count; ++i) out_counts[i] = 0;

    auto* world = WorldEntityMgr::instance();
    const auto& pool = world->get_object_pool();
    for (const auto& [id, obj] : pool)
    {
        if (!obj || !obj->check_valid()) continue;

        auto* gatherer = obj->get_component<Gatherer>();
        if (!gatherer) continue;

        // 只统计指定玩家的农民
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;

        ResourceType type = ResourceType::None;
        if (gatherer->target_resource_id != 0)
        {
            GameObject* target = world->get_object_by_id(gatherer->target_resource_id);
            if (target && target->check_valid())
            {
                auto* harvestable = target->get_component<Harvestable>();
                if (harvestable) type = harvestable->output_type;
            }
        }
        // 目标是空的（正在送货回城）但手里有货：仍算作该资源的采集者
        if (type == ResourceType::None && gatherer->carried_amount > 0)
            type = gatherer->carried_type;

        const int idx = static_cast<int>(type);
        if (idx > 0 && idx < type_count) out_counts[idx]++;
    }
}
