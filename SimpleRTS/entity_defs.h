#ifndef _ENTITY_DEFS_H_
#define _ENTITY_DEFS_H_

#include "damage_type.h"
#include "resources_type.h"
#include "unit_type.h"
#include "building_type.h"

#include <cstdint>
#include <string>
#include <vector>

// 编辑器创建的兵种/建筑使用独立于内置枚举值的 ID。
// 0..99 留给内置类型（UnitEntityType / BuildingEntityType 的原始值），
// 编辑器类型从 100 开始，直接 cast 进枚举后仍能穿过整个 ECS / 生产 / 渲染链路。
constexpr int CUSTOM_TYPE_ID_BASE = 100;

// 单位插件：决定创建单位时挂载哪些能力组件。
enum UnitPluginFlags : uint32_t {
    UNIT_PLUGIN_NONE   = 0,
    UNIT_PLUGIN_ATTACK = 1u << 0,
    UNIT_PLUGIN_GATHER = 1u << 1,
};

inline bool has_unit_plugin(uint32_t plugins, UnitPluginFlags flag) {
    return (plugins & static_cast<uint32_t>(flag)) != 0;
}

// ============ 兵种定义 ============
struct UnitDef {
    int type_id = 0;                    // 0..99 = 内置；>=100 = 自定义
    std::string name = u8"新兵种";
    std::string texture_text;           // 生成纹理的文字；空 = 用 name 的前 4 个字

    int hp = 100;
    int defense[static_cast<int>(DamageType::Count)] = { 0 };  // 每种伤害的减免
    float speed = 60.0f;

    uint32_t plugins = UNIT_PLUGIN_ATTACK;

    // 攻击插件参数
    int attack_damage = 10;
    DamageType damage_type = DamageType::Normal;
    float attack_range = 30.0f;
    float attack_interval = 1.0f;
    bool attack_ranged = false;

    // 采集插件参数
    int gather_amount = 10;
    float gather_interval = 1.0f;
    int carry_capacity = 30;

    // 生产参数
    float produce_time = 5.0f;
    int cost[static_cast<int>(ResourceType::Count)] = { 0 };
    std::string requires;               // 前置科技，逗号分隔

    bool builtin() const { return type_id >= 0 && type_id < CUSTOM_TYPE_ID_BASE; }
    bool has_attack() const { return has_unit_plugin(plugins, UNIT_PLUGIN_ATTACK); }
    bool has_gather() const { return has_unit_plugin(plugins, UNIT_PLUGIN_GATHER); }
};

// ============ 建筑定义 ============
struct BuildingDef {
    int type_id = 0;                    // 0..99 = 内置；>=100 = 自定义
    std::string name = u8"新建筑";
    std::string texture_text;           // 生成纹理的文字；空 = 用 name 的前 4 个字

    int hp = 1000;
    int defense[static_cast<int>(DamageType::Count)] = { 0 };
    int size_cells = 20;
    float build_time = 5.0f;

    int cost[static_cast<int>(ResourceType::Count)] = { 0 };
    std::string requires;               // 建造前置科技，逗号分隔

    bool dropoff = false;               // 资源提交点插件
    float research_time = 15.0f;        // 旧文件兜底值
    int research_gold = 50;

    std::vector<int> produces;          // 可生产单位类型 ID（内置 + 自定义）
    std::vector<std::string> research;  // 可研究科技名

    bool builtin() const { return type_id >= 0 && type_id < CUSTOM_TYPE_ID_BASE; }
};

// ============ 科技效果 ============
// 类似“强弓”：研究完成后，对指定兵种（或全部兵种）叠加属性。
enum class TechEffectType : uint8_t {
    AttackDamage,
    AttackRange,
    MaxHealth,
    MoveSpeed,
    Count
};

inline const char* tech_effect_type_name(TechEffectType type) {
    switch (type) {
    case TechEffectType::AttackDamage: return u8"攻击力";
    case TechEffectType::AttackRange:  return u8"攻击距离";
    case TechEffectType::MaxHealth:    return u8"生命上限";
    case TechEffectType::MoveSpeed:    return u8"移动速度";
    default: return u8"未知";
    }
}

struct TechEffect {
    int target_type = -1;               // -1 = 全部兵种；否则为单位 type_id
    TechEffectType type = TechEffectType::AttackRange;
    float value = 0.0f;
};

// ============ 科技定义 ============
struct TechDef {
    std::string name = u8"新科技";
    float research_time = 15.0f;
    int cost[static_cast<int>(ResourceType::Count)] = { 0 };
    std::vector<TechEffect> effects;
};

// ============ 全局定义库 ============
class EntityCatalog {
public:
    static EntityCatalog* instance();

    int version() const { return version_; }
    void touch() { ++version_; }

    void clear();
    void load_default_if_exists();

    // 内置兵种/建筑/科技不存在时补默认定义（载入旧文件或首次运行时调用）。
    void ensure_builtin_defaults();

    UnitDef* add_unit();
    BuildingDef* add_building();
    TechDef* add_tech();
    bool remove_unit(int type_id);
    bool remove_building(int type_id);
    bool remove_tech(const std::string& name);

    const std::vector<UnitDef>& units() const { return units_; }
    const std::vector<BuildingDef>& buildings() const { return buildings_; }
    const std::vector<TechDef>& techs() const { return techs_; }

    UnitDef* find_unit(int type_id);
    const UnitDef* find_unit(int type_id) const;
    BuildingDef* find_building(int type_id);
    const BuildingDef* find_building(int type_id) const;
    TechDef* find_tech(const std::string& name);
    const TechDef* find_tech(const std::string& name) const;

    bool rename_tech(const std::string& old_name, const std::string& new_name);

    UnitDef* find_unit(UnitEntityType type);
    const UnitDef* find_unit(UnitEntityType type) const;
    BuildingDef* find_building(BuildingEntityType type);
    const BuildingDef* find_building(BuildingEntityType type) const;

    bool is_custom_unit(UnitEntityType type) const;
    bool is_custom_building(BuildingEntityType type) const;

    int next_unit_id() const;
    int next_building_id() const;

    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);

private:
    EntityCatalog();

    std::vector<UnitDef> units_;
    std::vector<BuildingDef> buildings_;
    std::vector<TechDef> techs_;
    int version_ = 1;
    int next_unit_serial_ = 0;
    int next_building_serial_ = 0;
    int next_tech_serial_ = 0;
};

bool is_builtin_playable_unit(UnitEntityType type);

struct UnitTypeChoice {
    UnitEntityType type;
    std::string name;
    bool builtin = false;
};
std::vector<UnitTypeChoice> list_unit_type_choices();

// 科技名列表工具。
std::vector<std::string> split_tech_names(const std::string& csv);
std::string join_tech_names(const std::vector<std::string>& names);
bool tech_csv_has(const std::string& csv, const std::string& name);
std::string tech_csv_toggle(const std::string& csv, const std::string& name, bool enabled);
std::string sanitize_tech_name(const std::string& name);

#endif // !_ENTITY_DEFS_H_
