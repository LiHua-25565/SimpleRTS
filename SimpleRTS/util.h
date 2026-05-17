#ifndef _UTIL_H_
#define _UTIL_H_

#include "vector2.h"
#include "collision_box.h"
#include <algorithm>

static bool ray_intersects_box(const Vector2& origin, const Vector2& dir, float length, const CollisionBox& box)
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

#endif // !_UTIL_H_
