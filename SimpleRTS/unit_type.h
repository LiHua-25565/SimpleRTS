#ifndef _UNIT_TYPE_H_
#define _UNIT_TYPE_H_
#include <cstdint>

// 用 uint16_t 作为底层类型：编辑器自定义兵种使用 >= 100 的稳定 ID，
// 直接 cast 成枚举后能穿过 ECS、生产队列和存档，无需到处加“自定义索引”字段。
enum class UnitEntityType : uint16_t {
    Villager,      // 民
    Cavalry,       // 骑
    Spearman,      // 矛
    Archer,        // 弓
    Crossbowman,   // 弩
    Count
};

// 单位占地：2×2 格，与木（Wood 2×2）一致
constexpr int UNIT_SIZE_CELLS = 2;

#endif // !_UNIT_TYPE_H_
