#include "production_data.h"
#include "world_entity_mgr.h"
#include "components.h"

void research_strongbow_effect(int player_id) {
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* unit = obj->get_component<UnitType>();
        if (!unit || unit->type != UnitEntityType::Archer) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        auto* attack = obj->get_component<Attack>();
        if (attack) {
            attack->range += 50.0f;
        }
    }
}