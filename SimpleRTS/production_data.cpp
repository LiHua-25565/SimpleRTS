#include "production_data.h"
#include "entity_defs.h"

#include <algorithm>
#include <memory>

namespace {

void copy_costs(int (&dst)[static_cast<int>(ResourceType::Count)], std::initializer_list<int> costs) {
    int idx = 0;
    for (int c : costs) {
        if (idx < static_cast<int>(ResourceType::Count)) dst[idx] = c;
        ++idx;
    }
}

std::vector<ProductionItem> build_building_production(const BuildingDef& bd) {
    std::vector<ProductionItem> out;
    auto* catalog = EntityCatalog::instance();

    std::vector<std::string> tech_names = bd.research;
    auto add_tech_name = [&](const std::string& csv) {
        for (const auto& name : split_tech_names(csv)) {
            if (name.empty()) continue;
            if (std::find(tech_names.begin(), tech_names.end(), name) == tech_names.end())
                tech_names.push_back(name);
        }
    };
    add_tech_name(bd.requires);

    for (int unit_id : bd.produces) {
        const UnitDef* ud = catalog->find_unit(unit_id);
        if (!ud) continue;
        out.push_back(ProductionItem(static_cast<UnitEntityType>(unit_id), ud->produce_time,
            { ud->cost[static_cast<int>(ResourceType::None)],
              ud->cost[static_cast<int>(ResourceType::Wood)],
              ud->cost[static_cast<int>(ResourceType::Food)],
              ud->cost[static_cast<int>(ResourceType::Gold)],
              ud->cost[static_cast<int>(ResourceType::Stone)] },
            ud->requires));
        add_tech_name(ud->requires);
    }

    // 前置科技一定能在本建筑研究，否则“需要科技”会变成永远无法解锁的死锁。
    for (const auto& name : tech_names) {
        auto research = std::make_shared<ResearchItem>();
        research->name = name;
        const TechDef* tech = catalog->find_tech(name);
        if (tech) {
            research->research_time = std::max(0.1f, tech->research_time);
            for (int r = 0; r < static_cast<int>(ResourceType::Count); ++r)
                research->cost_amounts[r] = std::max(0, tech->cost[r]);
        }
        else {
            research->research_time = std::max(0.1f, bd.research_time);
            research->cost_amounts[static_cast<int>(ResourceType::Gold)] = std::max(0, bd.research_gold);
        }
        out.push_back(ProductionItem(research, research->research_time,
            { research->cost_amounts[static_cast<int>(ResourceType::None)],
              research->cost_amounts[static_cast<int>(ResourceType::Wood)],
              research->cost_amounts[static_cast<int>(ResourceType::Food)],
              research->cost_amounts[static_cast<int>(ResourceType::Gold)],
              research->cost_amounts[static_cast<int>(ResourceType::Stone)] }));
    }

    return out;
}

} // namespace

// ---------- 构造函数实现 ----------
ProductionItem::ProductionItem(UnitEntityType unit, float time,
    std::initializer_list<int> costs, const std::string& techs)
    : type(ProductionType::Unit), unit_type(unit), research(nullptr),
    produce_time(time), unlock_techs(techs) {
    copy_costs(cost_amounts, costs);
}

ProductionItem::ProductionItem(std::shared_ptr<ResearchItem> res, float time,
    std::initializer_list<int> costs)
    : type(ProductionType::Research), unit_type(UnitEntityType::Villager),
    research(std::move(res)), produce_time(time) {
    copy_costs(cost_amounts, costs);
}

ProductionItem::ProductionItem(BuildingEntityType building, int cells, float time,
    std::initializer_list<int> costs, const std::string& techs)
    : type(ProductionType::Building), unit_type(UnitEntityType::Villager),
    building_type(building), size_cells(cells), produce_time(time), unlock_techs(techs) {
    copy_costs(cost_amounts, costs);
}

// ---------- 生产列表 ----------
std::vector<ProductionItem> get_production_list(BuildingEntityType type) {
    const BuildingDef* bd = EntityCatalog::instance()->find_building(type);
    if (!bd) return {};
    return build_building_production(*bd);
}

// ---------- 全局建筑列表 ----------
const std::vector<ProductionItem>& get_global_build_list() {
    static std::vector<ProductionItem> list;
    static int cached_version = -1;

    auto* catalog = EntityCatalog::instance();
    if (cached_version == catalog->version() && !list.empty())
        return list;

    cached_version = catalog->version();
    list.clear();
    for (const auto& bd : catalog->buildings()) {
        list.push_back(ProductionItem(static_cast<BuildingEntityType>(bd.type_id),
            std::max(1, bd.size_cells), std::max(0.1f, bd.build_time),
            { bd.cost[static_cast<int>(ResourceType::None)],
              bd.cost[static_cast<int>(ResourceType::Wood)],
              bd.cost[static_cast<int>(ResourceType::Food)],
              bd.cost[static_cast<int>(ResourceType::Gold)],
              bd.cost[static_cast<int>(ResourceType::Stone)] },
            bd.requires));
    }
    return list;
}

const ProductionItem* get_build_item(BuildingEntityType type) {
    for (const auto& item : get_global_build_list()) {
        if (item.type == ProductionType::Building && item.building_type == type)
            return &item;
    }
    return nullptr;
}
