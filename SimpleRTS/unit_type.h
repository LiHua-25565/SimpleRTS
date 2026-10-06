#ifndef _UNIT_TYPE_H_
#define _UNIT_TYPE_H_
#include <cstdint>

enum class UnitEntityType : uint8_t {
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
