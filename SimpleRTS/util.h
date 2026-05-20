#ifndef _UTIL_H_
#define _UTIL_H_

#include "game_map.h"      // 需要 GameMap 指针
#include "game_object.h"
#include "resources_type.h"

// 射线检测
bool ray_intersects_box(const Vector2& origin, const Vector2& dir, float length, const CollisionBox& box);

// 直线路径是否完全可通行（可选择忽略某个实体）
bool is_line_passable(const Vector2& start, const Vector2& end, const GameObject* ignore = nullptr);

// BFS 计算实际路径距离（像素），限制最大步数 max_range_cells（格子数）
float bfs_path_distance(const Vector2& start, const Vector2& end, const GameMap* map,
    float max_range_cells, const GameObject* ignore);

// 取目标实体外一点作为移动目标
Vector2 compute_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    const CollisionBox& target_box,
    float extra_margin = 5.0f);

// 计算远程单位应停在射程边缘的位置
Vector2 compute_ranged_outer_target(const Vector2& unit_center,
    const Vector2& target_center,
    const CollisionBox& unit_box,
    float range,
    float extra_margin = 5.0f);

// 查找最近的可攻击敌人（基于 team_id，直线优先，被阻则 BFS）
GameObject* find_nearest_enemy(const Vector2& center, int attacker_team_id, float radius_cells);

// 在单位周围搜索最近的同类型且血量>0的资源实体
GameObject* find_nearest_resource_of_type(ResourceType type, const Vector2& center, float radius);

// 在单位周围搜索最近的可提交建筑（己方、接受指定资源类型）
GameObject* find_nearest_dropoff(const Vector2& center, int player_id, ResourceType carried_type, float radius_cells);

#endif // !_UTIL_H_
