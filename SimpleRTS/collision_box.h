#ifndef _COLLISION_BOX_H_
#define _COLLISION_BOX_H_

#include "vector2.h"

class CollisionBox
{
public:
    CollisionBox() = default;

    CollisionBox(Vector2 pos, float w, float h)
        : position(pos), width(w), height(h) {
    }

    // 获取碰撞箱中心点
    Vector2 get_center_position() const
    {
        return position + Vector2{width * 0.5f, height * 0.5f};
    }

    // 点是否在矩形内
    bool contains_point(const Vector2& point_position) const
    {
        return contains_point(point_position.x, point_position.y);
    }

    bool contains_point(float x, float y) const
    {
        return x >= position.x &&
            x <= position.x + width &&
            y >= position.y &&
            y <= position.y + height;
    }

    // 是否包含另一个碰撞箱的中心点（RTS 框选神器）
    bool contains_center_of(const CollisionBox& other) const
    {
        float cx = other.position.x + other.width * 0.5f;
        float cy = other.position.y + other.height * 0.5f;
        return contains_point(cx, cy);
    }

    // 矩形相交
    bool intersects(const CollisionBox& other) const
    {
        return !(position.x + width < other.position.x ||
            position.x > other.position.x + other.width ||
            position.y + height < other.position.y ||
            position.y > other.position.y + other.height);
    }

    // 严格相交（边缘接触不算重叠）
    bool overlaps_strict(const CollisionBox& other) const {
        return position.x < other.position.x + other.width &&
            position.x + width > other.position.x &&
            position.y < other.position.y + other.height &&
            position.y + height > other.position.y;
    }

public:
    Vector2 position;
    float width = 0.0f;
    float height = 0.0f;
};

#endif