#ifndef _PROJECTILE_TYPE_H_
#define _PROJECTILE_TYPE_H_

#include <cstdint>

enum class ProjectileType : uint8_t {
    Arrow,      // 箭矢
    // 后续可扩展：Bullet, Cannonball 等
    Count
};

#endif // !_PROJECTILE_TYPE_H_
