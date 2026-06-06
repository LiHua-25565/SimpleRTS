#include "production_data.h"
#include "world_entity_mgr.h"
#include "components.h"

// ---------- 构造函数实现 ----------
ProductionItem::ProductionItem(UnitEntityType unit, float time,
    std::initializer_list<int> costs, const std::string& techs)
    : type(ProductionType::Unit), unit_type(unit), research(nullptr),
    produce_time(time), unlock_techs(techs) {
    int idx = 0;
    for (int c : costs) {
        if (idx < static_cast<int>(ResourceType::Count)) cost_amounts[idx] = c;
        ++idx;
    }
}

ProductionItem::ProductionItem(ResearchItem* res, float time,
    std::initializer_list<int> costs)
    : type(ProductionType::Research), unit_type(UnitEntityType::Villager),
    research(res), produce_time(time) {
    int idx = 0;
    for (int c : costs) {
        if (idx < static_cast<int>(ResourceType::Count)) cost_amounts[idx] = c;
        ++idx;
    }
}

ProductionItem::ProductionItem(BuildingEntityType building, int cells, float time,
    std::initializer_list<int> costs)
    : type(ProductionType::Building), unit_type(UnitEntityType::Villager),
    building_type(building), size_cells(cells), produce_time(time) {
    int idx = 0;
    for (int c : costs) {
        if (idx < static_cast<int>(ResourceType::Count)) cost_amounts[idx] = c;
        ++idx;
    }
}

// ---------- 科技效果 ----------
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

// ---------- 生产列表 ----------
const std::vector<ProductionItem>& get_town_center_production() {
    static const std::vector<ProductionItem> list = {
        ProductionItem(UnitEntityType::Villager, 5.0f, {0, 0, 10, 0, 0}),
        ProductionItem(UnitEntityType::Archer,   8.0f, {0, 0, 0, 15, 0}),
    };
    return list;
}

const std::vector<ProductionItem>& get_archery_range_production() {
    static ResearchItem strongbow{
        u8"强弓",
        15.0f,
        {0, 0, 0, 50, 30},
        [](int player_id) { research_strongbow_effect(player_id); }
    };
    static ResearchItem crossbow{
        u8"弩箭",
        20.0f,
        {0, 0, 0, 80, 40},
        nullptr
    };

    static const std::vector<ProductionItem> list = {
        ProductionItem(UnitEntityType::Archer, 8.0f, {0, 0, 0, 15, 0}),
        ProductionItem(UnitEntityType::Crossbowman, 10.0f, {0, 0, 0, 25, 0}, u8"弩箭"),
        ProductionItem(&strongbow, strongbow.research_time,
                       {0, 0, 0, strongbow.cost_amounts[static_cast<int>(ResourceType::Gold)],
                        strongbow.cost_amounts[static_cast<int>(ResourceType::Stone)]}),
        ProductionItem(&crossbow, crossbow.research_time,
                       {0, 0, 0, crossbow.cost_amounts[static_cast<int>(ResourceType::Gold)],
                        crossbow.cost_amounts[static_cast<int>(ResourceType::Stone)]}),
    };
    return list;
}

const std::vector<ProductionItem>* get_production_list(BuildingEntityType type) {
    switch (type) {
    case BuildingEntityType::TownCenter:   return &get_town_center_production();
    case BuildingEntityType::ArcheryRange: return &get_archery_range_production();
    default: return nullptr;
    }
}

// ---------- 全局建筑列表 ----------
const std::vector<ProductionItem>& get_global_build_list() {
    static const std::vector<ProductionItem> list = {
        ProductionItem(BuildingEntityType::TownCenter,   20, 0.0f, {0, 200, 0, 100, 0}),
        ProductionItem(BuildingEntityType::ArcheryRange, 20, 0.0f, {0, 150, 0, 50,  0}),
    };
    return list;
}

const ProductionItem* get_build_item(BuildingEntityType type) {
    for (const auto& item : get_global_build_list()) {
        if (item.type == ProductionType::Building && item.building_type == type)
            return &item;
    }
    return nullptr;
}