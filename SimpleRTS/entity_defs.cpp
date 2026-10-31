#include "entity_defs.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace {

constexpr const char* DEF_MAGIC = "MYSTRDEF";
constexpr int DEF_VERSION = 4;

std::string sanitize_field(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

int parse_int(const std::string& s, int fallback = 0) {
    try { return std::stoi(s); }
    catch (...) { return fallback; }
}

float parse_float(const std::string& s, float fallback = 0.0f) {
    try { return std::stof(s); }
    catch (...) { return fallback; }
}

bool parse_bool(const std::string& s) {
    return parse_int(s, 0) != 0;
}

std::vector<std::string> split_comma(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';') {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        }
        else {
            cur.push_back(c);
        }
    }
    std::string t = trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

std::string join_comma(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += items[i];
    }
    return out;
}

std::vector<int> split_int_list(const std::string& s) {
    std::vector<int> out;
    for (const auto& part : split_comma(s)) out.push_back(parse_int(part, -1));
    return out;
}

DamageType clamp_damage_type(int v) {
    if (v < 0 || v >= static_cast<int>(DamageType::Count)) return DamageType::Normal;
    return static_cast<DamageType>(v);
}

std::string serialize_effects(const std::vector<TechEffect>& effects) {
    std::string out;
    for (size_t i = 0; i < effects.size(); ++i) {
        if (i) out += ";";
        const auto& e = effects[i];
        out += std::to_string(e.target_type);
        out += ":";
        out += std::to_string(static_cast<int>(e.type));
        out += ":";
        out += std::to_string(e.value);
    }
    return out;
}

std::vector<TechEffect> parse_effects(const std::string& text) {
    std::vector<TechEffect> out;
    std::string cur;
    auto flush = [&](const std::string& item) {
        if (item.empty()) return;
        std::vector<std::string> parts;
        std::string p;
        for (char c : item) {
            if (c == ':') { parts.push_back(p); p.clear(); }
            else p.push_back(c);
        }
        parts.push_back(p);
        if (parts.size() < 3) return;
        TechEffect e;
        e.target_type = parse_int(parts[0], -1);
        int t = parse_int(parts[1], 0);
        if (t < 0 || t >= static_cast<int>(TechEffectType::Count)) return;
        e.type = static_cast<TechEffectType>(t);
        e.value = parse_float(parts[2], 0.0f);
        if (e.value < -99999.0f) e.value = -99999.0f;
        if (e.value > 99999.0f) e.value = 99999.0f;
        out.push_back(e);
    };
    for (char c : text) {
        if (c == ';') { flush(cur); cur.clear(); }
        else cur.push_back(c);
    }
    flush(cur);
    return out;
}

} // namespace

// ==================== 单例 / 基础操作 ====================

EntityCatalog::EntityCatalog() {
    ensure_builtin_defaults();
}

EntityCatalog* EntityCatalog::instance() {
    static EntityCatalog catalog;
    return &catalog;
}

