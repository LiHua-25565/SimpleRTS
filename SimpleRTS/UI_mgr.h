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
    SDL_Color border_color{ 0, 0, 0, 0 };
    int       border_width = 0;

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

public:
    // 建筑放置模式相关
    bool is_placement_mode() const { return placement_active; }
    void enter_placement(BuildingEntityType type);
    void cancel_placement();
    void confirm_placement(int grid_x, int grid_y);
    void update_placement_preview(int grid_x, int grid_y, bool blocked);
    void clear_placement_preview();
    std::function<void(BuildingEntityType, int, int)> on_placement_confirm;
    // 获取当前放置的建筑类型（供其他模块查询）
    BuildingEntityType get_placement_building() const { return placement_building; }

private:
    bool placement_active = false;          // 是否处于放置状态
    BuildingEntityType placement_building;  // 仅保存建筑类型
    // 预览矩形状态
    bool  preview_active = false;
    int   preview_grid_x = 0;
    int   preview_grid_y = 0;
    bool  preview_blocked = false;

private:
    UIMgr() = default;
    ~UIMgr() { shutdown(); }

    void build_resource_panel();
    void update_selection_panel();
    void init_attribute_rows();

    int font_size = 18;

    std::vector<ui_panel> panels;

    uint64_t last_selected_id = 0;
    std::vector<int> last_resource_value_cache;

    std::vector<ui_attribute_row> attribute_rows;

    std::string resource_names[static_cast<int>(ResourceType::Count)] = {
        "", u8"木", u8"肉", u8"金", u8"石"
    };

    // 按下状态记录（面板名+索引，避免指针悬空）
    std::string pressed_panel_name;
    int         pressed_region_index = -1;

    // ========== 布局参数 ==========
    float panel_w_percent = 0.12f;
    float panel_h_percent = 0.16f;
    float panel_margin_x_percent = 0.01f;
    float panel_margin_y_percent = 0.01f;
    float font_size_percent = 0.025f;

    // ========== 信息面板布局参数 ==========
    float info_single_panel_w_percent = 0.30f;
    float info_multi_panel_w_percent = 0.28f;

    float single_icon_x = 0.01f;
    float single_icon_y = 0.17f;
    float single_icon_size_percent = 0.15f;
    float single_icon_to_attr_gap = 0.05f;
    float single_label_value_gap = 0.15f;
    float single_col_offset = 0.50f;
    int   single_max_rows_per_col = 3;
    float single_attr_row_height = 0.25f;

    // ========== 生产面板参数 ==========
    float prod_panel_w_percent = 0.18f;
    float prod_panel_h_percent = 0.16f;
    float prod_queue_panel_h_percent = 0.06f;
    int   prod_grid_cols = 4;
    int   prod_grid_rows = 2;
    float prod_button_size_percent = 0.16f;
    float prod_button_gap_x = 0.08f;
    float prod_button_gap_y = 0.1f;
    float prod_grid_start_x = 0.05f;
    float prod_grid_start_y = 0.15f;
    int   prod_max_buttons = 8;
    int   max_production_queue_size = 6;

    // ========== 可调颜色 ==========
    SDL_Color panel_bg_color = { 20, 20, 20, 255 };  // 所有面板统一背景色
    SDL_Color slot_bg_color = { 60, 60, 60, 255 };  // 有效槽位背景色
    SDL_Color empty_slot_bg_color = { 35, 35, 35, 255 };  // 空槽位背景色（稍暗）
    SDL_Color button_bg_color = { 60, 60, 60, 255 };  // 可用生产按钮背景色
    SDL_Color disabled_button_bg_color = { 30, 30, 30, 255 };  // 不可用按钮背景色（科技完成/排队）
    SDL_Color empty_button_bg_color = { 35, 35, 35, 255 };  // 空按钮背景色（与空槽位一致）

    void build_production_ui();
    void update_production_panel_content();
    void update_production_queue_display();

    const std::vector<ProductionItem>* current_production_list = nullptr;
    BuildingEntityType current_building_type = BuildingEntityType::TownCenter;
};

#endif // _UI_MGR_H_