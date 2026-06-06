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
enum class ProductionType { Unit, Research, Building };

struct ProductionItem {
    ProductionType type = ProductionType::Unit;
    UnitEntityType unit_type = UnitEntityType::Villager;
    ResearchItem* research = nullptr;
    BuildingEntityType building_type = BuildingEntityType::TownCenter;
    int size_cells = 0;

    float produce_time = 0.0f;
    int cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };
    std::string unlock_techs;

    ProductionItem() = default;
    ProductionItem(UnitEntityType unit, float time, std::initializer_list<int> costs,
        const std::string& techs = "");
    ProductionItem(ResearchItem* res, float time, std::initializer_list<int> costs);
    ProductionItem(BuildingEntityType building, int cells, float time,
        std::initializer_list<int> costs);
};

// 强弓科技效果
void research_strongbow_effect(int player_id);

// 生产列表查询
const std::vector<ProductionItem>& get_town_center_production();
const std::vector<ProductionItem>& get_archery_range_production();
const std::vector<ProductionItem>* get_production_list(BuildingEntityType type);

// 全局建筑列表（空闲生产面板）
const std::vector<ProductionItem>& get_global_build_list();
const ProductionItem* get_build_item(BuildingEntityType type);

#endif // _PRODUCTION_DATA_H_