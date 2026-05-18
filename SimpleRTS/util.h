#ifndef _UTIL_H_
#define _UTIL_H_

#include "game_map.h"      // 需要 GameMap 指针
#include "game_object.h"
#include "resources_type.h"

// 射线检测
bool ray_intersects_box(const Vector2& origin, const Vector2& dir, float length, const CollisionBox& box);

// 在单位周围搜索最近的同类型且血量>0的资源实体
GameObject* find_nearest_resource_of_type(ResourceType type, const Vector2& center, float radius);

// 在单位周围搜索最近的可提交建筑（己方、接受指定资源类型）
GameObject* find_nearest_dropoff(const Vector2& center, int player_id, ResourceType carried_type, float radius_cells);

#endif // !_UTIL_H_
