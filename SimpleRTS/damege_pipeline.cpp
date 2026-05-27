#include "damage_pipeline.h"
#include "game_object.h"

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

    size_t idx = static_cast<size_t>(armor->type);
    int pen = (idx < static_cast<size_t>(ArmorType::Count)) ? attack->armor_penetration[idx] : 0;
    int effective_armor = std::max(0, armor->armor_value - pen);
    return std::max(1, incoming_damage - effective_armor);
}