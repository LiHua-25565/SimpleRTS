#include "def_editor_scene.h"

#include "scene_mgr.h"
#include "scene_ui.h"
#include "texture_cache.h"
#include "color.h"
#include "factories.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace {

std::string trim_text(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string fmt_float(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s(buf);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool point_in_rect(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void erase_last_utf8(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
    s.erase(i);
}

int utf8_length(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else i += 4;
        ++n;
    }
    return n;
}

std::string limit_utf8(const std::string& s, int max_chars) {
    int n = 0;
    size_t i = 0;
    while (i < s.size() && n < max_chars) {
        unsigned char c = s[i];
        if (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else i += 4;
        ++n;
    }
    if (i > s.size()) i = s.size();
    return s.substr(0, i);
}

bool is_ascii_safe_path_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

} // namespace

// ==================== 基础访问 ====================

EntityCatalog* DefEditorScene::catalog() {
    return EntityCatalog::instance();
}

UnitDef* DefEditorScene::current_unit() {
    return catalog()->find_unit(selected_unit_id_);
}

BuildingDef* DefEditorScene::current_building() {
    return catalog()->find_building(selected_building_id_);
}

TechDef* DefEditorScene::current_tech() {
    return catalog()->find_tech(selected_tech_name_);
}

bool DefEditorScene::has_selection() const {
    if (category_ == Category::Units)
        return EntityCatalog::instance()->find_unit(selected_unit_id_) != nullptr;
    if (category_ == Category::Buildings)
        return EntityCatalog::instance()->find_building(selected_building_id_) != nullptr;
    return EntityCatalog::instance()->find_tech(selected_tech_name_) != nullptr;
}

int DefEditorScene::selected_id() const {
    return category_ == Category::Units ? selected_unit_id_ : selected_building_id_;
}

std::string DefEditorScene::selected_name() const {
    auto* self = const_cast<DefEditorScene*>(this);
    if (category_ == Category::Units) {
        if (auto* d = self->current_unit()) return d->name;
    }
    else if (category_ == Category::Buildings) {
        if (auto* d = self->current_building()) return d->name;
    }
    else {
        if (auto* d = self->current_tech()) return d->name;
    }
    return "";
}

void DefEditorScene::set_status(const std::string& msg) {
    status_ = msg;
    status_timer_ = 4.0f;
}

// ==================== 生命周期 ====================

void DefEditorScene::on_enter() {
    layout();
    focus_ = Field::None;
    edit_buf_.clear();
    editing_effect_ = -1;
    delete_confirm_id_ = 0;
    delete_confirm_tech_.clear();
    status_.clear();
    status_timer_ = 0.0f;
    list_scroll_ = 0.0f;
    produce_scroll_ = 0.0f;
    require_scroll_ = 0.0f;
    research_scroll_ = 0.0f;
    effect_scroll_ = 0.0f;

    ensure_selection();
    rebuild_fields();

    SDL_Window* win = SDL_GetRenderWindow(renderer);
    if (win) SDL_StartTextInput(win);
}

void DefEditorScene::on_exit() {
    commit_edit();
    SDL_Window* win = SDL_GetRenderWindow(renderer);
    if (win) SDL_StopTextInput(win);
}

void DefEditorScene::on_update(float delta) {
    if (status_timer_ > 0.0f) {
        status_timer_ -= delta;
        if (status_timer_ <= 0.0f) {
            status_timer_ = 0.0f;
            status_.clear();
        }
    }
}

// ==================== 布局 ====================

void DefEditorScene::layout() {
    file_rect_ = { 270.0f, 16.0f, 170.0f, 30.0f };
    btn_save_ = { 450.0f, 16.0f, 72.0f, 30.0f };
    btn_load_ = { 530.0f, 16.0f, 72.0f, 30.0f };
    btn_back_ = { 1130.0f, 15.0f, 130.0f, 32.0f };

    tab_units_ = { 8.0f, 70.0f, 92.0f, 32.0f };
    tab_buildings_ = { 104.0f, 70.0f, 92.0f, 32.0f };
    tab_techs_ = { 200.0f, 70.0f, 92.0f, 32.0f };
    btn_new_ = { 8.0f, 112.0f, 136.0f, 30.0f };
    btn_delete_ = { 152.0f, 112.0f, 136.0f, 30.0f };
    list_rect_ = { 8.0f, LIST_TOP, 284.0f, LIST_BOTTOM - LIST_TOP };

    form_rect_ = { 304.0f, 72.0f, 968.0f, 620.0f };
    preview_rect_ = { 1120.0f, 90.0f, 130.0f, 130.0f };

    damage_type_btn_ = { 800.0f, 514.0f, 200.0f, 30.0f };
    attack_toggle_ = { 320.0f, 444.0f, 110.0f, 30.0f };
    ranged_toggle_ = { 800.0f, 444.0f, 110.0f, 30.0f };
    gather_toggle_ = { 920.0f, 444.0f, 110.0f, 30.0f };
    dropoff_toggle_ = { 1000.0f, 194.0f, 120.0f, 30.0f };

    requires_rect_ = { 320.0f, 324.0f, 900.0f, 100.0f };
    research_rect_ = { 320.0f, 454.0f, 420.0f, 220.0f };
    produce_rect_ = { 760.0f, 454.0f, 460.0f, 220.0f };
    effect_add_btn_ = { 320.0f, 272.0f, 140.0f, 30.0f };
    effect_list_rect_ = { 320.0f, 312.0f, 900.0f, 300.0f };
}

void DefEditorScene::rebuild_fields() {
    fields_.clear();
    if (category_ == Category::Units) {
        fields_.push_back({ Field::Name,          { 320.0f, 124.0f, 360.0f, 30.0f }, u8"名字" });
        fields_.push_back({ Field::Texture,       { 700.0f, 124.0f, 410.0f, 30.0f }, u8"纹理文字（空 = 名字，最多 4 字）" });
        fields_.push_back({ Field::Hp,            { 320.0f, 194.0f, 110.0f, 30.0f }, u8"生命值" });
        fields_.push_back({ Field::Speed,         { 450.0f, 194.0f, 110.0f, 30.0f }, u8"速度" });
        fields_.push_back({ Field::ProduceTime,   { 580.0f, 194.0f, 110.0f, 30.0f }, u8"生产时间(秒)" });
        fields_.push_back({ Field::DefenseNormal, { 320.0f, 264.0f, 110.0f, 30.0f }, u8"防御·普通" });
        fields_.push_back({ Field::DefensePierce, { 440.0f, 264.0f, 110.0f, 30.0f }, u8"防御·穿刺" });
        fields_.push_back({ Field::DefenseSiege,  { 560.0f, 264.0f, 110.0f, 30.0f }, u8"防御·攻城" });
        fields_.push_back({ Field::CostWood,      { 700.0f, 264.0f, 80.0f, 30.0f }, u8"木" });
        fields_.push_back({ Field::CostFood,      { 790.0f, 264.0f, 80.0f, 30.0f }, u8"肉" });
        fields_.push_back({ Field::CostGold,      { 880.0f, 264.0f, 80.0f, 30.0f }, u8"金" });
        fields_.push_back({ Field::CostStone,     { 970.0f, 264.0f, 80.0f, 30.0f }, u8"石" });
        fields_.push_back({ Field::AttackDamage,  { 440.0f, 444.0f, 110.0f, 30.0f }, u8"攻击力" });
        fields_.push_back({ Field::AttackRange,   { 560.0f, 444.0f, 110.0f, 30.0f }, u8"攻击距离" });
        fields_.push_back({ Field::AttackInterval,{ 680.0f, 444.0f, 110.0f, 30.0f }, u8"攻击间隔(秒)" });
        fields_.push_back({ Field::GatherAmount,  { 320.0f, 514.0f, 110.0f, 30.0f }, u8"采集量" });
        fields_.push_back({ Field::GatherInterval,{ 440.0f, 514.0f, 110.0f, 30.0f }, u8"采集间隔(秒)" });
        fields_.push_back({ Field::CarryCapacity, { 560.0f, 514.0f, 110.0f, 30.0f }, u8"携带上限" });
    }
    else if (category_ == Category::Buildings) {
        fields_.push_back({ Field::Name,          { 320.0f, 124.0f, 360.0f, 30.0f }, u8"名字" });
        fields_.push_back({ Field::Texture,       { 700.0f, 124.0f, 410.0f, 30.0f }, u8"纹理文字（空 = 名字，最多 4 字）" });
        fields_.push_back({ Field::Hp,            { 320.0f, 194.0f, 110.0f, 30.0f }, u8"生命值" });
        fields_.push_back({ Field::SizeCells,     { 450.0f, 194.0f, 110.0f, 30.0f }, u8"占地格数" });
        fields_.push_back({ Field::BuildTime,     { 580.0f, 194.0f, 110.0f, 30.0f }, u8"建造时间(秒)" });
        fields_.push_back({ Field::DefenseNormal, { 320.0f, 264.0f, 110.0f, 30.0f }, u8"防御·普通" });
        fields_.push_back({ Field::DefensePierce, { 440.0f, 264.0f, 110.0f, 30.0f }, u8"防御·穿刺" });
        fields_.push_back({ Field::DefenseSiege,  { 560.0f, 264.0f, 110.0f, 30.0f }, u8"防御·攻城" });
        fields_.push_back({ Field::CostWood,      { 700.0f, 264.0f, 80.0f, 30.0f }, u8"木" });
        fields_.push_back({ Field::CostFood,      { 790.0f, 264.0f, 80.0f, 30.0f }, u8"肉" });
        fields_.push_back({ Field::CostGold,      { 880.0f, 264.0f, 80.0f, 30.0f }, u8"金" });
        fields_.push_back({ Field::CostStone,     { 970.0f, 264.0f, 80.0f, 30.0f }, u8"石" });
    }
    else {
        fields_.push_back({ Field::Name,         { 320.0f, 124.0f, 400.0f, 30.0f }, u8"科技名字" });
        fields_.push_back({ Field::ResearchTime, { 760.0f, 124.0f, 120.0f, 30.0f }, u8"研究时间(秒)" });
        fields_.push_back({ Field::CostWood,     { 320.0f, 194.0f, 100.0f, 30.0f }, u8"木" });
        fields_.push_back({ Field::CostFood,     { 440.0f, 194.0f, 100.0f, 30.0f }, u8"肉" });
        fields_.push_back({ Field::CostGold,     { 560.0f, 194.0f, 100.0f, 30.0f }, u8"金" });
        fields_.push_back({ Field::CostStone,    { 680.0f, 194.0f, 100.0f, 30.0f }, u8"石" });
    }
}

const DefEditorScene::FieldView* DefEditorScene::find_field_view(Field f) const {
    for (const auto& v : fields_) if (v.field == f) return &v;
    return nullptr;
}

bool DefEditorScene::point_in(const SDL_FRect& r, float x, float y) const {
    return point_in_rect(r, x, y);
}

// ==================== 字段读写 ====================

bool DefEditorScene::field_is_float(Field f) const {
    switch (f) {
    case Field::Speed:
    case Field::ProduceTime:
    case Field::BuildTime:
    case Field::ResearchTime:
    case Field::AttackRange:
    case Field::AttackInterval:
    case Field::GatherInterval:
        return true;
    default:
        return false;
    }
}

bool DefEditorScene::field_is_integer(Field f) const {
    switch (f) {
    case Field::Hp:
    case Field::DefenseNormal:
    case Field::DefensePierce:
    case Field::DefenseSiege:
    case Field::CostWood:
    case Field::CostFood:
    case Field::CostGold:
    case Field::CostStone:
    case Field::SizeCells:
    case Field::AttackDamage:
    case Field::GatherAmount:
    case Field::CarryCapacity:
        return true;
    default:
        return false;
    }
}

bool DefEditorScene::field_is_text(Field f) const {
    return f == Field::FileName || f == Field::Name;
}

std::string DefEditorScene::field_text(Field f) const {
    if (f == Field::FileName) return file_name_;

    auto* self = const_cast<DefEditorScene*>(this);
    if (category_ == Category::Units) {
        UnitDef* d = self->current_unit();
        if (!d) return "";
        switch (f) {
        case Field::Name: return d->name;
        case Field::Texture: return d->texture_text;
        case Field::Hp: return std::to_string(d->hp);
        case Field::DefenseNormal: return std::to_string(d->defense[static_cast<int>(DamageType::Normal)]);
        case Field::DefensePierce: return std::to_string(d->defense[static_cast<int>(DamageType::Pierce)]);
        case Field::DefenseSiege: return std::to_string(d->defense[static_cast<int>(DamageType::Siege)]);
        case Field::Speed: return fmt_float(d->speed);
        case Field::CostWood: return std::to_string(d->cost[static_cast<int>(ResourceType::Wood)]);
        case Field::CostFood: return std::to_string(d->cost[static_cast<int>(ResourceType::Food)]);
        case Field::CostGold: return std::to_string(d->cost[static_cast<int>(ResourceType::Gold)]);
        case Field::CostStone: return std::to_string(d->cost[static_cast<int>(ResourceType::Stone)]);
        case Field::ProduceTime: return fmt_float(d->produce_time);
        case Field::AttackDamage: return std::to_string(d->attack_damage);
        case Field::AttackRange: return fmt_float(d->attack_range);
        case Field::AttackInterval: return fmt_float(d->attack_interval);
        case Field::GatherAmount: return std::to_string(d->gather_amount);
        case Field::GatherInterval: return fmt_float(d->gather_interval);
        case Field::CarryCapacity: return std::to_string(d->carry_capacity);
        default: return "";
        }
    }

    if (category_ == Category::Buildings) {
        BuildingDef* b = self->current_building();
        if (!b) return "";
        switch (f) {
        case Field::Name: return b->name;
        case Field::Texture: return b->texture_text;
        case Field::Hp: return std::to_string(b->hp);
        case Field::DefenseNormal: return std::to_string(b->defense[static_cast<int>(DamageType::Normal)]);
        case Field::DefensePierce: return std::to_string(b->defense[static_cast<int>(DamageType::Pierce)]);
        case Field::DefenseSiege: return std::to_string(b->defense[static_cast<int>(DamageType::Siege)]);
        case Field::SizeCells: return std::to_string(b->size_cells);
        case Field::BuildTime: return fmt_float(b->build_time);
        case Field::CostWood: return std::to_string(b->cost[static_cast<int>(ResourceType::Wood)]);
        case Field::CostFood: return std::to_string(b->cost[static_cast<int>(ResourceType::Food)]);
        case Field::CostGold: return std::to_string(b->cost[static_cast<int>(ResourceType::Gold)]);
        case Field::CostStone: return std::to_string(b->cost[static_cast<int>(ResourceType::Stone)]);
        default: return "";
        }
    }

    TechDef* t = self->current_tech();
    if (!t) return "";
    switch (f) {
    case Field::Name: return t->name;
    case Field::ResearchTime: return fmt_float(t->research_time);
    case Field::CostWood: return std::to_string(t->cost[static_cast<int>(ResourceType::Wood)]);
    case Field::CostFood: return std::to_string(t->cost[static_cast<int>(ResourceType::Food)]);
    case Field::CostGold: return std::to_string(t->cost[static_cast<int>(ResourceType::Gold)]);
    case Field::CostStone: return std::to_string(t->cost[static_cast<int>(ResourceType::Stone)]);
    default: return "";
    }
}

void DefEditorScene::begin_edit(Field f) {
    if (f == Field::None) return;
    if (focus_ == f) return;
    commit_edit();
    focus_ = f;
    edit_buf_ = field_text(f);
    edit_replace_on_type_ = true;
}

void DefEditorScene::cancel_edit() {
    focus_ = Field::None;
    editing_effect_ = -1;
    edit_buf_.clear();
    edit_replace_on_type_ = false;
}

void DefEditorScene::apply_field(Field f, const std::string& text) {
    auto* cat = catalog();

    if (f == Field::FileName) {
        std::string name;
        for (char c : text) if (is_ascii_safe_path_char(c)) name.push_back(c);
        if (name.empty()) name = "custom_defs";
        if (name.size() > 40) name.resize(40);
        file_name_ = name;
        return;
    }

    auto clamp_int = [](int v, int lo, int hi) { return std::clamp(v, lo, hi); };
    auto clamp_float = [](float v, float lo, float hi) { return std::clamp(v, lo, hi); };
    auto parse_int_or = [&](int fallback) {
        try { return std::stoi(text); }
        catch (...) { set_status(u8"请输入整数"); return fallback; }
    };
    auto parse_float_or = [&](float fallback) {
        try { return std::stof(text); }
        catch (...) { set_status(u8"请输入数字"); return fallback; }
    };

    if (category_ == Category::Units) {
        UnitDef* d = current_unit();
        if (!d) return;
        switch (f) {
        case Field::Name: {
            std::string n = limit_utf8(trim_text(text), 16);
            d->name = n.empty() ? u8"兵种" : n;
            break;
        }
        case Field::Texture: d->texture_text = limit_utf8(trim_text(text), 4); break;
        case Field::Hp: d->hp = clamp_int(parse_int_or(d->hp), 1, 999999); break;
        case Field::DefenseNormal: d->defense[static_cast<int>(DamageType::Normal)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::DefensePierce: d->defense[static_cast<int>(DamageType::Pierce)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::DefenseSiege: d->defense[static_cast<int>(DamageType::Siege)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::Speed: d->speed = clamp_float(parse_float_or(d->speed), 1.0f, 2000.0f); break;
        case Field::CostWood: d->cost[static_cast<int>(ResourceType::Wood)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostFood: d->cost[static_cast<int>(ResourceType::Food)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostGold: d->cost[static_cast<int>(ResourceType::Gold)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostStone: d->cost[static_cast<int>(ResourceType::Stone)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::ProduceTime: d->produce_time = clamp_float(parse_float_or(d->produce_time), 0.1f, 600.0f); break;
        case Field::AttackDamage: d->attack_damage = clamp_int(parse_int_or(d->attack_damage), 0, 99999); break;
        case Field::AttackRange: d->attack_range = clamp_float(parse_float_or(d->attack_range), 0.0f, 5000.0f); break;
        case Field::AttackInterval: d->attack_interval = clamp_float(parse_float_or(d->attack_interval), 0.05f, 60.0f); break;
        case Field::GatherAmount: d->gather_amount = clamp_int(parse_int_or(d->gather_amount), 0, 99999); break;
        case Field::GatherInterval: d->gather_interval = clamp_float(parse_float_or(d->gather_interval), 0.05f, 60.0f); break;
        case Field::CarryCapacity: d->carry_capacity = clamp_int(parse_int_or(d->carry_capacity), 1, 99999); break;
        default: return;
        }
    }
    else if (category_ == Category::Buildings) {
        BuildingDef* b = current_building();
        if (!b) return;
        switch (f) {
        case Field::Name: {
            std::string n = limit_utf8(trim_text(text), 16);
            b->name = n.empty() ? u8"建筑" : n;
            break;
        }
        case Field::Texture: b->texture_text = limit_utf8(trim_text(text), 4); break;
        case Field::Hp: b->hp = clamp_int(parse_int_or(b->hp), 1, 999999); break;
        case Field::DefenseNormal: b->defense[static_cast<int>(DamageType::Normal)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::DefensePierce: b->defense[static_cast<int>(DamageType::Pierce)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::DefenseSiege: b->defense[static_cast<int>(DamageType::Siege)] = clamp_int(parse_int_or(0), 0, 9999); break;
        case Field::SizeCells: b->size_cells = clamp_int(parse_int_or(b->size_cells), 1, 80); break;
        case Field::BuildTime: b->build_time = clamp_float(parse_float_or(b->build_time), 0.1f, 600.0f); break;
        case Field::CostWood: b->cost[static_cast<int>(ResourceType::Wood)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostFood: b->cost[static_cast<int>(ResourceType::Food)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostGold: b->cost[static_cast<int>(ResourceType::Gold)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostStone: b->cost[static_cast<int>(ResourceType::Stone)] = clamp_int(parse_int_or(0), 0, 999999); break;
        default: return;
        }
    }
    else {
        TechDef* t = current_tech();
        if (!t) return;
        switch (f) {
        case Field::Name: {
            std::string n = sanitize_tech_name(limit_utf8(trim_text(text), 16));
            if (n.empty()) return;
            if (n != t->name) {
                if (!catalog()->rename_tech(t->name, n)) {
                    set_status(u8"科技名已存在，换一个名字");
                    return;
                }
                selected_tech_name_ = n;
            }
            break;
        }
        case Field::ResearchTime: t->research_time = clamp_float(parse_float_or(t->research_time), 0.1f, 600.0f); break;
        case Field::CostWood: t->cost[static_cast<int>(ResourceType::Wood)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostFood: t->cost[static_cast<int>(ResourceType::Food)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostGold: t->cost[static_cast<int>(ResourceType::Gold)] = clamp_int(parse_int_or(0), 0, 999999); break;
        case Field::CostStone: t->cost[static_cast<int>(ResourceType::Stone)] = clamp_int(parse_int_or(0), 0, 999999); break;
        default: return;
        }
    }

    cat->touch();
}

void DefEditorScene::commit_edit() {
    if (editing_effect_ >= 0) {
        int idx = editing_effect_;
        std::string text = edit_buf_;
        editing_effect_ = -1;
        edit_buf_.clear();
        edit_replace_on_type_ = false;
        apply_effect_edit(idx, text);
        return;
    }
    if (focus_ == Field::None) return;
    Field f = focus_;
    std::string text = edit_buf_;
    focus_ = Field::None;
    edit_buf_.clear();
    edit_replace_on_type_ = false;
    apply_field(f, text);
}

// ==================== 选择 / 新增 / 删除 ====================

void DefEditorScene::ensure_selection() {
    if (category_ == Category::Units) {
        if (!current_unit()) {
            selected_unit_id_ = catalog()->units().empty() ? 0 : catalog()->units().front().type_id;
        }
    }
    else if (category_ == Category::Buildings) {
        if (!current_building()) {
            selected_building_id_ = catalog()->buildings().empty() ? 0 : catalog()->buildings().front().type_id;
        }
    }
    else {
        if (!current_tech()) {
            selected_tech_name_ = catalog()->techs().empty() ? "" : catalog()->techs().front().name;
        }
    }
}

void DefEditorScene::select_category(Category c) {
    if (category_ == c) return;
    commit_edit();
    category_ = c;
    list_scroll_ = 0.0f;
    produce_scroll_ = 0.0f;
    require_scroll_ = 0.0f;
    research_scroll_ = 0.0f;
    delete_confirm_id_ = 0;
    delete_confirm_tech_.clear();
    ensure_selection();
    rebuild_fields();
}

void DefEditorScene::select_unit(int id) {
    commit_edit();
    selected_unit_id_ = id;
    delete_confirm_id_ = 0;
}

void DefEditorScene::select_building(int id) {
    commit_edit();
    selected_building_id_ = id;
    delete_confirm_id_ = 0;
}

void DefEditorScene::select_tech(const std::string& name) {
    commit_edit();
    selected_tech_name_ = name;
    delete_confirm_tech_.clear();
}

void DefEditorScene::do_new() {
    commit_edit();
    if (category_ == Category::Units) {
        UnitDef* d = catalog()->add_unit();
        selected_unit_id_ = d ? d->type_id : 0;
        set_status(u8"已新建兵种，编辑后记得导出");
    }
    else if (category_ == Category::Buildings) {
        BuildingDef* d = catalog()->add_building();
        selected_building_id_ = d ? d->type_id : 0;
        set_status(u8"已新建建筑，可在下方勾选可生产单位");
    }
    else {
        TechDef* t = catalog()->add_tech();
        selected_tech_name_ = t ? t->name : "";
        set_status(u8"已新建科技，可在兵种/建筑的前置科技里勾选");
    }
    delete_confirm_id_ = 0;
    delete_confirm_tech_.clear();
}

void DefEditorScene::do_delete() {
    commit_edit();

    if (category_ == Category::Techs) {
        if (selected_tech_name_.empty()) return;
        if (delete_confirm_tech_ != selected_tech_name_) {
            delete_confirm_tech_ = selected_tech_name_;
            set_status(u8"再次点击「删除」确认删除科技（会同时清除所有引用）");
            return;
        }
        catalog()->remove_tech(selected_tech_name_);
        selected_tech_name_.clear();
        delete_confirm_tech_.clear();
        ensure_selection();
        rebuild_fields();
        set_status(u8"已删除科技");
        return;
    }

    int id = selected_id();
    if (id < 0) return;
    if (id < CUSTOM_TYPE_ID_BASE) {
        set_status(u8"内置兵种/建筑不可删除，可以直接编辑属性");
        return;
    }
    if (delete_confirm_id_ != id) {
        delete_confirm_id_ = id;
        set_status(u8"再次点击「删除」确认删除");
        return;
    }

    if (category_ == Category::Units) {
        catalog()->remove_unit(id);
        selected_unit_id_ = 0;
    }
    else {
        catalog()->remove_building(id);
        selected_building_id_ = 0;
    }
    delete_confirm_id_ = 0;
    ensure_selection();
    rebuild_fields();
    set_status(u8"已删除（未导出前仅当前运行期生效）");
}

// ==================== 导入 / 导出 ====================

void DefEditorScene::do_save() {
    commit_edit();
    try {
        std::filesystem::create_directories("defs");
        std::string path = (std::filesystem::path("defs") / (file_name_ + ".srdef")).string();
        if (catalog()->save_to_file(path)) {
            set_status(u8"已导出: " + path);
        }
        else {
            set_status(u8"导出失败: " + path);
        }
    }
    catch (...) {
        set_status(u8"导出失败");
    }
}

void DefEditorScene::do_load() {
    commit_edit();
    try {
        std::string path = (std::filesystem::path("defs") / (file_name_ + ".srdef")).string();
        if (!catalog()->load_from_file(path)) {
            set_status(u8"导入失败: " + path + u8" 不存在或格式不符");
            return;
        }
        TextureCache::instance()->clear_entity_caches();
        selected_unit_id_ = 0;
        selected_building_id_ = 0;
        selected_tech_name_.clear();
        delete_confirm_id_ = 0;
        delete_confirm_tech_.clear();
        list_scroll_ = 0.0f;
        produce_scroll_ = 0.0f;
        require_scroll_ = 0.0f;
        research_scroll_ = 0.0f;
        ensure_selection();
        set_status(u8"已导入: " + path + u8"（兵种 " + std::to_string(catalog()->units().size()) +
                   u8"，建筑 " + std::to_string(catalog()->buildings().size()) +
                   u8"，科技 " + std::to_string(catalog()->techs().size()) + u8"）");
    }
    catch (...) {
        set_status(u8"导入失败");
    }
}

void DefEditorScene::do_back() {
    commit_edit();
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
}

// ==================== 点击处理 ====================

void DefEditorScene::toggle_damage_type() {
    if (category_ != Category::Units) return;
    if (auto* d = current_unit()) {
        int next = (static_cast<int>(d->damage_type) + 1) % static_cast<int>(DamageType::Count);
        d->damage_type = static_cast<DamageType>(next);
        catalog()->touch();
    }
}

void DefEditorScene::toggle_plugin(uint32_t flag) {
    if (category_ != Category::Units) return;
    if (auto* d = current_unit()) {
        d->plugins ^= flag;
        catalog()->touch();
    }
}

void DefEditorScene::toggle_bool(bool& value) {
    value = !value;
    catalog()->touch();
}

void DefEditorScene::toggle_produce(int unit_type_id) {
    BuildingDef* b = current_building();
    if (!b) return;
    auto it = std::find(b->produces.begin(), b->produces.end(), unit_type_id);
    if (it != b->produces.end()) b->produces.erase(it);
    else b->produces.push_back(unit_type_id);
    catalog()->touch();
}

void DefEditorScene::toggle_required_tech(const std::string& tech_name) {
    if (category_ == Category::Units) {
        if (auto* d = current_unit()) {
            bool has = tech_csv_has(d->requires, tech_name);
            d->requires = tech_csv_toggle(d->requires, tech_name, !has);
            catalog()->touch();
        }
    }
    else if (category_ == Category::Buildings) {
        if (auto* d = current_building()) {
            bool has = tech_csv_has(d->requires, tech_name);
            d->requires = tech_csv_toggle(d->requires, tech_name, !has);
            catalog()->touch();
        }
    }
}

void DefEditorScene::toggle_research_tech(const std::string& tech_name) {
    BuildingDef* b = current_building();
    if (!b) return;
    auto it = std::find(b->research.begin(), b->research.end(), tech_name);
    if (it != b->research.end()) b->research.erase(it);
    else b->research.push_back(tech_name);
    catalog()->touch();
}

// ==================== 科技效果 ====================

SDL_FRect DefEditorScene::effect_value_rect(int index) const {
    return SDL_FRect{ effect_list_rect_.x + 440.0f,
                      effect_list_rect_.y + 8.0f + (float)index * 38.0f - effect_scroll_,
                      90.0f, 30.0f };
}

void DefEditorScene::begin_effect_edit(int index) {
    if (editing_effect_ == index) return;
    TechDef* tech = current_tech();
    if (!tech || index < 0 || index >= (int)tech->effects.size()) return;
    commit_edit();
    editing_effect_ = index;
    edit_buf_ = fmt_float(tech->effects[index].value);
    edit_replace_on_type_ = true;
}

void DefEditorScene::apply_effect_edit(int index, const std::string& text) {
    TechDef* tech = current_tech();
    if (!tech || index < 0 || index >= (int)tech->effects.size()) return;
    float value = tech->effects[index].value;
    try { value = std::stof(text); }
    catch (...) { set_status(u8"效果数值请输入数字"); return; }
    tech->effects[index].value = std::clamp(value, -99999.0f, 99999.0f);
    catalog()->touch();
}

void DefEditorScene::add_tech_effect() {
    TechDef* tech = current_tech();
    if (!tech) return;
    TechEffect effect;
    effect.target_type = -1;
    effect.type = TechEffectType::AttackDamage;
    effect.value = 10.0f;
    tech->effects.push_back(effect);
    catalog()->touch();
}

void DefEditorScene::remove_tech_effect(int index) {
    TechDef* tech = current_tech();
    if (!tech || index < 0 || index >= (int)tech->effects.size()) return;
    tech->effects.erase(tech->effects.begin() + index);
    if (editing_effect_ == index) cancel_edit();
    else if (editing_effect_ > index) --editing_effect_;
    catalog()->touch();
}

void DefEditorScene::cycle_effect_target(TechEffect& effect) {
    auto choices = list_unit_type_choices();
    std::vector<int> targets;
    targets.push_back(-1);
    for (const auto& c : choices) targets.push_back(static_cast<int>(c.type));
    auto it = std::find(targets.begin(), targets.end(), effect.target_type);
    if (it == targets.end()) effect.target_type = -1;
    else effect.target_type = targets[(it - targets.begin() + 1) % targets.size()];
    catalog()->touch();
}

void DefEditorScene::cycle_effect_type(TechEffect& effect) {
    int next = (static_cast<int>(effect.type) + 1) % static_cast<int>(TechEffectType::Count);
    effect.type = static_cast<TechEffectType>(next);
    catalog()->touch();
}

void DefEditorScene::draw_tech_effects(TechDef& def) {
    ui_draw_text(renderer, u8"强化效果（类似强弓：选择目标兵种、效果类型和数值）", 15,
        { 190, 200, 220, 255 }, effect_list_rect_.x, effect_list_rect_.y - 58.0f, false);
    draw_button(effect_add_btn_, u8"+ 添加效果", false);

    ui_fill_rect(renderer, effect_list_rect_, { 24, 31, 52, 255 }, { 60, 76, 112, 255 }, 1);
    SDL_Rect clip{ (int)effect_list_rect_.x, (int)effect_list_rect_.y,
                   (int)effect_list_rect_.w, (int)effect_list_rect_.h };
    SDL_SetRenderClipRect(renderer, &clip);

    if (def.effects.empty()) {
        ui_draw_text(renderer, u8"无效果（只作为前置科技）。点「+ 添加效果」创建强化。", 15,
            { 150, 160, 180, 255 }, effect_list_rect_.x + 12.0f, effect_list_rect_.y + 12.0f, false);
    }

    for (size_t i = 0; i < def.effects.size(); ++i) {
        const TechEffect& e = def.effects[i];
        float row_y = effect_list_rect_.y + 8.0f + (float)i * 38.0f - effect_scroll_;
        if (row_y + 34.0f < effect_list_rect_.y || row_y > effect_list_rect_.y + effect_list_rect_.h)
            continue;

        SDL_FRect target_btn{ effect_list_rect_.x + 6.0f, row_y, 234.0f, 30.0f };
        SDL_FRect type_btn{ effect_list_rect_.x + 250.0f, row_y, 180.0f, 30.0f };
        SDL_FRect value_box{ effect_list_rect_.x + 440.0f, row_y, 90.0f, 30.0f };
        SDL_FRect remove_btn{ effect_list_rect_.x + 550.0f, row_y, 50.0f, 30.0f };

        std::string target_name = u8"全部兵种";
        if (e.target_type >= 0) {
            if (const UnitDef* ud = catalog()->find_unit(e.target_type)) target_name = ud->name;
            else target_name = u8"未知兵种";
        }
        draw_button(target_btn, target_name, false);
        draw_button(type_btn, tech_effect_type_name(e.type), false);

        bool focused = (editing_effect_ == (int)i);
        ui_fill_rect(renderer, value_box,
            focused ? SDL_Color{ 46, 60, 96, 255 } : SDL_Color{ 30, 38, 62, 255 },
            focused ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 }, 1);
        std::string value_text = focused ? (edit_buf_ + "_") : fmt_float(e.value);
        ui_draw_text_vcenter(renderer, value_text, 16, { 255, 255, 255, 255 },
            value_box.x + 8.0f, value_box.y + value_box.h * 0.5f);

        draw_button(remove_btn, u8"×", false, true);
    }

    SDL_SetRenderClipRect(renderer, nullptr);
}

void DefEditorScene::handle_click(float x, float y) {
    // 点击其他位置先提交当前编辑
    bool on_focus = false;
    if (focus_ == Field::FileName) on_focus = point_in(file_rect_, x, y);
    else if (const FieldView* fv = find_field_view(focus_)) on_focus = point_in(fv->rect, x, y);
    else if (editing_effect_ >= 0) on_focus = point_in(effect_value_rect(editing_effect_), x, y);
    if (!on_focus) commit_edit();

    // 顶栏
    if (point_in(file_rect_, x, y)) { begin_edit(Field::FileName); return; }
    if (point_in(btn_save_, x, y)) { do_save(); return; }
    if (point_in(btn_load_, x, y)) { do_load(); return; }
    if (point_in(btn_back_, x, y)) { do_back(); return; }

    // 左侧
    if (point_in(tab_units_, x, y)) { select_category(Category::Units); return; }
    if (point_in(tab_buildings_, x, y)) { select_category(Category::Buildings); return; }
    if (point_in(tab_techs_, x, y)) { select_category(Category::Techs); return; }
    if (point_in(btn_new_, x, y)) { do_new(); return; }
    if (point_in(btn_delete_, x, y)) { do_delete(); return; }

    if (point_in(list_rect_, x, y)) {
        float rel = y - list_rect_.y + list_scroll_;
        int idx = (int)(rel / LIST_ROW_H);
        if (idx >= 0) {
            if (category_ == Category::Units) {
                if (idx < (int)catalog()->units().size())
                    select_unit(catalog()->units()[idx].type_id);
            }
            else if (category_ == Category::Buildings) {
                if (idx < (int)catalog()->buildings().size())
                    select_building(catalog()->buildings()[idx].type_id);
            }
            else {
                if (idx < (int)catalog()->techs().size())
                    select_tech(catalog()->techs()[idx].name);
            }
        }
        return;
    }

    if (!has_selection()) return;

    // 表单字段
    for (const auto& fv : fields_) {
        if (point_in(fv.rect, x, y)) {
            begin_edit(fv.field);
            return;
        }
    }

    if (category_ == Category::Techs) {
        if (point_in(effect_add_btn_, x, y)) { add_tech_effect(); return; }
        if (point_in(effect_list_rect_, x, y)) {
            TechDef* tech = current_tech();
            if (tech) {
                const float row_h = 38.0f;
                float rel_y = y - effect_list_rect_.y + effect_scroll_;
                int idx = (int)(rel_y / row_h);
                if (idx >= 0 && idx < (int)tech->effects.size()) {
                    float row_y = effect_list_rect_.y + 8.0f + idx * row_h - effect_scroll_;
                    if (x >= effect_list_rect_.x + 550.0f) {
                        remove_tech_effect(idx);
                    }
                    else if (x >= effect_list_rect_.x + 440.0f && x < effect_list_rect_.x + 540.0f) {
                        begin_effect_edit(idx);
                    }
                    else if (x >= effect_list_rect_.x + 250.0f && x < effect_list_rect_.x + 430.0f) {
                        cycle_effect_type(tech->effects[idx]);
                    }
                    else if (x >= effect_list_rect_.x && x < effect_list_rect_.x + 240.0f) {
                        cycle_effect_target(tech->effects[idx]);
                    }
                    (void)row_y;
                }
            }
        }
        return;
    }

    // 前置科技：多选列表（宽列表 2 列）
    auto tech_index_at = [](const SDL_FRect& rect, float scroll, float px, float py,
                            int count, int cols) -> int {
        if (count <= 0) return -1;
        const float row_h = 24.0f;
        float rel_y = py - rect.y - 8.0f + scroll;
        if (rel_y < 0.0f) return -1;
        int row = (int)(rel_y / row_h);
        int col = cols == 2 ? (px < rect.x + rect.w * 0.5f ? 0 : 1) : 0;
        int idx = row * cols + col;
        return (idx >= 0 && idx < count) ? idx : -1;
    };

    if (point_in(requires_rect_, x, y) && !catalog()->techs().empty()) {
        int idx = tech_index_at(requires_rect_, require_scroll_, x, y,
            (int)catalog()->techs().size(), 2);
        if (idx >= 0) toggle_required_tech(catalog()->techs()[idx].name);
        return;
    }

    if (category_ == Category::Units) {
        if (point_in(attack_toggle_, x, y)) { toggle_plugin(UNIT_PLUGIN_ATTACK); return; }
        if (point_in(gather_toggle_, x, y)) { toggle_plugin(UNIT_PLUGIN_GATHER); return; }
        if (point_in(ranged_toggle_, x, y)) {
            if (auto* d = current_unit()) toggle_bool(d->attack_ranged);
            return;
        }
        if (point_in(damage_type_btn_, x, y)) { toggle_damage_type(); return; }
    }
    else {
        if (point_in(dropoff_toggle_, x, y)) {
            if (auto* d = current_building()) toggle_bool(d->dropoff);
            return;
        }
        if (point_in(research_rect_, x, y) && !catalog()->techs().empty()) {
            int idx = tech_index_at(research_rect_, research_scroll_, x, y,
                (int)catalog()->techs().size(), 1);
            if (idx >= 0) toggle_research_tech(catalog()->techs()[idx].name);
            return;
        }
        if (point_in(produce_rect_, x, y)) {
            auto choices = list_unit_type_choices();
            const float row_h = 26.0f;
            float rel_y = y - produce_rect_.y + produce_scroll_;
            int row = (int)(rel_y / row_h);
            int col = (x < produce_rect_.x + produce_rect_.w * 0.5f) ? 0 : 1;
            int idx = row * 2 + col;
            if (idx >= 0 && idx < (int)choices.size()) {
                toggle_produce(static_cast<int>(choices[idx].type));
            }
            return;
        }
    }
}

// ==================== 输入 ====================

void DefEditorScene::on_input(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            handle_click(event.button.x, event.button.y);
        }
        else if (event.button.button == SDL_BUTTON_RIGHT) {
            cancel_edit();
        }
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        // 用最近一次 MOUSE_MOTION 的坐标做命中，SDL 的 wheel 坐标在不同平台/缩放下不稳定
        float x = mouse_x_;
        float y = mouse_y_;
        if (x == 0.0f && y == 0.0f) {
            x = event.wheel.mouse_x;
            y = event.wheel.mouse_y;
        }
        if (point_in(list_rect_, x, y)) {
            int count = category_ == Category::Units ? (int)catalog()->units().size()
                        : category_ == Category::Buildings ? (int)catalog()->buildings().size()
                                                           : (int)catalog()->techs().size();
            float content = count * LIST_ROW_H;
            float max_scroll = std::max(0.0f, content - list_rect_.h);
            list_scroll_ = std::clamp(list_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
        }
        else if (has_selection() && category_ != Category::Techs && point_in(requires_rect_, x, y)) {
            int rows = ((int)catalog()->techs().size() + 1) / 2;
            float content = rows * 24.0f + 16.0f;
            float max_scroll = std::max(0.0f, content - requires_rect_.h);
            require_scroll_ = std::clamp(require_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
        }
        else if (category_ == Category::Buildings && point_in(research_rect_, x, y)) {
            float content = (float)catalog()->techs().size() * 24.0f + 16.0f;
            float max_scroll = std::max(0.0f, content - research_rect_.h);
            research_scroll_ = std::clamp(research_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
        }
        else if (category_ == Category::Buildings && point_in(produce_rect_, x, y)) {
            int rows = ((int)list_unit_type_choices().size() + 1) / 2;
            float content = rows * 26.0f;
            float max_scroll = std::max(0.0f, content - produce_rect_.h);
            produce_scroll_ = std::clamp(produce_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
        }
        else if (category_ == Category::Techs && point_in(effect_list_rect_, x, y)) {
            TechDef* tech = current_tech();
            if (tech) {
                float content = (float)tech->effects.size() * 38.0f + 16.0f;
                float max_scroll = std::max(0.0f, content - effect_list_rect_.h);
                effect_scroll_ = std::clamp(effect_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
            }
        }
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (editing_effect_ >= 0) {
            if (event.key.key == SDLK_BACKSPACE) {
                edit_replace_on_type_ = false;
                erase_last_utf8(edit_buf_);
            }
            else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                commit_edit();
            }
            else if (event.key.key == SDLK_ESCAPE) {
                cancel_edit();
            }
            return;
        }
        if (focus_ != Field::None) {
            if (event.key.key == SDLK_BACKSPACE) {
                edit_replace_on_type_ = false;
                erase_last_utf8(edit_buf_);
            }
            else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                commit_edit();
            }
            else if (event.key.key == SDLK_ESCAPE) {
                cancel_edit();
            }
            return;
        }

        if (event.key.key == SDLK_ESCAPE) {
            do_back();
        }
        else if (event.key.key == SDLK_DELETE) {
            do_delete();
        }
        return;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT) {
        if (editing_effect_ < 0 && focus_ == Field::None) return;
        std::string incoming = event.text.text ? event.text.text : "";
        if (incoming.empty()) return;
        if (edit_replace_on_type_) {
            edit_buf_.clear();
            edit_replace_on_type_ = false;
        }

        if (editing_effect_ >= 0) {
            bool has_dot = edit_buf_.find('.') != std::string::npos;
            bool has_minus = edit_buf_.find('-') != std::string::npos;
            for (char c : incoming) {
                if ((c >= '0' && c <= '9') ||
                    (c == '.' && !has_dot) ||
                    (c == '-' && !has_minus && edit_buf_.empty())) {
                    if (edit_buf_.size() < 14) edit_buf_.push_back(c);
                    if (c == '.') has_dot = true;
                    if (c == '-') has_minus = true;
                }
            }
            return;
        }

        if (focus_ == Field::FileName) {
            for (char c : incoming) {
                if (is_ascii_safe_path_char(c) && edit_buf_.size() < 40) edit_buf_.push_back(c);
            }
        }
        else if (field_is_integer(focus_)) {
            for (char c : incoming) {
                if (c >= '0' && c <= '9' && edit_buf_.size() < 12) edit_buf_.push_back(c);
            }
        }
        else if (field_is_float(focus_)) {
            bool has_dot = edit_buf_.find('.') != std::string::npos;
            for (char c : incoming) {
                if ((c >= '0' && c <= '9') || (c == '.' && !has_dot)) {
                    if (edit_buf_.size() < 12) edit_buf_.push_back(c);
                    if (c == '.') has_dot = true;
                }
            }
        }
        else {
            for (char c : incoming) {
                unsigned char uc = (unsigned char)c;
                if (uc >= 0x20 && uc != 0x7F) edit_buf_.push_back(c);
            }
            // 名字/科技不要无限长；按字节粗略限制即可
            const size_t limit = (focus_ == Field::Name) ? 72 : 240;
            if (edit_buf_.size() > limit) {
                edit_buf_.resize(limit);
                while (!edit_buf_.empty() && ((unsigned char)edit_buf_.back() & 0xC0) == 0x80)
                    edit_buf_.pop_back();
            }
        }
        return;
    }
}

// ==================== 绘制 ====================

void DefEditorScene::draw_button(const SDL_FRect& r, const std::string& label, bool active, bool danger) {
    SDL_Color bg = active ? SDL_Color{ 62, 122, 62, 255 }
        : (danger ? SDL_Color{ 110, 48, 48, 255 } : SDL_Color{ 36, 48, 84, 255 });
    SDL_Color bd = (active || danger) ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
    ui_fill_rect(renderer, r, bg, bd, active ? 2 : 1);
    ui_draw_text(renderer, label, 18, { 255, 255, 255, 255 },
        r.x + r.w * 0.5f, r.y + r.h * 0.5f, true);
}

void DefEditorScene::draw_input(const FieldView& view) {
    bool focused = (focus_ == view.field);
    SDL_Color bg = focused ? SDL_Color{ 46, 60, 96, 255 } : SDL_Color{ 30, 38, 62, 255 };
    SDL_Color bd = focused ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };

    ui_draw_text(renderer, view.label, 14, { 170, 182, 205, 255 },
        view.rect.x, view.rect.y - 18.0f, false);
    ui_fill_rect(renderer, view.rect, bg, bd, focused ? 2 : 1);

    std::string text;
    SDL_Color color = { 255, 255, 255, 255 };
    if (focused) {
        text = edit_buf_ + "_";
    }
    else {
        text = field_text(view.field);
        if (text.empty()) {
            if (view.field == Field::Texture) {
                text = u8"（空 = 名字前 4 个字）";
                color = { 130, 140, 165, 255 };
            }
        }
    }
    ui_draw_text_vcenter(renderer, text, 16, color, view.rect.x + 8.0f, view.rect.y + view.rect.h * 0.5f);
}

void DefEditorScene::draw_toggle(const SDL_FRect& r, const std::string& label, bool active) {
    ui_fill_rect(renderer, r, active ? SDL_Color{ 52, 104, 62, 255 } : SDL_Color{ 34, 42, 68, 255 },
        active ? SDL_Color{ 120, 230, 140, 255 } : SDL_Color{ 90, 110, 160, 255 }, 1);
    ui_draw_text(renderer, label, 17, active ? SDL_Color{ 180, 255, 190, 255 } : SDL_Color{ 200, 205, 220, 255 },
        r.x + r.w * 0.5f, r.y + r.h * 0.5f, true);
}

void DefEditorScene::draw_topbar() {
    ui_fill_rect(renderer, SDL_FRect{ 0, 0, LOGICAL_W, TOP_H }, { 24, 30, 48, 255 });
    ui_fill_rect(renderer, SDL_FRect{ 0, TOP_H - 3, LOGICAL_W, 3 }, { 255, 215, 0, 255 });
    ui_draw_text(renderer, u8"兵种 / 建筑编辑器", 24, { 255, 215, 0, 255 }, 16.0f, TOP_H * 0.5f, false);
    ui_draw_text(renderer, u8"文件", 16, { 190, 200, 220, 255 }, 236.0f, TOP_H * 0.5f, false);

    bool file_focused = (focus_ == Field::FileName);
    ui_fill_rect(renderer, file_rect_,
        file_focused ? SDL_Color{ 46, 60, 96, 255 } : SDL_Color{ 30, 38, 62, 255 },
        file_focused ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 }, 1);
    std::string shown = file_focused ? (edit_buf_ + "_") : file_name_;
    ui_draw_text_vcenter(renderer, shown, 17, { 255, 255, 255, 255 },
        file_rect_.x + 8.0f, file_rect_.y + file_rect_.h * 0.5f);

    draw_button(btn_save_, u8"导出", false);
    draw_button(btn_load_, u8"导入", false);
    draw_button(btn_back_, u8"返回主菜单", false);

    if (!status_.empty()) {
        ui_draw_text(renderer, status_, 15, { 150, 220, 150, 255 }, 630.0f, TOP_H * 0.5f, false);
    }
}

void DefEditorScene::draw_side_list() {
    ui_fill_rect(renderer, SDL_FRect{ 0, TOP_H, LEFT_W, LOGICAL_H - TOP_H }, { 18, 24, 40, 255 });
    ui_fill_rect(renderer, SDL_FRect{ LEFT_W - 2, TOP_H, 2, LOGICAL_H - TOP_H }, { 50, 64, 100, 255 });

    draw_button(tab_units_, u8"兵种", category_ == Category::Units);
    draw_button(tab_buildings_, u8"建筑", category_ == Category::Buildings);
    draw_button(tab_techs_, u8"科技", category_ == Category::Techs);

    bool confirming = false;
    if (category_ == Category::Techs)
        confirming = !selected_tech_name_.empty() && delete_confirm_tech_ == selected_tech_name_;
    else
        confirming = delete_confirm_id_ != 0 && delete_confirm_id_ == selected_id();
    draw_button(btn_new_, u8"新建", false);
    draw_button(btn_delete_, confirming ? u8"确认删除" : u8"删除", confirming, confirming);

    size_t count = 0;
    if (category_ == Category::Units) count = catalog()->units().size();
    else if (category_ == Category::Buildings) count = catalog()->buildings().size();
    else count = catalog()->techs().size();
    ui_draw_text(renderer, u8"共 " + std::to_string(count) + u8" 项", 14, { 160, 170, 190, 255 },
        8.0f, 194.0f, false);

    SDL_Rect clip{ 0, (int)list_rect_.y, (int)LEFT_W, (int)list_rect_.h };
    SDL_SetRenderClipRect(renderer, &clip);

    if (count == 0) {
        ui_draw_text(renderer, u8"还没有定义，点「新建」", 16, { 150, 160, 180, 255 },
            16.0f, list_rect_.y + 18.0f, false);
    }

    for (size_t i = 0; i < count; ++i) {
        int id = 0;
        std::string name;
        bool selected = false;
        if (category_ == Category::Units) {
            id = catalog()->units()[i].type_id;
            name = catalog()->units()[i].name;
            selected = (id == selected_unit_id_);
        }
        else if (category_ == Category::Buildings) {
            id = catalog()->buildings()[i].type_id;
            name = catalog()->buildings()[i].name;
            selected = (id == selected_building_id_);
        }
        else {
            name = catalog()->techs()[i].name;
            selected = (name == selected_tech_name_);
        }

        SDL_FRect row{ list_rect_.x, list_rect_.y + (float)i * LIST_ROW_H - list_scroll_,
                       list_rect_.w, LIST_ROW_H - 4.0f };
        if (row.y + row.h < list_rect_.y || row.y > list_rect_.y + list_rect_.h) continue;

        ui_fill_rect(renderer, row,
            selected ? SDL_Color{ 52, 72, 122, 255 } : SDL_Color{ 28, 36, 60, 255 },
            selected ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 60, 76, 112, 255 },
            selected ? 2 : 1);
        ui_draw_text_vcenter(renderer, name, 17, { 255, 255, 255, 255 },
            row.x + 10.0f, row.y + row.h * 0.5f);
        if (category_ != Category::Techs) {
            std::string tag = (id < CUSTOM_TYPE_ID_BASE) ? u8"内置" : ("#" + std::to_string(id));
            ui_draw_text(renderer, tag, 13, { 160, 175, 200, 255 },
                row.x + row.w - 48.0f, row.y + 8.0f, false);
        }
        else {
            const TechDef& tech = catalog()->techs()[i];
            ui_draw_text(renderer, fmt_float(tech.research_time) + u8"s", 13, { 160, 175, 200, 255 },
                row.x + row.w - 48.0f, row.y + 8.0f, false);
        }
    }

    SDL_SetRenderClipRect(renderer, nullptr);
}

void DefEditorScene::draw_preview(const std::string& name, uint32_t tex_id) {
    ui_fill_rect(renderer, preview_rect_, { 22, 28, 44, 255 }, { 90, 110, 160, 255 }, 1);
    if (tex_id) {
        SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(tex_id);
        if (tex) {
            float tw = 0, th = 0;
            if (TextureCache::instance()->get_texture_size(tex_id, tw, th) && tw > 0 && th > 0) {
                float max_w = preview_rect_.w - 16.0f;
                float max_h = preview_rect_.h - 16.0f;
                float scale = std::min(max_w / tw, max_h / th);
                float w = tw * scale, h = th * scale;
                SDL_FRect dst{ preview_rect_.x + (preview_rect_.w - w) * 0.5f,
                               preview_rect_.y + (preview_rect_.h - h) * 0.5f, w, h };
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
            }
        }
    }
    else {
        ui_draw_text(renderer, u8"无纹理", 15, { 140, 150, 175, 255 },
            preview_rect_.x + preview_rect_.w * 0.5f, preview_rect_.y + preview_rect_.h * 0.5f, true);
    }
    ui_draw_text(renderer, u8"纹理预览", 13, { 150, 160, 185, 255 },
        preview_rect_.x + preview_rect_.w * 0.5f, preview_rect_.y + preview_rect_.h - 10.0f, true);
    (void)name;
}

bool DefEditorScene::draw_tech_checklist(const SDL_FRect& rect, float& scroll,
                                         const std::string& csv, bool two_columns) {
    ui_fill_rect(renderer, rect, { 24, 31, 52, 255 }, { 60, 76, 112, 255 }, 1);

    auto& techs = catalog()->techs();
    if (techs.empty()) {
        ui_draw_text(renderer, u8"还没有科技，请先到「科技」页新建", 15,
            { 150, 160, 180, 255 }, rect.x + 12.0f, rect.y + 12.0f, false);
        return false;
    }

    SDL_Rect clip{ (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h };
    SDL_SetRenderClipRect(renderer, &clip);

    const float row_h = 24.0f;
    const float col_w = two_columns ? rect.w * 0.5f : rect.w;
    const int cols = two_columns ? 2 : 1;
    for (size_t i = 0; i < techs.size(); ++i) {
        int col = (int)i % cols;
        int row = (int)i / cols;
        float cx = rect.x + 10.0f + col * col_w;
        float cy = rect.y + 8.0f + row * row_h - scroll;
        if (cy + row_h < rect.y || cy > rect.y + rect.h) continue;

        bool checked = tech_csv_has(csv, techs[i].name);
        SDL_FRect box{ cx, cy + 3.0f, 16.0f, 16.0f };
        ui_fill_rect(renderer, box,
            checked ? SDL_Color{ 62, 142, 74, 255 } : SDL_Color{ 28, 36, 60, 255 },
            checked ? SDL_Color{ 140, 240, 150, 255 } : SDL_Color{ 90, 110, 160, 255 }, 1);
        if (checked) {
            ui_draw_text(renderer, u8"✓", 14, { 235, 255, 235, 255 },
                box.x + box.w * 0.5f, box.y + box.h * 0.5f, true);
        }
        ui_draw_text(renderer, techs[i].name, 15, { 235, 240, 250, 255 },
            cx + 24.0f, cy + 1.0f, false);
        ui_draw_text(renderer, "(" + fmt_float(techs[i].research_time) + "s)", 12,
            { 140, 155, 180, 255 },
            cx + 24.0f + std::min(150.0f, (float)techs[i].name.size() * 9.0f), cy + 4.0f, false);
    }

    SDL_SetRenderClipRect(renderer, nullptr);
    return true;
}

void DefEditorScene::draw_unit_form(UnitDef& def) {
    draw_preview(def.name, TextureCache::instance()->get_unit_texture(
        static_cast<UnitEntityType>(def.type_id), to_sdl_color(Color::SelectionBlue), 96, 96));

    ui_draw_text(renderer, u8"兵种属性  #" + std::to_string(def.type_id), 18, { 255, 215, 0, 255 },
        320.0f, 78.0f, false);

    for (const auto& fv : fields_) draw_input(fv);

    // 前置科技：从科技表勾选
    ui_draw_text(renderer, u8"前置科技（勾选已有科技，取消即不需要）", 15,
        { 190, 200, 220, 255 }, requires_rect_.x, requires_rect_.y - 18.0f, false);
    draw_tech_checklist(requires_rect_, require_scroll_, def.requires, true);

    // 插件开关
    ui_draw_text(renderer, u8"插件", 14, { 170, 182, 205, 255 }, 320.0f, 426.0f, false);
    draw_toggle(attack_toggle_, u8"攻击插件", def.has_attack());
    draw_toggle(ranged_toggle_, u8"远程攻击", def.attack_ranged);
    draw_toggle(gather_toggle_, u8"采集插件", def.has_gather());

    ui_draw_text(renderer, u8"伤害类型（点击切换）", 14, { 170, 182, 205, 255 },
        damage_type_btn_.x, damage_type_btn_.y - 18.0f, false);
    draw_button(damage_type_btn_, damage_type_name(def.damage_type), false);

    // 提示
    ui_draw_text(renderer, u8"普通：全额减免；穿刺：只受 50% 减免；攻城：对建筑 ×1.5、对单位 ×0.75。",
        15, { 160, 172, 198, 255 }, 320.0f, 558.0f, false);
    ui_draw_text(renderer, u8"防御按普通/穿刺/攻城分别设置；攻击方的伤害类型决定使用哪一项。",
        15, { 160, 172, 198, 255 }, 320.0f, 584.0f, false);
    ui_draw_text(renderer, u8"纹理文字用于生成阵营色的文字纹理；留空则使用名字前 4 个字。",
        15, { 160, 172, 198, 255 }, 320.0f, 610.0f, false);
    ui_draw_text(renderer, u8"内置兵种也可以直接编辑；不能删除，但可以改数值、纹理文字与前置科技。",
        15, { 160, 172, 198, 255 }, 320.0f, 636.0f, false);
}

void DefEditorScene::draw_building_form(BuildingDef& def) {
    draw_preview(def.name, TextureCache::instance()->get_building_texture(
        static_cast<BuildingEntityType>(def.type_id), to_sdl_color(Color::Gold), 96, 96));

    ui_draw_text(renderer, u8"建筑属性  #" + std::to_string(def.type_id), 18, { 255, 215, 0, 255 },
        320.0f, 78.0f, false);

    for (const auto& fv : fields_) draw_input(fv);

    draw_toggle(dropoff_toggle_, u8"资源提交点", def.dropoff);

    // 建造前置科技
    ui_draw_text(renderer, u8"建造前置科技（勾选已有科技）", 15,
        { 190, 200, 220, 255 }, requires_rect_.x, requires_rect_.y - 18.0f, false);
    draw_tech_checklist(requires_rect_, require_scroll_, def.requires, true);

    // 可研究科技
    ui_draw_text(renderer, u8"可研究科技（勾选后出现在本建筑生产面板）", 15,
        { 190, 200, 220, 255 }, research_rect_.x, research_rect_.y - 18.0f, false);
    draw_tech_checklist(research_rect_, research_scroll_, join_tech_names(def.research), false);

    // 可生产单位
    ui_draw_text(renderer, u8"可生产单位（点击勾选，内置 + 自定义）", 15,
        { 190, 200, 220, 255 }, produce_rect_.x, produce_rect_.y - 18.0f, false);
    ui_fill_rect(renderer, produce_rect_, { 24, 31, 52, 255 }, { 60, 76, 112, 255 }, 1);

    SDL_Rect clip{ (int)produce_rect_.x, (int)produce_rect_.y, (int)produce_rect_.w, (int)produce_rect_.h };
    SDL_SetRenderClipRect(renderer, &clip);

    auto choices = list_unit_type_choices();
    const float row_h = 26.0f;
    const float col_w = produce_rect_.w * 0.5f;
    for (size_t i = 0; i < choices.size(); ++i) {
        int col = (int)(i % 2);
        int row = (int)(i / 2);
        float cx = produce_rect_.x + 8.0f + col * col_w;
        float cy = produce_rect_.y + 8.0f + row * row_h - produce_scroll_;
        if (cy + row_h < produce_rect_.y || cy > produce_rect_.y + produce_rect_.h) continue;

        bool checked = std::find(def.produces.begin(), def.produces.end(),
            static_cast<int>(choices[i].type)) != def.produces.end();

        SDL_FRect box{ cx, cy + 4.0f, 16.0f, 16.0f };
        ui_fill_rect(renderer, box,
            checked ? SDL_Color{ 62, 142, 74, 255 } : SDL_Color{ 28, 36, 60, 255 },
            checked ? SDL_Color{ 140, 240, 150, 255 } : SDL_Color{ 90, 110, 160, 255 }, 1);
        if (checked) {
            ui_draw_text(renderer, u8"✓", 14, { 235, 255, 235, 255 },
                box.x + box.w * 0.5f, box.y + box.h * 0.5f, true);
        }
        ui_draw_text(renderer, choices[i].name, 15, { 235, 240, 250, 255 },
            cx + 24.0f, cy + 3.0f, false);
        if (choices[i].builtin) {
            ui_draw_text(renderer, u8"内置", 11, { 140, 150, 175, 255 },
                cx + 24.0f + std::min(110.0f, (float)choices[i].name.size() * 9.0f), cy + 5.0f, false);
        }
    }

    if (choices.empty()) {
        ui_draw_text(renderer, u8"还没有可生产的单位，请先到「兵种」页新建。", 15,
            { 150, 160, 180, 255 }, produce_rect_.x + 12.0f, produce_rect_.y + 12.0f, false);
    }

    SDL_SetRenderClipRect(renderer, nullptr);

    ui_draw_text(renderer, u8"提示：单位/建筑前置科技会自动补进本建筑研究列表，按科技页的成本/时间研究。",
        14, { 160, 172, 198, 255 }, 320.0f, 680.0f, false);
}

void DefEditorScene::draw_tech_form(TechDef& def) {
    draw_preview(def.name, TextureCache::instance()->get_text_texture(
        def.name, to_sdl_color(Color::Gold), 32, 96, 96));

    ui_draw_text(renderer, u8"科技设置", 18, { 255, 215, 0, 255 }, 320.0f, 78.0f, false);

    for (const auto& fv : fields_) draw_input(fv);

    draw_tech_effects(def);

    ui_draw_text(renderer, u8"科技成本与时间会用于所有引用它的建筑；改名会自动同步所有前置科技引用。",
        14, { 160, 172, 198, 255 }, 320.0f, 628.0f, false);
    ui_draw_text(renderer, u8"效果支持攻击力/攻击距离/生命上限/移动速度，可对指定兵种或全部兵种生效。",
        14, { 160, 172, 198, 255 }, 320.0f, 652.0f, false);
}

void DefEditorScene::draw_form() {
    ui_fill_rect(renderer, form_rect_, { 20, 26, 44, 255 }, { 50, 64, 100, 255 }, 1);

    if (!has_selection()) {
        std::string empty_text = u8"左侧还没有定义";
        if (category_ == Category::Units) empty_text = u8"左侧还没有兵种定义";
        else if (category_ == Category::Buildings) empty_text = u8"左侧还没有建筑定义";
        else empty_text = u8"左侧还没有科技定义";
        ui_draw_text(renderer, empty_text,
            22, { 160, 170, 195, 255 }, form_rect_.x + form_rect_.w * 0.5f,
            form_rect_.y + 200.0f, true);
        ui_draw_text(renderer, u8"点击左侧「新建」开始编辑", 18, { 140, 150, 175, 255 },
            form_rect_.x + form_rect_.w * 0.5f, form_rect_.y + 240.0f, true);
        return;
    }

    if (category_ == Category::Units) {
        if (UnitDef* d = current_unit()) draw_unit_form(*d);
    }
    else if (category_ == Category::Buildings) {
        if (BuildingDef* d = current_building()) draw_building_form(*d);
    }
    else {
        if (TechDef* d = current_tech()) draw_tech_form(*d);
    }
}

void DefEditorScene::on_render() {
    SDL_SetRenderDrawColor(renderer, 12, 16, 27, 255);
    SDL_RenderClear(renderer);

    draw_topbar();
    draw_side_list();
    draw_form();

    // 底部提示条
    ui_fill_rect(renderer, SDL_FRect{ 0, LOGICAL_H - 22.0f, LOGICAL_W, 22.0f }, { 18, 24, 40, 255 });
    ui_draw_text(renderer, u8"导出/导入目录：defs/名字.srdef    |    纹理文字最多 4 字    |    内置类型可编辑、不可删除",
        13, { 140, 152, 180, 255 }, 8.0f, LOGICAL_H - 11.0f, false);
}