void EntityCatalog::ensure_builtin_defaults() {
    bool changed = false;

    auto ensure_unit = [&](int id, const UnitDef& tmpl) {
        if (find_unit(id)) return;
        units_.push_back(tmpl);
        units_.back().type_id = id;
        changed = true;
    };

    // 内置兵种：与重构前 factories.cpp 的数值保持一致。
    {
        UnitDef d;
        d.type_id = static_cast<int>(UnitEntityType::Villager);
        d.name = u8"农民";
        d.hp = 50;
        d.defense[static_cast<int>(DamageType::Normal)] = 1;
        d.speed = 60.0f;
        d.plugins = UNIT_PLUGIN_ATTACK | UNIT_PLUGIN_GATHER;
        d.attack_damage = 10;
        d.damage_type = DamageType::Normal;
        d.attack_range = 30.0f;
        d.attack_interval = 1.0f;
        d.attack_ranged = false;
        d.gather_amount = 10;
        d.gather_interval = 1.0f;
        d.carry_capacity = 30;
        d.produce_time = 5.0f;
        d.cost[static_cast<int>(ResourceType::Gold)] = 10;
        ensure_unit(static_cast<int>(UnitEntityType::Villager), d);
    }
    {
        UnitDef d;
        d.type_id = static_cast<int>(UnitEntityType::Archer);
        d.name = u8"弓兵";
        d.hp = 40;
        d.speed = 60.0f;
        d.plugins = UNIT_PLUGIN_ATTACK;
        d.attack_damage = 8;
        d.damage_type = DamageType::Pierce;
        d.attack_range = 200.0f;
        d.attack_interval = 1.5f;
        d.attack_ranged = true;
        d.produce_time = 8.0f;
        d.cost[static_cast<int>(ResourceType::Gold)] = 15;
        ensure_unit(static_cast<int>(UnitEntityType::Archer), d);
    }
    {
        UnitDef d;
        d.type_id = static_cast<int>(UnitEntityType::Crossbowman);
        d.name = u8"弩手";
        d.hp = 45;
        d.defense[static_cast<int>(DamageType::Normal)] = 1;
        d.speed = 60.0f;
        d.plugins = UNIT_PLUGIN_ATTACK;
        d.attack_damage = 12;
        d.damage_type = DamageType::Pierce;
        d.attack_range = 250.0f;
        d.attack_interval = 1.8f;
        d.attack_ranged = true;
        d.produce_time = 10.0f;
        d.cost[static_cast<int>(ResourceType::Gold)] = 25;
        d.requires = u8"弩箭";
        ensure_unit(static_cast<int>(UnitEntityType::Crossbowman), d);
    }

    auto ensure_building = [&](int id, const BuildingDef& tmpl) {
        if (find_building(id)) return;
        buildings_.push_back(tmpl);
        buildings_.back().type_id = id;
        changed = true;
    };

    {
        BuildingDef d;
        d.type_id = static_cast<int>(BuildingEntityType::TownCenter);
        d.name = u8"城镇大厅";
        d.hp = 1000;
        d.size_cells = 20;
        d.build_time = 5.0f;
        d.cost[static_cast<int>(ResourceType::Wood)] = 200;
        d.cost[static_cast<int>(ResourceType::Gold)] = 100;
        d.dropoff = true;
        d.produces = { static_cast<int>(UnitEntityType::Villager), static_cast<int>(UnitEntityType::Archer) };
        ensure_building(static_cast<int>(BuildingEntityType::TownCenter), d);
    }
    {
        BuildingDef d;
        d.type_id = static_cast<int>(BuildingEntityType::ArcheryRange);
        d.name = u8"靶场";
        d.hp = 1200;
        d.size_cells = 20;
        d.build_time = 5.0f;
        d.cost[static_cast<int>(ResourceType::Wood)] = 150;
        d.cost[static_cast<int>(ResourceType::Gold)] = 50;
        d.dropoff = false;
        d.produces = { static_cast<int>(UnitEntityType::Archer), static_cast<int>(UnitEntityType::Crossbowman) };
        d.research = { u8"强弓", u8"弩箭" };
        ensure_building(static_cast<int>(BuildingEntityType::ArcheryRange), d);
    }

    auto ensure_tech = [&](const TechDef& tmpl) {
        if (find_tech(tmpl.name)) return;
        techs_.push_back(tmpl);
        changed = true;
    };

    {
        TechDef t;
        t.name = u8"强弓";
        t.research_time = 15.0f;
        t.cost[static_cast<int>(ResourceType::Gold)] = 50;
        t.cost[static_cast<int>(ResourceType::Stone)] = 30;
        TechEffect e;
        e.target_type = static_cast<int>(UnitEntityType::Archer);
        e.type = TechEffectType::AttackRange;
        e.value = 50.0f;
        t.effects.push_back(e);
        ensure_tech(t);
    }
    {
        TechDef t;
        t.name = u8"弩箭";
        t.research_time = 20.0f;
        t.cost[static_cast<int>(ResourceType::Gold)] = 80;
        t.cost[static_cast<int>(ResourceType::Stone)] = 40;
        ensure_tech(t);
    }

    std::sort(units_.begin(), units_.end(),
        [](const UnitDef& a, const UnitDef& b) { return a.type_id < b.type_id; });
    std::sort(buildings_.begin(), buildings_.end(),
        [](const BuildingDef& a, const BuildingDef& b) { return a.type_id < b.type_id; });

    if (changed) touch();
}

void EntityCatalog::clear() {
    units_.clear();
    buildings_.clear();
    techs_.clear();
    next_unit_serial_ = 0;
    next_building_serial_ = 0;
    next_tech_serial_ = 0;
    touch();
}

int EntityCatalog::next_unit_id() const {
    return CUSTOM_TYPE_ID_BASE + next_unit_serial_;
}

int EntityCatalog::next_building_id() const {
    return CUSTOM_TYPE_ID_BASE + next_building_serial_;
}

UnitDef* EntityCatalog::add_unit() {
    UnitDef def;
    def.type_id = next_unit_id();
    ++next_unit_serial_;
    def.name = u8"新兵种" + std::to_string(def.type_id - CUSTOM_TYPE_ID_BASE + 1);
    units_.push_back(def);
    touch();
    return &units_.back();
}

