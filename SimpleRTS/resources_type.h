#ifndef _RESOURCES_TYPE_H_
#define _RESOURCES_TYPE_H_
#include <cstdint>

enum class ResourceType : uint8_t {
    Wood,
    Food,
    Gold,
    Stone,    // 可扩展
    Count     // 自动计数，方便定义数组大小
};

#endif // !_RESOURCES_TYPE_H_

