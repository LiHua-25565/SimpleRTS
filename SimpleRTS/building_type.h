#ifndef _BUILDING_TYPE_H_
#define _BUILDING_TYPE_H_

#include <cstdint>

enum class BuildingEntityType : uint8_t {
    TownCenter,  // 城镇大厅
    ArcheryRange,   // 靶场（新增）
    Count
};

#endif // !_BUILDING_TYPE_H_
