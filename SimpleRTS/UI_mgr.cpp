#include "ui_mgr.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "texture_cache.h"
#include "factories.h"
#include <algorithm>

UIMgr* UIMgr::instance() {
    static UIMgr mgr;
    return &mgr;
}

void UIMgr::init(SDL_Renderer* r, TTF_Font* f) {
    renderer = r;
    font = f;
    init_attribute_rows();
}

void UIMgr::shutdown() {
    panels.clear();
    font = nullptr;
    renderer = nullptr;
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
        // 字体大小变化时，旧纹理仍留在缓存中，仅重建面板
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
                tex_cmd.texture_id = region.texture_id;   // 直接使用纹理 ID
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
            SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(name_id);
            if (tex) {
                float tw, th;
                SDL_GetTextureSize(tex, &tw, &th);
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
        if (val == last_resource_value_cache[i]) continue;
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
            SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(tex_id);
            if (tex) {
                float tw, th;
                SDL_GetTextureSize(tex, &tw, &th);
                value_region.tex_w = tw;
                value_region.tex_h = th;
            }
        }
    }
}

// ---------- 属性行初始化 ----------
void UIMgr::init_attribute_rows() {
    attribute_rows.clear();
    attribute_rows.push_back({
        u8"HP",
        [](GameObject* obj) { return obj->get_component<Health>() != nullptr; },
        [](GameObject* obj) {
            auto* h = obj->get_component<Health>();
            return std::to_string(h->current_health) + "/" + std::to_string(h->max_health);
        }
        });
    attribute_rows.push_back({
        u8"攻击",
        [](GameObject* obj) { return obj->get_component<Attack>() != nullptr; },
        [](GameObject* obj) {
            auto* a = obj->get_component<Attack>();
            return std::to_string(a->damage);
        }
        });
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
    if (selected_ids.size() != 1) {
        last_selected_id = 0;
        remove_panel("selection_info");
        return;
    }

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

    // 半透明背景
    ui_region bg;
    bg.x_percent = 0.0f; bg.y_percent = 0.0f;
    bg.w_percent = 1.0f; bg.h_percent = 1.0f;
    bg.bg_color = { 20, 20, 20, 220 };
    panel.regions.push_back(bg);

    // 实体图标
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
    icon_region.texture_id = tex_id;
    icon_region.tex_w = (float)tex_w;
    icon_region.tex_h = (float)tex_h;
    panel.regions.push_back(icon_region);

    // 属性列表
    float attr_start_x = icon_x + icon_size_percent + 0.03f;
    float attr_y = 0.1f;
    float attr_row_height = 0.25f;
    SDL_Color text_color = to_sdl_color(Color::White);

    for (auto& attr : attribute_rows) {
        if (!attr.visible(obj)) continue;

        // 属性标签
        ui_region label_region;
        label_region.x_percent = attr_start_x;
        label_region.y_percent = attr_y;
        label_region.w_percent = 0.0f;
        label_region.h_percent = attr_row_height;
        label_region.bg_color = { 0, 0, 0, 0 };
        uint32_t label_id = TextureCache::instance()->get_text_texture(attr.label, text_color, font_size);
        if (label_id) {
            label_region.texture_id = label_id;
            SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(label_id);
            if (tex) {
                float tw, th;
                SDL_GetTextureSize(tex, &tw, &th);
                label_region.tex_w = tw;
                label_region.tex_h = th;
            }
        }
        panel.regions.push_back(label_region);

        // 属性数值
        std::string value_text = attr.text(obj);
        ui_region value_region;
        value_region.x_percent = attr_start_x + 0.15f;
        value_region.y_percent = attr_y;
        value_region.w_percent = 0.0f;
        value_region.h_percent = attr_row_height;
        value_region.bg_color = { 0, 0, 0, 0 };
        uint32_t value_id = TextureCache::instance()->get_text_texture(value_text, text_color, font_size);
        if (value_id) {
            value_region.texture_id = value_id;
            SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(value_id);
            if (tex) {
                float tw, th;
                SDL_GetTextureSize(tex, &tw, &th);
                value_region.tex_w = tw;
                value_region.tex_h = th;
            }
        }
        panel.regions.push_back(value_region);

        attr_y += attr_row_height + 0.02f;
    }
}