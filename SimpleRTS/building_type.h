#ifndef _BUILDING_TYPE_H_
#define _BUILDING_TYPE_H_

#include <cstdint>

// 同 UnitEntityType：>= 100 的 ID 表示编辑器创建的建筑。
enum class BuildingEntityType : uint16_t {
    TownCenter,  // 城镇大厅
    ArcheryRange,   // 靶场（新增）
    Count
};

#endif // !_BUILDING_TYPE_H_