BuildingDef* EntityCatalog::add_building() {
    BuildingDef def;
    def.type_id = next_building_id();
    ++next_building_serial_;
    def.name = u8"新建筑" + std::to_string(def.type_id - CUSTOM_TYPE_ID_BASE + 1);
    buildings_.push_back(def);
    touch();
    return &buildings_.back();
}

TechDef* EntityCatalog::add_tech() {
    TechDef def;
    std::string base = u8"新科技";
    std::string name;
    do {
        ++next_tech_serial_;
        name = base + std::to_string(next_tech_serial_);
    } while (find_tech(name) != nullptr);
    def.name = name;
    def.cost[static_cast<int>(ResourceType::Gold)] = 50;
    techs_.push_back(def);
    touch();
    return &techs_.back();
}

bool EntityCatalog::remove_unit(int type_id) {
    if (type_id < CUSTOM_TYPE_ID_BASE) return false;
    auto it = std::find_if(units_.begin(), units_.end(),
        [type_id](const UnitDef& d) { return d.type_id == type_id; });
    if (it == units_.end()) return false;
    units_.erase(it);
    for (auto& b : buildings_) {
        b.produces.erase(std::remove(b.produces.begin(), b.produces.end(), type_id), b.produces.end());
    }
    for (auto& t : techs_) {
        t.effects.erase(std::remove_if(t.effects.begin(), t.effects.end(),
            [type_id](const TechEffect& e) { return e.target_type == type_id; }), t.effects.end());
    }
    touch();
    return true;
}

bool EntityCatalog::remove_building(int type_id) {
    if (type_id < CUSTOM_TYPE_ID_BASE) return false;
    auto it = std::find_if(buildings_.begin(), buildings_.end(),
        [type_id](const BuildingDef& d) { return d.type_id == type_id; });
    if (it == buildings_.end()) return false;
    buildings_.erase(it);
    touch();
    return true;
}

bool EntityCatalog::remove_tech(const std::string& name) {
    auto it = std::find_if(techs_.begin(), techs_.end(),
        [&](const TechDef& t) { return t.name == name; });
    if (it == techs_.end()) return false;
    techs_.erase(it);

    for (auto& u : units_) u.requires = tech_csv_toggle(u.requires, name, false);
    for (auto& b : buildings_) {
        b.requires = tech_csv_toggle(b.requires, name, false);
        b.research.erase(std::remove(b.research.begin(), b.research.end(), name), b.research.end());
    }
    touch();
    return true;
}

bool EntityCatalog::rename_tech(const std::string& old_name, const std::string& new_name) {
    const std::string old_key = old_name;
    const std::string new_key = sanitize_tech_name(new_name);
    if (old_key == new_key) return true;
    if (new_key.empty()) return false;
    if (find_tech(new_key)) return false;
    TechDef* tech = find_tech(old_key);
    if (!tech) return false;
    tech->name = new_key;

    auto replace_in_csv = [&](std::string& csv) {
        auto names = split_tech_names(csv);
        for (auto& n : names) if (n == old_key) n = new_key;
        csv = join_tech_names(names);
    };
    for (auto& u : units_) replace_in_csv(u.requires);
    for (auto& b : buildings_) {
        replace_in_csv(b.requires);
        for (auto& r : b.research) if (r == old_key) r = new_key;
    }
    touch();
    return true;
}

UnitDef* EntityCatalog::find_unit(int type_id) {
    for (auto& d : units_) if (d.type_id == type_id) return &d;
    return nullptr;
}

const UnitDef* EntityCatalog::find_unit(int type_id) const {
    for (const auto& d : units_) if (d.type_id == type_id) return &d;
    return nullptr;
}

BuildingDef* EntityCatalog::find_building(int type_id) {
    for (auto& d : buildings_) if (d.type_id == type_id) return &d;
    return nullptr;
}

const BuildingDef* EntityCatalog::find_building(int type_id) const {
    for (const auto& d : buildings_) if (d.type_id == type_id) return &d;
    return nullptr;
}

TechDef* EntityCatalog::find_tech(const std::string& name) {
    for (auto& t : techs_) if (t.name == name) return &t;
    return nullptr;
}

const TechDef* EntityCatalog::find_tech(const std::string& name) const {
    for (const auto& t : techs_) if (t.name == name) return &t;
    return nullptr;
}

UnitDef* EntityCatalog::find_unit(UnitEntityType type) {
    return find_unit(static_cast<int>(type));
}

