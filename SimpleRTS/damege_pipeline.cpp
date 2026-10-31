#include "damage_pipeline.h"
#include "game_object.h"
#include "components.h"

#include <algorithm>
#include <cmath>

void DamagePipeline::add_modifier(std::unique_ptr<DamageModifier> modifier) {
    modifiers.push_back(std::move(modifier));
}

int DamagePipeline::calculate(int base_damage, const GameObject* attacker, const GameObject* target) {
    int result = base_damage;
    for (auto& mod : modifiers) {
        result = mod->process(result, attacker, target);
    }
    return result;
}

int ArmorModifier::process(int incoming_damage, const GameObject* attacker, const GameObject* target){
    auto* attack = attacker->get_component<Attack>();
    auto* armor = target->get_component<Armor>();
    if (!armor || !attack) return incoming_damage;

    int damage = std::max(1, incoming_damage);
    int reduction = 0;
    switch (attack->damage_type) {
    case DamageType::Normal:
        reduction = armor->reduction[static_cast<int>(DamageType::Normal)];
        break;
    case DamageType::Pierce:
        // 穿刺只受一半防御减免，专门克制高防目标。
        reduction = armor->reduction[static_cast<int>(DamageType::Pierce)] / 2;
        break;
    case DamageType::Siege: {
        bool building = target->get_component<Structure>() != nullptr;
        if (building) {
            // 攻城对建筑 ×1.5，且只受一半减免。
            damage = (int)std::lround(damage * 1.5f);
            reduction = armor->reduction[static_cast<int>(DamageType::Siege)] / 2;
        }
        else {
            // 攻城器械打单位效率低。
            damage = std::max(1, (int)std::floor(damage * 0.75f));
            reduction = armor->reduction[static_cast<int>(DamageType::Siege)];
        }
        break;
    }
    default:
        break;
    }

    return std::max(1, damage - std::max(0, reduction));
}