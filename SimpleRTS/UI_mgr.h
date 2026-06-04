#ifndef _UI_MGR_H_
#define _UI_MGR_H_

#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <functional>
#include "resources_mgr.h"
#include "production_data.h"

// ========== UI 最小单元 ==========
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

    // 按钮按下状态（用于高亮反馈）
    SDL_Color original_color{ 0, 0, 0, 0 };
    bool      is_pressed = false;
};

// ========== 锚点枚举 ==========
enum class PanelAnchor {
    BottomLeft,
    BottomCenter,
    BottomRight,
    TopLeft,
    TopCenter,
    TopRight
};

// ========== 面板 ==========
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

// ========== 属性行配置 ==========
class GameObject;
struct ui_attribute_row {
    std::string label;
    std::function<bool(GameObject*)> visible;
    std::function<std::string(GameObject*)> text;
};

// ========== UI 管理器 ==========
class UIMgr {
public:
    static UIMgr* instance();

    void init();
    void shutdown();
    void update_layout(int screen_w, int screen_h);
    void update_content();
    void on_render();
    bool handle_mouse_down(float x, float y);
    bool handle_mouse_up(float x, float y);

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

    int font_size = 18;                             // 当前字号

    std::vector<ui_panel> panels;

    uint64_t last_selected_id = 0;
    std::vector<int> last_resource_value_cache;

    std::vector<ui_attribute_row> attribute_rows;

    std::string resource_names[static_cast<int>(ResourceType::Count)] = {
        "", u8"木", u8"肉", u8"金", u8"石"
    };

    ui_region* pressed_region = nullptr;
    ui_panel* pressed_panel = nullptr;

    // ========== 布局参数 ==========
    float panel_w_percent = 0.12f;
    float panel_h_percent = 0.16f;
    float panel_margin_x_percent = 0.01f;
    float panel_margin_y_percent = 0.01f;
    float font_size_percent = 0.025f;

    // ========== 信息面板布局参数（可调试） ==========
    float info_single_panel_w_percent = 0.30f;       // 单选信息面板宽度
    float info_multi_panel_w_percent = 0.28f;       // 多选信息面板宽度

    // 单选属性布局
    float single_icon_x = 0.01f;
    float single_icon_y = 0.17f;
    float single_icon_size_percent = 0.15f;
    float single_icon_to_attr_gap = 0.05f;
    float single_label_value_gap = 0.15f;
    float single_col_offset = 0.50f;
    int   single_max_rows_per_col = 3;
    float single_attr_row_height = 0.25f;

    // ========== 生产面板参数（可调试） ==========
    float prod_panel_w_percent = 0.18f;   // 生产按钮面板宽度
    float prod_panel_h_percent = 0.16f;   // 生产按钮面板高度（与资源面板等高）
    float prod_queue_panel_h_percent = 0.06f;   // 队列面板高度（位于生产面板上方）
    int   prod_grid_cols = 4;       // 按钮列数
    int   prod_grid_rows = 2;       // 按钮行数
    float prod_button_size_percent = 0.16f;   // 按钮宽度百分比（高度自动适配为正方形）
    float prod_button_gap_x = 0.08f;   // 按钮水平间距
    float prod_button_gap_y = 0.1f;   // 按钮垂直间距
    float prod_grid_start_x = 0.05f;   // 网格起点 X（面板内百分比）
    float prod_grid_start_y = 0.15f;   // 网格起点 Y（面板内百分比）
    int   prod_max_buttons = 8;       // 最大槽位数（应与 prod_grid_cols * prod_grid_rows 一致）
    int   max_production_queue_size = 6;       // 生产队列最大长度（同时生产上限）

    // 生产 UI 框架与内容更新
    void build_production_ui();
    void update_production_panel_content();
    void update_production_queue_display();

    // 缓存当前生产列表
    const std::vector<ProductionItem>* current_production_list = nullptr;
    BuildingEntityType current_building_type = BuildingEntityType::TownCenter;
};

#endif // !_UI_MGR_H_