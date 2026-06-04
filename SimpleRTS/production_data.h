#ifndef _PRODUCTION_DATA_H_
#define _PRODUCTION_DATA_H_

#include "unit_type.h"
#include "resources_type.h"
#include "building_type.h"
#include <vector>
#include <functional>
#include <string>
#include <initializer_list>

// 科技
struct ResearchItem {
    std::string name;
    float research_time;
    int cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };
    std::function<void(int player_id)> apply_effect;
};

// 生产类型
enum class ProductionType { Unit, Research };

struct ProductionItem {
    ProductionType type = ProductionType::Unit;
    UnitEntityType unit_type = UnitEntityType::Villager;
    ResearchItem* research = nullptr;
    float produce_time = 0.0f;
    int cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };

    ProductionItem() = default;

    ProductionItem(UnitEntityType unit, float time, std::initializer_list<int> costs)
        : type(ProductionType::Unit), unit_type(unit), research(nullptr), produce_time(time) {
        int idx = 0;
        for (int c : costs) {
            if (idx < static_cast<int>(ResourceType::Count))
                cost_amounts[idx] = c;
            ++idx;
        }
    }

    ProductionItem(ResearchItem* res, float time, std::initializer_list<int> costs)
        : type(ProductionType::Research), unit_type(UnitEntityType::Villager),
        research(res), produce_time(time) {
        int idx = 0;
        for (int c : costs) {
            if (idx < static_cast<int>(ResourceType::Count))
                cost_amounts[idx] = c;
            ++idx;
        }
    }
};

// 城镇中心生产列表
inline const std::vector<ProductionItem>& get_town_center_production() {
    static const std::vector<ProductionItem> list = {
        ProductionItem(UnitEntityType::Villager, 5.0f, {0, 0, 10, 0, 0}),
        ProductionItem(UnitEntityType::Archer,   8.0f, {0, 0, 0, 15, 0}),
    };
    return list;
}

// 强弓科技效果：提升己方所有弓兵射程 +50（声明）
void research_strongbow_effect(int player_id);

// 靶场生产列表
inline const std::vector<ProductionItem>& get_archery_range_production() {
    static ResearchItem strongbow{
        u8"强弓",
        15.0f,
        {0, 0, 0, 50, 30},         // 50黄金, 30石头
        research_strongbow_effect   // 函数指针
    };

    static const std::vector<ProductionItem> list = {
        ProductionItem(UnitEntityType::Archer, 8.0f, {0, 0, 0, 15, 0}),
        ProductionItem(&strongbow, strongbow.research_time,
                       {0, 0, 0, strongbow.cost_amounts[static_cast<int>(ResourceType::Gold)],
                        strongbow.cost_amounts[static_cast<int>(ResourceType::Stone)]}),
    };
    return list;
}

// 根据建筑类型获取生产列表
inline const std::vector<ProductionItem>* get_production_list(BuildingEntityType type) {
    switch (type) {
    case BuildingEntityType::TownCenter:   return &get_town_center_production();
    case BuildingEntityType::ArcheryRange: return &get_archery_range_production();
    default: return nullptr;
    }
}

#endif // _PRODUCTION_DATA_H_