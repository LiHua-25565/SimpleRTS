#include "ui_mgr.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "texture_cache.h"
#include "factories.h"
#include <algorithm>
#include <unordered_map>  // 多选面板需要

UIMgr* UIMgr::instance() {
    static UIMgr mgr;
    return &mgr;
}

void UIMgr::init() {
    init_attribute_rows();
}

void UIMgr::shutdown() {
    panels.clear();
}

ui_panel& UIMgr::add_panel(const std::string& name, PanelAnchor anchor,
    float x_percent, float y_percent,
    float w_percent, float h_percent) {
    ui_panel panel;
    panel.name = name;
    panel.anchor = anchor;
    panel.x_percent = x_percent;
    panel.y_percent = y_percent;
    panel.w_percent = w_percent;
    panel.h_percent = h_percent;
    panels.push_back(panel);
    return panels.back();
}

ui_panel* UIMgr::find_panel(const std::string& name) {
    for (auto& p : panels) if (p.name == name) return &p;
    return nullptr;
}

void UIMgr::remove_panel(const std::string& name) {
    panels.erase(std::remove_if(panels.begin(), panels.end(),
        [&](const ui_panel& p) { return p.name == name; }), panels.end());
}

// ---------- 布局 ----------
void UIMgr::update_layout(int screen_w, int screen_h) {
    if (screen_w <= 0 || screen_h <= 0) return;

    int new_font_size = static_cast<int>(screen_h * font_size_percent);
    if (new_font_size != font_size) {
        font_size = new_font_size;
        panels.clear();
    }

    if (panels.empty()) {
        build_resource_panel();
    }

    update_selection_panel();

    for (auto& panel : panels) {
        float px = 0.0f, py = 0.0f;
        switch (panel.anchor) {
        case PanelAnchor::BottomLeft:
            px = screen_w * panel.x_percent;
            py = screen_h * (1.0f + panel.y_percent);
            break;
        case PanelAnchor::BottomCenter:
            px = screen_w * (0.5f + panel.x_percent) - screen_w * panel.w_percent * 0.5f;
            py = screen_h * (1.0f + panel.y_percent);
            break;
        case PanelAnchor::BottomRight:
            px = screen_w * (1.0f + panel.x_percent) - screen_w * panel.w_percent;
            py = screen_h * (1.0f + panel.y_percent);
            break;
        default: break;
        }
        panel.abs_rect = { px, py, screen_w * panel.w_percent, screen_h * panel.h_percent };

        for (auto& region : panel.regions) {
            region.abs_rect.x = panel.abs_rect.x + panel.abs_rect.w * region.x_percent;
            region.abs_rect.y = panel.abs_rect.y + panel.abs_rect.h * region.y_percent;
            region.abs_rect.w = panel.abs_rect.w * region.w_percent;
            region.abs_rect.h = panel.abs_rect.h * region.h_percent;
        }
    }
}

// ---------- 渲染 ----------
void UIMgr::on_render() {
    for (const auto& panel : panels) {
        for (const auto& region : panel.regions) {
            if (region.bg_color.a > 0) {
                RenderCmd bg_cmd;
                bg_cmd.layer = RenderLayer::UI;
                bg_cmd.color = { region.bg_color.r, region.bg_color.g, region.bg_color.b, region.bg_color.a };
                bg_cmd.position = { region.abs_rect.x, region.abs_rect.y };
                bg_cmd.w = region.abs_rect.w;
                bg_cmd.h = region.abs_rect.h;
                RenderMgr::instance()->push_main_cmd(bg_cmd);
            }
            if (region.texture_id) {
                RenderCmd tex_cmd;
                tex_cmd.layer = RenderLayer::UI;
                tex_cmd.texture_id = region.texture_id;
                tex_cmd.color = { 255, 255, 255, 255 };

                float offset_x = 0.0f;
                if (region.x_percent == 0.0f) offset_x = 5.0f;
                else if (region.x_percent == 1.0f) offset_x = -5.0f - region.tex_w;

                tex_cmd.position = {
                    region.abs_rect.x + offset_x,
                    region.abs_rect.y + (region.abs_rect.h - region.tex_h) * 0.5f
                };
                tex_cmd.w = region.tex_w;
                tex_cmd.h = region.tex_h;
                RenderMgr::instance()->push_main_cmd(tex_cmd);
            }
        }
    }
}

