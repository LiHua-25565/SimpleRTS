#include "tech_effects.h"

#include "components.h"
#include "entity_defs.h"
#include "resources_mgr.h"
#include "world_entity_mgr.h"

#include <algorithm>
#include <cmath>

void apply_tech_effect_to_unit(GameObject* obj, const TechEffect& effect) {
    if (!obj || !obj->check_valid()) return;

    auto* unit = obj->get_component<UnitType>();
    if (!unit) return;
    if (effect.target_type >= 0 && static_cast<int>(unit->type) != effect.target_type) return;

    switch (effect.type) {
    case TechEffectType::AttackDamage: {
        if (auto* attack = obj->get_component<Attack>())
            attack->damage = std::max(0, attack->damage + (int)std::lround(effect.value));
        break;
    }
    case TechEffectType::AttackRange: {
        if (auto* attack = obj->get_component<Attack>())
            attack->range = std::max(0.0f, attack->range + effect.value);
        break;
    }
    case TechEffectType::MaxHealth: {
        if (auto* hp = obj->get_component<Health>()) {
            int delta = (int)std::lround(effect.value);
            hp->max_health = std::max(1, hp->max_health + delta);
            hp->current_health = std::clamp(hp->current_health + delta, 0, hp->max_health);
            if (hp->current_health <= 0 && delta > 0) hp->current_health = hp->max_health;
        }
        break;
    }
    case TechEffectType::MoveSpeed: {
        if (auto* movable = obj->get_component<Movable>())
            movable->speed = std::max(1.0f, movable->speed + effect.value);
        break;
    }
    default:
        break;
    }
}

void apply_tech_effects_to_player(int player_id, const std::string& tech_name) {
    const TechDef* tech = EntityCatalog::instance()->find_tech(tech_name);
    if (!tech || tech->effects.empty()) return;

    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj || !obj->check_valid()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        for (const auto& effect : tech->effects)
            apply_tech_effect_to_unit(obj, effect);
    }
}

void apply_completed_techs_to_unit(GameObject* obj, int player_id) {
    if (!obj || player_id <= 0) return;
    auto* catalog = EntityCatalog::instance();
    for (const auto& tech_name : ResourcesMgr::instance()->get_player_techs(player_id)) {
        const TechDef* tech = catalog->find_tech(tech_name);
        if (!tech) continue;
        for (const auto& effect : tech->effects)
            apply_tech_effect_to_unit(obj, effect);
    }
}
