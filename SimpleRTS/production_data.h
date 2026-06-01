#ifndef _PRODUCTION_DATA_H_
#define _PRODUCTION_DATA_H_

#include "unit_type.h"
#include "resources_type.h"
#include "building_type.h"
#include <vector>

// ---------- 单条生产配置 ----------
struct ProductionItem {
    UnitEntityType unit_type;       // 生产出的单位类型
    float         produce_time;     // 生产耗时（秒）
    int           cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };   // 资源消耗
};

// ---------- 各建筑的生产列表 ----------
inline const std::vector<ProductionItem>& get_town_center_production() {
    static const std::vector<ProductionItem> list = {
        { UnitEntityType::Villager, 5.0f, {0, 0, 10, 0, 0} },   // 农民：5秒，消耗10食物
        { UnitEntityType::Archer,   8.0f, {0, 0, 0, 15, 0} },   // 弓兵：8秒，消耗15黄金
    };
    return list;
}

// ---------- 根据建筑类型获取生产列表 ----------
inline const std::vector<ProductionItem>* get_production_list(BuildingEntityType type) {
    switch (type) {
    case BuildingEntityType::TownCenter: return &get_town_center_production();
    default: return nullptr;
    }
}

#endif // _PRODUCTION_DATA_H_