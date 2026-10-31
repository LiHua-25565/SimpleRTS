#ifndef _DAMAGE_TYPE_H_
#define _DAMAGE_TYPE_H_

#include <cstdint>

// 伤害类型：攻击方选择，防御方按类型分别设置减免值。
// 规则由 ArmorModifier 实现：
//   普通：目标对应减免全额生效。
//   穿刺：只受目标对应减免的 50% 影响（克制高防单位）。
//   攻城：对建筑伤害 ×1.5 且只受 50% 减免；对非建筑伤害 ×0.75。
enum class DamageType : uint8_t {
    Normal,   // 普通
    Pierce,   // 穿刺
    Siege,    // 攻城
    Count
};

inline const char* damage_type_name(DamageType type) {
    switch (type) {
    case DamageType::Normal: return u8"普通";
    case DamageType::Pierce: return u8"穿刺";
    case DamageType::Siege:  return u8"攻城";
    default: return u8"未知";
    }
}

#endif // !_DAMAGE_TYPE_H_