// ---------- 点击事件 ----------
bool UIMgr::handle_mouse_down(float x, float y) {
    for (auto panel_it = panels.rbegin(); panel_it != panels.rend(); ++panel_it) {
        for (auto& region : panel_it->regions) {
            if (region.on_click &&
                x >= region.abs_rect.x && x <= region.abs_rect.x + region.abs_rect.w &&
                y >= region.abs_rect.y && y <= region.abs_rect.y + region.abs_rect.h) {
                region.on_click();
                return true;
            }
        }
    }
    return false;
}

// ---------- 资源面板构建 ----------
void UIMgr::build_resource_panel() {
    auto* res = ResourcesMgr::instance();
    int player = res->get_local_player_id();
    auto displayed_types = res->get_player_resource_types(player);
    if (displayed_types.empty()) return;

    auto& panel = add_panel("resources", PanelAnchor::BottomLeft,
        panel_margin_x_percent,
        -(panel_h_percent + panel_margin_y_percent),
        panel_w_percent, panel_h_percent);

    int count = static_cast<int>(displayed_types.size());
    float bar_h = 1.0f / count;
    SDL_Color yellow = to_sdl_color(Color::Gold);

    last_resource_value_cache.resize(count, -1);

    for (int i = 0; i < count; ++i) {
        ResourceType type = displayed_types[i];
        std::string name = resource_names[static_cast<int>(type)];

        // 背景条
        ui_region bg;
        bg.x_percent = 0.0f; bg.y_percent = i * bar_h;
        bg.w_percent = 1.0f; bg.h_percent = bar_h;
        bg.bg_color = { 30, 30, 30, 255 };
        panel.regions.push_back(bg);

        // 名称标签
        ui_region name_region;
        name_region.x_percent = 0.0f; name_region.y_percent = i * bar_h;
        name_region.w_percent = 0.0f; name_region.h_percent = bar_h;
        name_region.bg_color = { 0, 0, 0, 0 };
        uint32_t name_id = TextureCache::instance()->get_text_texture(name, yellow, font_size);
        if (name_id) {
            name_region.texture_id = name_id;
            float tw, th;
            if (TextureCache::instance()->get_texture_size(name_id, tw, th)) {
                name_region.tex_w = tw;
                name_region.tex_h = th;
            }
        }
        panel.regions.push_back(name_region);

        // 数值标签（先占位，稍后由 update_content 填充）
        ui_region value_region;
        value_region.x_percent = 1.0f; value_region.y_percent = i * bar_h;
        value_region.w_percent = 0.0f; value_region.h_percent = bar_h;
        value_region.bg_color = { 0, 0, 0, 0 };
        panel.regions.push_back(value_region);
    }
    update_content(); // 首次填充数值
}

// ---------- 资源数值更新 ----------
void UIMgr::update_content() {
    auto* res = ResourcesMgr::instance();
    int player = res->get_local_player_id();
    const auto& types = res->get_player_resource_types(player);

    if (last_resource_value_cache.size() != types.size())
        last_resource_value_cache.resize(types.size(), -1);

    for (size_t i = 0; i < types.size(); ++i) {
        int val = res->get_resource(player, types[i]);
        if (val == last_resource_value_cache[i]) continue;  // 无变化跳过
        last_resource_value_cache[i] = val;

        auto* panel = find_panel("resources");
        if (!panel || i * 3 + 2 >= panel->regions.size()) continue;
        auto& value_region = panel->regions[i * 3 + 2];

        // 释放旧纹理
        if (value_region.texture_id) {
            TextureCache::instance()->release_texture(value_region.texture_id);
            value_region.texture_id = 0;
        }

        SDL_Color yellow = to_sdl_color(Color::Gold);
        std::string text = std::to_string(val);
        uint32_t tex_id = TextureCache::instance()->get_text_texture(text, yellow, font_size);
        if (tex_id) {
            value_region.texture_id = tex_id;
            float tw, th;
            if (TextureCache::instance()->get_texture_size(tex_id, tw, th)) {
                value_region.tex_w = tw;
                value_region.tex_h = th;
            }
        }
    }
}

