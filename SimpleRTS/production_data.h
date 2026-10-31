#ifndef _PRODUCTION_DATA_H_
#define _PRODUCTION_DATA_H_

#include "unit_type.h"
#include "resources_type.h"
#include "building_type.h"
#include <vector>
#include <functional>
#include <string>
#include <initializer_list>
#include <memory>

// 科技
struct ResearchItem {
    std::string name;
    float research_time = 0.0f;
    int cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };
};

// 生产类型
enum class ProductionType { Unit, Research, Building };

struct ProductionItem {
    ProductionType type = ProductionType::Unit;
    UnitEntityType unit_type = UnitEntityType::Villager;
    // 用 shared_ptr 保存科技项：内置科技是静态对象（别名指针、不负责释放），
    // 自定义建筑自动生成的科技由列表持有，返回 vector 时也不会悬空。
    std::shared_ptr<ResearchItem> research;
    BuildingEntityType building_type = BuildingEntityType::TownCenter;
    int size_cells = 0;

    float produce_time = 0.0f;
    int cost_amounts[static_cast<int>(ResourceType::Count)] = { 0 };
    std::string unlock_techs;

    ProductionItem() = default;
    ProductionItem(UnitEntityType unit, float time, std::initializer_list<int> costs,
        const std::string& techs = "");
    ProductionItem(std::shared_ptr<ResearchItem> res, float time, std::initializer_list<int> costs);
    ProductionItem(BuildingEntityType building, int cells, float time,
        std::initializer_list<int> costs, const std::string& techs = "");
};

// 生产列表查询（内置和自定义建筑都从定义生成）
std::vector<ProductionItem> get_production_list(BuildingEntityType type);

// 全局建筑列表（空闲生产面板，包含内置与自定义建筑）
const std::vector<ProductionItem>& get_global_build_list();
const ProductionItem* get_build_item(BuildingEntityType type);

#endif // _PRODUCTION_DATA_H_