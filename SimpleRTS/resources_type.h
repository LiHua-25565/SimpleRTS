#ifndef _RESOURCES_TYPE_H_
#define _RESOURCES_TYPE_H_
#include <cstdint>

enum class ResourceType : uint8_t {
    None,
    Wood,
    Food,
    Gold,
    Stone,    // 可扩展
    Count     // 自动计数，方便定义数组大小
};

enum class ResourceEntityType : uint8_t {
    Wood,        // 树木，2×2格
    SGold,       // 小金矿，10×10格
    LGold,       // 大金矿，15×15格
    Stone,       // 石矿，10×10格
    Berries,     // 浆果丛，4×4格
};

#endif // !_RESOURCES_TYPE_H_