// ---------- 属性行初始化 ----------
void UIMgr::init_attribute_rows() {
    attribute_rows.clear();

    // HP
    attribute_rows.push_back({
        u8"HP",
        [](GameObject* obj) { return obj->get_component<Health>() != nullptr; },
        [](GameObject* obj) {
            auto* h = obj->get_component<Health>();
            return std::to_string(h->current_health) + "/" + std::to_string(h->max_health);
        }
        });

    // 攻击
    attribute_rows.push_back({
        u8"攻击",
        [](GameObject* obj) { return obj->get_component<Attack>() != nullptr; },
        [](GameObject* obj) {
            auto* a = obj->get_component<Attack>();
            std::string str = std::to_string(a->damage);
            int sum = 0;
            for (int i = 0; i < 4; ++i) sum += a->armor_penetration[i];
            if (sum > 0) {
                str += "(";
                for (int i = 0; i < 4; ++i) {
                    str += std::to_string(a->armor_penetration[i]);
                    if (i < 3) str += "/";
                }
                str += ")";
            }
            return str;
        }
        });

    // 护甲（新增）
    attribute_rows.push_back({
        u8"护甲",
        [](GameObject* obj) { return obj->get_component<Armor>() != nullptr; },
        [](GameObject* obj) {
            auto* armor = obj->get_component<Armor>();
            if (!armor) return std::string("");
            std::string type_str;
            switch (armor->type) {
                case ArmorType::None:     type_str = u8"无甲"; break;
                case ArmorType::Light:    type_str = u8"轻甲"; break;
                case ArmorType::Heavy:    type_str = u8"重甲"; break;
                case ArmorType::Building: type_str = u8"建筑"; break;
                default:                  type_str = u8"未知"; break;
            }
            return type_str + " " + std::to_string(armor->armor_value);
        }
        });

    // 采集
    attribute_rows.push_back({
        u8"采集",
        [](GameObject* obj) { return obj->get_component<Gatherer>() != nullptr; },
        [](GameObject* obj) {
            auto* g = obj->get_component<Gatherer>();
            return std::to_string(g->gather_amount);
        }
        });

    // 携带
    attribute_rows.push_back({
        u8"携带",
        [](GameObject* obj) {
            auto* g = obj->get_component<Gatherer>();
            return g && g->carried_amount > 0;
        },
        [this](GameObject* obj) {
            auto* g = obj->get_component<Gatherer>();
            if (!g) return std::string("0");
            std::string type_name = resource_names[static_cast<int>(g->carried_type)];
            return std::to_string(g->carried_amount) + " " + type_name;
        }
        });
}

