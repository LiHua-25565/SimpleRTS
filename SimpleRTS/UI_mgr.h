#ifndef _UI_MGR_H_
#define _UI_MGR_H_

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <string>
#include <functional>
#include "resources_mgr.h"

// ---------- UI 最小单元 ----------
struct ui_region {
    float x_percent = 0.0f;
    float y_percent = 0.0f;
    float w_percent = 0.0f;
    float h_percent = 0.0f;

    SDL_Color bg_color{ 0, 0, 0, 0 };
    uint32_t texture_id = 0;
    float tex_w = 0.0f;
    float tex_h = 0.0f;

    SDL_FRect abs_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::function<void()> on_click;
};

// ---------- 锚点枚举 ----------
enum class PanelAnchor {
    BottomLeft,
    BottomCenter,
    BottomRight,
    TopLeft,
    TopCenter,
    TopRight
};

// ---------- 面板（一组区域） ----------
struct ui_panel {
    std::string name;
    PanelAnchor anchor = PanelAnchor::BottomLeft;
    float x_percent = 0.0f;
    float y_percent = 0.0f;
    float w_percent = 0.0f;
    float h_percent = 0.0f;

    std::vector<ui_region> regions;
    SDL_FRect abs_rect{ 0, 0, 0, 0 };
};

// ---------- 属性行配置 ----------
class GameObject;
struct ui_attribute_row {
    std::string label;
    std::function<bool(GameObject*)> visible;
    std::function<std::string(GameObject*)> text;
};

// ---------- UI 管理器 ----------
class UIMgr {
public:
    static UIMgr* instance();

    void init(SDL_Renderer* renderer, TTF_Font* font);
    void shutdown();
    void update_layout(int screen_w, int screen_h);
    void update_content();
    void on_render();
    bool handle_mouse_down(float x, float y);

    ui_panel& add_panel(const std::string& name, PanelAnchor anchor,
        float x_percent, float y_percent,
        float w_percent, float h_percent);
    ui_panel* find_panel(const std::string& name);
    void remove_panel(const std::string& name);

private:
    UIMgr() = default;
    ~UIMgr() { shutdown(); }

    void build_resource_panel();
    void update_selection_panel();
    void init_attribute_rows();

    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    int font_size = 18;

    std::vector<ui_panel> panels;

    // 缓存，避免重复重建
    uint64_t last_selected_id = 0;
    std::vector<int> last_resource_value_cache;

    std::vector<ui_attribute_row> attribute_rows;

    std::string resource_names[static_cast<int>(ResourceType::Count)] = {
        "", u8"木", u8"肉", u8"金", u8"石"
    };

    // 布局参数
    float panel_w_percent = 0.12f;
    float panel_h_percent = 0.16f;
    float panel_margin_x_percent = 0.01f;
    float panel_margin_y_percent = 0.01f;
    float font_size_percent = 0.025f;
};

#endif // !_UI_MGR_H_