const UnitDef* EntityCatalog::find_unit(UnitEntityType type) const {
    return find_unit(static_cast<int>(type));
}

BuildingDef* EntityCatalog::find_building(BuildingEntityType type) {
    return find_building(static_cast<int>(type));
}

const BuildingDef* EntityCatalog::find_building(BuildingEntityType type) const {
    return find_building(static_cast<int>(type));
}

bool EntityCatalog::is_custom_unit(UnitEntityType type) const {
    return static_cast<int>(type) >= CUSTOM_TYPE_ID_BASE && find_unit(type) != nullptr;
}

bool EntityCatalog::is_custom_building(BuildingEntityType type) const {
    return static_cast<int>(type) >= CUSTOM_TYPE_ID_BASE && find_building(type) != nullptr;
}

// ==================== 文本序列化 ====================
//
// .srdef（UTF-8 文本，制表符分隔，版本 3）：
//   MYSTRDEF 3
//   TECHS <n>
//   T <name> <research_time> <wood> <food> <gold> <stone> <effects>
//     effects = target:type:value;target:type:value
//   UNITS <n>
//   U <id> <name> <texture_text> <hp> <def_normal> <def_pierce> <def_siege>
//     <speed> <plugins> <damage> <damage_type> <range> <interval> <ranged>
//     <gather_amount> <gather_interval> <carry> <produce_time>
//     <wood> <food> <gold> <stone> <requires_csv>
//   BUILDINGS <n>
//   B <id> <name> <texture_text> <hp> <def_normal> <def_pierce> <def_siege>
//     <size> <build_time> <wood> <food> <gold> <stone> <requires_csv>
//     <dropoff> <research_time_legacy> <research_gold_legacy>
//     <produces_csv> <research_csv>
//
// 版本 2 旧文件仍可载入：旧 armor_value 会映射成所有伤害类型的减免。

bool EntityCatalog::save_to_file(const std::string& path) const {
    std::error_code ec;
    std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path()) {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out << DEF_MAGIC << " " << DEF_VERSION << "\n";
    out << "TECHS " << techs_.size() << "\n";
    for (const auto& t : techs_) {
        out << "T\t" << sanitize_field(t.name)
            << "\t" << t.research_time
            << "\t" << t.cost[static_cast<int>(ResourceType::Wood)]
            << "\t" << t.cost[static_cast<int>(ResourceType::Food)]
            << "\t" << t.cost[static_cast<int>(ResourceType::Gold)]
            << "\t" << t.cost[static_cast<int>(ResourceType::Stone)]
            << "\t" << serialize_effects(t.effects)
            << "\n";
    }
    out << "UNITS " << units_.size() << "\n";
    for (const auto& d : units_) {
        std::vector<std::string> f;
        f.push_back("U");
        f.push_back(std::to_string(d.type_id));
        f.push_back(sanitize_field(d.name));
        f.push_back(sanitize_field(d.texture_text));
        f.push_back(std::to_string(d.hp));
        for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
            f.push_back(std::to_string(d.defense[i]));
        f.push_back(std::to_string(d.speed));
        f.push_back(std::to_string(d.plugins));
        f.push_back(std::to_string(d.attack_damage));
        f.push_back(std::to_string(static_cast<int>(d.damage_type)));
        f.push_back(std::to_string(d.attack_range));
        f.push_back(std::to_string(d.attack_interval));
        f.push_back(std::to_string(d.attack_ranged ? 1 : 0));
        f.push_back(std::to_string(d.gather_amount));
        f.push_back(std::to_string(d.gather_interval));
        f.push_back(std::to_string(d.carry_capacity));
        f.push_back(std::to_string(d.produce_time));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Wood)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Food)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Gold)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Stone)]));
        f.push_back(sanitize_field(d.requires));

        for (size_t i = 0; i < f.size(); ++i) {
            if (i) out << '\t';
            out << f[i];
        }
        out << "\n";
    }

    out << "BUILDINGS " << buildings_.size() << "\n";
    for (const auto& d : buildings_) {
        std::vector<std::string> f;
        f.push_back("B");
        f.push_back(std::to_string(d.type_id));
        f.push_back(sanitize_field(d.name));
        f.push_back(sanitize_field(d.texture_text));
        f.push_back(std::to_string(d.hp));
        for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
            f.push_back(std::to_string(d.defense[i]));
        f.push_back(std::to_string(d.size_cells));
        f.push_back(std::to_string(d.build_time));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Wood)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Food)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Gold)]));
        f.push_back(std::to_string(d.cost[static_cast<int>(ResourceType::Stone)]));
        f.push_back(sanitize_field(d.requires));
        f.push_back(std::to_string(d.dropoff ? 1 : 0));
        f.push_back(std::to_string(d.research_time));
        f.push_back(std::to_string(d.research_gold));

        std::string produces;
        for (size_t i = 0; i < d.produces.size(); ++i) {
            if (i) produces += ",";
            produces += std::to_string(d.produces[i]);
        }
        f.push_back(produces);
        f.push_back(sanitize_field(join_comma(d.research)));

        for (size_t i = 0; i < f.size(); ++i) {
            if (i) out << '\t';
            out << f[i];
        }
        out << "\n";
    }

    return out.good();
}