// ---------- 选中信息面板 ----------
void UIMgr::update_selection_panel() {
    auto selected_ids = SelectionMgr::instance()->get_selected_object_id_set();
    if (selected_ids.empty()) {
        last_selected_id = 0;
        remove_panel("selection_info");
        return;
    }

    // 多选处理
    if (selected_ids.size() > 1) {
        last_selected_id = 0;
        std::unordered_map<UnitEntityType, std::vector<GameObject*>> unit_map;
        for (uint64_t id : selected_ids) {
            GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
            if (!obj || !obj->check_valid()) continue;
            auto* unit_type = obj->get_component<UnitType>();
            if (!unit_type) continue;
            unit_map[unit_type->type].push_back(obj);
        }

        if (unit_map.empty()) {
            remove_panel("selection_info");
            return;
        }

        std::vector<std::pair<UnitEntityType, std::vector<GameObject*>>> sorted_units(
            unit_map.begin(), unit_map.end());
        std::sort(sorted_units.begin(), sorted_units.end(),
            [](const auto& a, const auto& b) {
                int hp_a = a.second.front()->get_component<Health>()->max_health;
                int hp_b = b.second.front()->get_component<Health>()->max_health;
                return hp_a > hp_b;
            });

        float info_x = panel_margin_x_percent + panel_w_percent + 0.005f;
        float info_y = -(panel_h_percent + panel_margin_y_percent);
        float info_w = 0.20f;
        float info_h = panel_h_percent;

        remove_panel("selection_info");
        auto& panel = add_panel("selection_info", PanelAnchor::BottomLeft,
            info_x, info_y, info_w, info_h);

        ui_region bg;
        bg.x_percent = 0.0f; bg.y_percent = 0.0f;
        bg.w_percent = 1.0f; bg.h_percent = 1.0f;
        bg.bg_color = { 20, 20, 20, 220 };
        panel.regions.push_back(bg);

        float icon_size = 0.15f;
        float start_x = 0.05f;
        float start_y = 0.05f;
        float step_x = icon_size + 0.02f;
        float step_y = icon_size + 0.02f;
        int max_cols = (int)((1.0f - 2 * start_x) / step_x) + 1;

        int tex_w = 32, tex_h = 32;
        SDL_Color text_color = to_sdl_color(Color::White);

        int index = 0;
        for (auto& [type, units] : sorted_units) {
            int count = (int)units.size();
            float x = start_x + (index % max_cols) * step_x;
            float y = start_y + (index / max_cols) * step_y;

            ui_region icon_region;
            icon_region.x_percent = x;
            icon_region.y_percent = y;
            icon_region.w_percent = icon_size;
            icon_region.h_percent = icon_size * (info_w / info_h);
            icon_region.bg_color = { 0, 0, 0, 0 };

            Color player_color = ObjectFactory::get_player_color(
                units[0]->get_component<Ownership>() ?
                units[0]->get_component<Ownership>()->player_id : 0);
            uint32_t icon_id = TextureCache::instance()->get_unit_texture(
                type, to_sdl_color(player_color), tex_w, tex_h);
            icon_region.texture_id = icon_id;
            icon_region.tex_w = (float)tex_w;
            icon_region.tex_h = (float)tex_h;
            panel.regions.push_back(icon_region);

            if (count > 1) {
                ui_region count_region;
                count_region.x_percent = x + icon_size * 0.6f;
                count_region.y_percent = y + icon_size * 0.6f;
                count_region.w_percent = icon_size * 0.4f;
                count_region.h_percent = icon_size * 0.4f;
                count_region.bg_color = { 0, 0, 0, 0 };
                uint32_t count_id = TextureCache::instance()->get_text_texture(
                    std::to_string(count), text_color, font_size);
                if (count_id) {
                    count_region.texture_id = count_id;
                    float tw, th;
                    if (TextureCache::instance()->get_texture_size(count_id, tw, th)) {
                        count_region.tex_w = tw;
                        count_region.tex_h = th;
                    }
                }
                panel.regions.push_back(count_region);
            }
            index++;
        }
        return;
    }

    // 单选处理
    uint64_t id = *selected_ids.begin();
    last_selected_id = id;

    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
    if (!obj || !obj->check_valid()) {
        remove_panel("selection_info");
        return;
    }

    float info_x = panel_margin_x_percent + panel_w_percent + 0.005f;
    float info_y = -(panel_h_percent + panel_margin_y_percent);
    float info_w = 0.20f;
    float info_h = panel_h_percent;

    remove_panel("selection_info");
    auto& panel = add_panel("selection_info", PanelAnchor::BottomLeft,
        info_x, info_y, info_w, info_h);

    ui_region bg;
    bg.x_percent = 0.0f; bg.y_percent = 0.0f;
    bg.w_percent = 1.0f; bg.h_percent = 1.0f;
    bg.bg_color = { 20, 20, 20, 220 };
    panel.regions.push_back(bg);

    float icon_size_percent = 0.35f;
    float icon_x = 0.03f, icon_y = 0.1f;
    ui_region icon_region;
    icon_region.x_percent = icon_x;
    icon_region.y_percent = icon_y;
    icon_region.w_percent = icon_size_percent;
    icon_region.h_percent = icon_size_percent * (info_w / info_h);
    icon_region.bg_color = { 0, 0, 0, 0 };

    int tex_w = 64, tex_h = 64;
    uint32_t tex_id = 0;
    if (auto* unit_type = obj->get_component<UnitType>()) {
        Color player_color = ObjectFactory::get_player_color(
            obj->get_component<Ownership>() ? obj->get_component<Ownership>()->player_id : 0);
        tex_id = TextureCache::instance()->get_unit_texture(unit_type->type, to_sdl_color(player_color), tex_w, tex_h);
    }
    else if (auto* harvestable = obj->get_component<Harvestable>()) {
        tex_id = TextureCache::instance()->get_resource_texture(harvestable->entity_type, tex_w, tex_h);
    }
    else if (auto* building_type = obj->get_component<BuildingType>()) {
        Color player_color = ObjectFactory::get_player_color(
            obj->get_component<Ownership>() ? obj->get_component<Ownership>()->player_id : 0);
        tex_id = TextureCache::instance()->get_building_texture(
            building_type->type, to_sdl_color(player_color), tex_w, tex_h);
    }
    icon_region.texture_id = tex_id;
    icon_region.tex_w = (float)tex_w;
    icon_region.tex_h = (float)tex_h;
    panel.regions.push_back(icon_region);

    // ==================== 属性列表（先排满第一列，再排第二列） ====================
    // 收集所有可见属性
    std::vector<ui_attribute_row*> visible_attrs;
    for (auto& attr : attribute_rows)
        if (attr.visible(obj))
            visible_attrs.push_back(&attr);

    int total_visible = (int)visible_attrs.size();
    const int max_rows_per_col = 3;               // 每列最大行数
    int cols = (total_visible > max_rows_per_col) ? 2 : 1;   // 是否需要两列

    float attr_start_x = icon_x + icon_size_percent + 0.03f;
    float second_col_start_x = attr_start_x + 0.5f * 0.8f; // 第二列起始 x
    float attr_y = 0.1f;
    float attr_row_height = 0.25f;
    SDL_Color text_color = to_sdl_color(Color::White);

    for (int i = 0; i < total_visible; i++)
    {
        int col = i / max_rows_per_col;            // 0：第一列，1：第二列
        int row = i % max_rows_per_col;            // 该列中的行号

        float label_x = (col == 0) ? attr_start_x : second_col_start_x;
        float value_x = label_x + 0.15f;
        float y = attr_y + row * (attr_row_height + 0.02f);

        auto* attr = visible_attrs[i];

        // 标签
        ui_region label_region;
        label_region.x_percent = label_x;
        label_region.y_percent = y;
        label_region.w_percent = 0.0f;
        label_region.h_percent = attr_row_height;
        label_region.bg_color = { 0, 0, 0, 0 };
        uint32_t label_id = TextureCache::instance()->get_text_texture(attr->label, text_color, font_size);
        if (label_id) {
            label_region.texture_id = label_id;
            float tw, th;
            if (TextureCache::instance()->get_texture_size(label_id, tw, th)) {
                label_region.tex_w = tw;
                label_region.tex_h = th;
            }
        }
        panel.regions.push_back(label_region);

        // 数值
        std::string value_text = attr->text(obj);
        ui_region value_region;
        value_region.x_percent = value_x;
        value_region.y_percent = y;
        value_region.w_percent = 0.0f;
        value_region.h_percent = attr_row_height;
        value_region.bg_color = { 0, 0, 0, 0 };
        uint32_t value_id = TextureCache::instance()->get_text_texture(value_text, text_color, font_size);
        if (value_id) {
            value_region.texture_id = value_id;
            float tw, th;
            if (TextureCache::instance()->get_texture_size(value_id, tw, th)) {
                value_region.tex_w = tw;
                value_region.tex_h = th;
            }
        }
        panel.regions.push_back(value_region);
    }
}