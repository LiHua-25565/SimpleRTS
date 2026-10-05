#ifndef _UTIL_H_
#define _UTIL_H_

#include "game_map.h"      // 需要 GameMap 指针
#include "game_object.h"
#include "resources_type.h"

// 射线检测
bool ray_intersects_box(const Vector2& origin, const Vector2& dir, float length, const CollisionBox& box);

// 直线路径是否完全可通行（可选择忽略某个实体）
bool is_line_passable(const Vector2& start, const Vector2& end, const GameObject* ignore = nullptr);

// 计算一个点 (px, py) 到矩形 (rect_x, rect_y, rect_w, rect_h) 的最短距离
float rect_closest_distance(const Vector2& point, const CollisionBox& rect);

// BFS 计算实际路径距离（像素），限制最大步数 max_range_cells（格子数）
float bfs_path_distance(const Vector2& start, const Vector2& end, const GameMap* map,
    float max_range_cells, const GameObject* ignore);

// 取目标实体外一点作为移动目标
Vector2 compute_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float extra_margin = 5.0f);

// ===== 周界站位点分配（多单位作业分散，解决采集/提交拥堵） =====
// 在 target_box 周围（膨胀 half_unit + extra_margin）的周界上按单位尺寸
// 采样站位点（避开四角），并为 **一组单位** 各分配一个互不重复的点
// （就近原则）。站位点数不足时允许复用。用于多个农民同时右键采集/提交，
// 避免所有人挤在目标盒的同一侧同一点上互相堵死
std::unordered_map<GameObject*, Vector2> compute_perimeter_targets(
    const std::vector<GameObject*>& units,
    const CollisionBox& target_box,
    float extra_margin = 2.0f);

// 为 **单个单位** 选择周界站位点：在所有候选点中挑"附近其它单位最少"
// （最不拥挤）的一个；current_target 附近的点会被降权，避免堵在原地时
// 反复选中同一个点。用于农民被挤离/堵住后的换位，以及送货、返程分配
Vector2 compute_perimeter_target(uint64_t requester_id,
    const Vector2& unit_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float extra_margin = 2.0f,
    const Vector2& current_target = { -1.0f, -1.0f });

// 计算远程单位应停在射程边缘的位置（基于目标矩形最近点）
Vector2 compute_ranged_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float range,
    float extra_margin = 5.0f);

// 查找最近的可攻击敌人（基于 team_id，直线优先，被阻则 BFS）
GameObject* find_nearest_enemy(const Vector2& center, int attacker_team_id, float radius_cells);

// 在单位周围搜索最近的同类型且血量>0的资源实体
GameObject* find_nearest_resource_of_type(ResourceType type, const Vector2& center, float radius);

// 在单位周围搜索最近的可提交建筑（己方、接受指定资源类型）
GameObject* find_nearest_dropoff(const Vector2& center, int player_id, ResourceType carried_type, float radius_cells);

#endif // !_UTIL_H_