bool EntityCatalog::load_from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::string line;
    if (!std::getline(in, line)) return false;
    if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
        line.erase(0, 3);
    }
    int version = 0;
    {
        std::istringstream ls(line);
        std::string magic;
        if (!(ls >> magic >> version)) return false;
        if (magic != DEF_MAGIC) return false;
    }

    std::vector<UnitDef> new_units;
    std::vector<BuildingDef> new_buildings;
    std::vector<TechDef> new_techs;

    auto read_count = [&](const char* key, int& count) -> bool {
        if (!std::getline(in, line)) return false;
        std::istringstream ls(line);
        std::string k;
        int n = 0;
        if (!(ls >> k >> n) || k != key || n < 0) return false;
        count = n;
        return true;
    };

    auto read_tab_fields = [&](std::vector<std::string>& fields) -> bool {
        if (!std::getline(in, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        fields.clear();
        std::string cur;
        for (char c : line) {
            if (c == '\t') { fields.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        fields.push_back(cur);
        return true;
    };

    // TECHS 段（版本 2+）在 UNITS 之前；版本 1 没有这一段。
    int unit_count = 0;
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("TECHS ", 0) == 0) {
        int tech_count = 0;
        {
            std::istringstream ls(line);
            std::string k;
            if (!(ls >> k >> tech_count) || k != "TECHS" || tech_count < 0) return false;
        }
        std::set<std::string> used_names;
        for (int i = 0; i < tech_count; ++i) {
            std::vector<std::string> f;
            if (!read_tab_fields(f) || f.size() < 7 || f[0] != "T") return false;
            TechDef t;
            t.name = sanitize_tech_name(trim(f[1]));
            if (t.name.empty()) t.name = u8"科技";
            std::string base = t.name;
            int suffix = 2;
            while (used_names.count(t.name)) t.name = base + "_" + std::to_string(suffix++);
            used_names.insert(t.name);
            t.research_time = std::clamp(parse_float(f[2], 15.0f), 0.1f, 600.0f);
            t.cost[static_cast<int>(ResourceType::Wood)] = std::clamp(parse_int(f[3], 0), 0, 999999);
            t.cost[static_cast<int>(ResourceType::Food)] = std::clamp(parse_int(f[4], 0), 0, 999999);
            t.cost[static_cast<int>(ResourceType::Gold)] = std::clamp(parse_int(f[5], 0), 0, 999999);
            t.cost[static_cast<int>(ResourceType::Stone)] = std::clamp(parse_int(f[6], 0), 0, 999999);
            if (f.size() >= 8) t.effects = parse_effects(f[7]);
            new_techs.push_back(std::move(t));
        }
        int dummy = 0;
        if (!read_count("UNITS", dummy)) return false;
        unit_count = dummy;
    }
    else {
        std::istringstream ls(line);
        std::string k;
        if (!(ls >> k >> unit_count) || k != "UNITS" || unit_count < 0) return false;
    }

    std::set<int> used_unit_ids;
    for (int i = 0; i < unit_count; ++i) {
        std::vector<std::string> f;
        if (!read_tab_fields(f) || f[0] != "U") return false;
        UnitDef d;
        if (f.size() >= 24) {
            // 版本 3
            d.type_id = parse_int(f[1], 0);
            d.name = f[2];
            if (version >= 4) d.texture_text = f[3];
            d.hp = parse_int(f[4], 100);
            for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
                d.defense[k] = parse_int(f[5 + k], 0);
            d.speed = parse_float(f[8], 60.0f);
            d.plugins = (uint32_t)parse_int(f[9], 1);
            d.attack_damage = parse_int(f[10], 10);
            d.damage_type = clamp_damage_type(parse_int(f[11], 0));
            d.attack_range = parse_float(f[12], 30.0f);
            d.attack_interval = parse_float(f[13], 1.0f);
            d.attack_ranged = parse_bool(f[14]);
            d.gather_amount = parse_int(f[15], 10);
            d.gather_interval = parse_float(f[16], 1.0f);
            d.carry_capacity = parse_int(f[17], 30);
            d.produce_time = parse_float(f[18], 5.0f);
            d.cost[static_cast<int>(ResourceType::Wood)] = parse_int(f[19], 0);
            d.cost[static_cast<int>(ResourceType::Food)] = parse_int(f[20], 0);
            d.cost[static_cast<int>(ResourceType::Gold)] = parse_int(f[21], 0);
            d.cost[static_cast<int>(ResourceType::Stone)] = parse_int(f[22], 0);
            d.requires = f[23];
        }
        else if (f.size() >= 26) {
            // 版本 1/2：armor_type + armor_value + armor_pen
            d.type_id = parse_int(f[1], 0);
            d.name = f[2];
            d.hp = parse_int(f[4], 100);
            int old_armor = parse_int(f[6], 0);
            for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
                d.defense[k] = old_armor;
            d.speed = parse_float(f[7], 60.0f);
            d.plugins = (uint32_t)parse_int(f[8], 1);
            d.attack_damage = parse_int(f[9], 10);
            d.damage_type = DamageType::Normal;
            d.attack_range = parse_float(f[10], 30.0f);
            d.attack_interval = parse_float(f[11], 1.0f);
            d.attack_ranged = parse_bool(f[12]);
            d.gather_amount = parse_int(f[17], 10);
            d.gather_interval = parse_float(f[18], 1.0f);
            d.carry_capacity = parse_int(f[19], 30);
            d.produce_time = parse_float(f[20], 5.0f);
            d.cost[static_cast<int>(ResourceType::Wood)] = parse_int(f[21], 0);
            d.cost[static_cast<int>(ResourceType::Food)] = parse_int(f[22], 0);
            d.cost[static_cast<int>(ResourceType::Gold)] = parse_int(f[23], 0);
            d.cost[static_cast<int>(ResourceType::Stone)] = parse_int(f[24], 0);
            d.requires = f[25];
        }
        else {
            return false;
        }

        if (d.name.empty()) d.name = u8"兵种";
        d.hp = std::clamp(d.hp, 1, 999999);
        for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
            d.defense[k] = std::clamp(d.defense[k], 0, 9999);
        d.speed = std::clamp(d.speed, 1.0f, 2000.0f);
        d.attack_damage = std::clamp(d.attack_damage, 0, 99999);
        d.attack_range = std::clamp(d.attack_range, 0.0f, 5000.0f);
        d.attack_interval = std::clamp(d.attack_interval, 0.05f, 60.0f);
        d.gather_amount = std::clamp(d.gather_amount, 0, 99999);
        d.gather_interval = std::clamp(d.gather_interval, 0.05f, 60.0f);
        d.carry_capacity = std::clamp(d.carry_capacity, 1, 99999);
        d.produce_time = std::clamp(d.produce_time, 0.1f, 600.0f);
        for (int r = 1; r < static_cast<int>(ResourceType::Count); ++r)
            d.cost[r] = std::clamp(d.cost[r], 0, 999999);

        if (d.type_id < 0 || used_unit_ids.count(d.type_id)) {
            int candidate = CUSTOM_TYPE_ID_BASE;
            while (used_unit_ids.count(candidate)) ++candidate;
            d.type_id = candidate;
        }
        used_unit_ids.insert(d.type_id);
        new_units.push_back(std::move(d));
    }

    int building_count = 0;
    if (!read_count("BUILDINGS", building_count)) return false;
    std::set<int> used_building_ids;
    for (int i = 0; i < building_count; ++i) {
        std::vector<std::string> f;
        if (!read_tab_fields(f) || f[0] != "B") return false;
        BuildingDef d;
        if (f.size() >= 20) {
            // 版本 3
            d.type_id = parse_int(f[1], 0);
            d.name = f[2];
            if (version >= 4) d.texture_text = f[3];
            d.hp = parse_int(f[4], 1000);
            for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
                d.defense[k] = parse_int(f[5 + k], 0);
            d.size_cells = parse_int(f[8], 20);
            d.build_time = parse_float(f[9], 5.0f);
            d.cost[static_cast<int>(ResourceType::Wood)] = parse_int(f[10], 0);
            d.cost[static_cast<int>(ResourceType::Food)] = parse_int(f[11], 0);
            d.cost[static_cast<int>(ResourceType::Gold)] = parse_int(f[12], 0);
            d.cost[static_cast<int>(ResourceType::Stone)] = parse_int(f[13], 0);
            d.requires = f[14];
            d.dropoff = parse_bool(f[15]);
            d.research_time = parse_float(f[16], 15.0f);
            d.research_gold = parse_int(f[17], 50);
            for (int id : split_int_list(f[18])) if (id >= 0) d.produces.push_back(id);
            for (auto& tech : split_tech_names(f[19])) if (!tech.empty()) d.research.push_back(tech);
        }
        else if (f.size() >= 19) {
            // 版本 2
            d.type_id = parse_int(f[1], 0);
            d.name = f[2];
            d.hp = parse_int(f[4], 1000);
            int old_armor = parse_int(f[6], 0);
            for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
                d.defense[k] = old_armor;
            d.size_cells = parse_int(f[7], 20);
            d.build_time = parse_float(f[8], 5.0f);
            d.cost[static_cast<int>(ResourceType::Wood)] = parse_int(f[9], 0);
            d.cost[static_cast<int>(ResourceType::Food)] = parse_int(f[10], 0);
            d.cost[static_cast<int>(ResourceType::Gold)] = parse_int(f[11], 0);
            d.cost[static_cast<int>(ResourceType::Stone)] = parse_int(f[12], 0);
            d.requires = f[13];
            d.dropoff = parse_bool(f[14]);
            d.research_time = parse_float(f[15], 15.0f);
            d.research_gold = parse_int(f[16], 50);
            for (int id : split_int_list(f[17])) if (id >= 0) d.produces.push_back(id);
            for (auto& tech : split_tech_names(f[18])) if (!tech.empty()) d.research.push_back(tech);
        }
        else {
            return false;
        }

        if (d.name.empty()) d.name = u8"建筑";
        d.hp = std::clamp(d.hp, 1, 999999);
        for (int k = 0; k < static_cast<int>(DamageType::Count); ++k)
            d.defense[k] = std::clamp(d.defense[k], 0, 9999);
        d.size_cells = std::clamp(d.size_cells, 1, 80);
        d.build_time = std::clamp(d.build_time, 0.1f, 600.0f);
        d.research_time = std::clamp(d.research_time, 0.1f, 600.0f);
        d.research_gold = std::clamp(d.research_gold, 0, 999999);
        for (int r = 1; r < static_cast<int>(ResourceType::Count); ++r)
            d.cost[r] = std::clamp(d.cost[r], 0, 999999);

        if (d.type_id < 0 || used_building_ids.count(d.type_id)) {
            int candidate = CUSTOM_TYPE_ID_BASE;
            while (used_building_ids.count(candidate)) ++candidate;
            d.type_id = candidate;
        }
        used_building_ids.insert(d.type_id);
        new_buildings.push_back(std::move(d));
    }

    // 清理不存在的单位引用
    for (auto& b : new_buildings) {
        b.produces.erase(std::remove_if(b.produces.begin(), b.produces.end(),
            [&](int id) {
                if (id < CUSTOM_TYPE_ID_BASE) return !is_builtin_playable_unit((UnitEntityType)id);
                return std::none_of(new_units.begin(), new_units.end(),
                    [id](const UnitDef& u) { return u.type_id == id; });
            }), b.produces.end());
    }

    // 旧文件 / 手写文件容错：凡是 requires/research 引用到但 TECHS 里没有的科技，
    // 自动补一个默认科技，保证前置科技一定能从编辑器里看到并编辑。
    std::set<std::string> tech_names;
    for (const auto& t : new_techs) tech_names.insert(t.name);
    auto ensure_tech = [&](const std::string& raw_name, float time, int gold) {
        std::string name = sanitize_tech_name(raw_name);
        if (name.empty() || tech_names.count(name)) return;
        TechDef t;
        t.name = name;
        t.research_time = std::clamp(time, 0.1f, 600.0f);
        t.cost[static_cast<int>(ResourceType::Gold)] = std::clamp(gold, 0, 999999);
        new_techs.push_back(std::move(t));
        tech_names.insert(name);
    };
    for (const auto& u : new_units) {
        for (const auto& name : split_tech_names(u.requires)) ensure_tech(name, 15.0f, 50);
    }
    for (const auto& b : new_buildings) {
        for (const auto& name : split_tech_names(b.requires)) ensure_tech(name, b.research_time, b.research_gold);
        for (const auto& name : b.research) ensure_tech(name, b.research_time, b.research_gold);
    }

    units_ = std::move(new_units);
    buildings_ = std::move(new_buildings);
    techs_ = std::move(new_techs);

    // 旧的自定义-only 文件没有内置定义，这里补齐。
    ensure_builtin_defaults();

    // 清理指向已删除兵种的科技效果
    for (auto& t : techs_) {
        t.effects.erase(std::remove_if(t.effects.begin(), t.effects.end(),
            [&](const TechEffect& e) {
                if (e.target_type < 0) return false;
                return find_unit(e.target_type) == nullptr;
            }), t.effects.end());
    }

    next_unit_serial_ = 0;
    for (const auto& u : units_)
        next_unit_serial_ = std::max(next_unit_serial_, u.type_id - CUSTOM_TYPE_ID_BASE + 1);
    next_building_serial_ = 0;
    for (const auto& b : buildings_)
        next_building_serial_ = std::max(next_building_serial_, b.type_id - CUSTOM_TYPE_ID_BASE + 1);
    next_tech_serial_ = 0;

    touch();
    return true;
}

void EntityCatalog::load_default_if_exists() {
    std::error_code ec;
    const char* candidates[] = { "defs/custom_defs.srdef", "custom_defs.srdef" };
    for (const char* path : candidates) {
        if (!std::filesystem::exists(path, ec)) continue;
        if (load_from_file(path)) {
            std::printf("[EntityCatalog] loaded %s (%zu units, %zu buildings, %zu techs)\n",
                path, units_.size(), buildings_.size(), techs_.size());
            return;
        }
        std::printf("[EntityCatalog] failed to load %s\n", path);
    }
}

// ==================== 查询辅助 ====================

bool is_builtin_playable_unit(UnitEntityType type) {
    switch (type) {
    case UnitEntityType::Villager:
    case UnitEntityType::Archer:
    case UnitEntityType::Crossbowman:
        return true;
    default:
        return false;
    }
}

std::vector<UnitTypeChoice> list_unit_type_choices() {
    std::vector<UnitTypeChoice> out;
    for (const auto& u : EntityCatalog::instance()->units()) {
        out.push_back({ static_cast<UnitEntityType>(u.type_id), u.name, u.builtin() });
    }
    return out;
}

std::vector<std::string> split_tech_names(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::string t = trim(cur);
        cur.clear();
        if (t.empty()) return;
        for (auto& existing : out)
            if (existing == t) return;
        out.push_back(t);
    };
    for (char c : csv) {
        if (c == ',') flush();
        else cur.push_back(c);
    }
    flush();

    // 中文逗号：EF BC 8C
    std::vector<std::string> result;
    for (auto& item : out) {
        std::string token;
        for (size_t i = 0; i < item.size(); ) {
            if (i + 2 < item.size() &&
                (unsigned char)item[i] == 0xEF &&
                (unsigned char)item[i + 1] == 0xBC &&
                (unsigned char)item[i + 2] == 0x8C) {
                std::string t = trim(token);
                if (!t.empty()) result.push_back(t);
                token.clear();
                i += 3;
            }
            else {
                token.push_back(item[i]);
                ++i;
            }
        }
        std::string t = trim(token);
        if (!t.empty()) result.push_back(t);
    }

    std::vector<std::string> unique;
    for (auto& t : result) {
        bool dup = false;
        for (auto& u : unique) if (u == t) { dup = true; break; }
        if (!dup) unique.push_back(t);
    }
    return unique;
}

std::string join_tech_names(const std::vector<std::string>& names) {
    return join_comma(names);
}

bool tech_csv_has(const std::string& csv, const std::string& name) {
    for (const auto& n : split_tech_names(csv)) if (n == name) return true;
    return false;
}

std::string tech_csv_toggle(const std::string& csv, const std::string& name, bool enabled) {
    auto names = split_tech_names(csv);
    auto it = std::find(names.begin(), names.end(), name);
    if (enabled) {
        if (it == names.end()) names.push_back(name);
    }
    else {
        if (it != names.end()) names.erase(it);
    }
    return join_tech_names(names);
}

std::string sanitize_tech_name(const std::string& name) {
    std::string raw;
    for (size_t i = 0; i < name.size(); ) {
        if (i + 2 < name.size() &&
            (unsigned char)name[i] == 0xEF &&
            (unsigned char)name[i + 1] == 0xBC &&
            (unsigned char)name[i + 2] == 0x8C) {
            raw.push_back(' ');
            i += 3;
            continue;
        }
        char c = name[i++];
        if (c == ',' || c == ';' || c == '\t' || c == '\r' || c == '\n')
            raw.push_back(' ');
        else
            raw.push_back(c);
    }

    std::string out;
    bool last_space = false;
    for (char c : raw) {
        if (c == ' ') {
            if (!last_space) out.push_back(c);
            last_space = true;
        }
        else {
            out.push_back(c);
            last_space = false;
        }
    }
    return trim(out);
}
