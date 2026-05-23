#ifndef _UI_MGR_H_
#define _UI_MGR_H_

#include "ui_label.h"
#include "ui_panel.h"
#include "resources_mgr.h"

#include <string>
#include <vector>
#include <memory>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

class UIMgr
{
public:
    static UIMgr* instance();

    void init(SDL_Renderer* renderer, TTF_Font* font);
    void shutdown();
    void update_layout(int screen_w, int screen_h);
    void update_content();
    void on_render();
    void build_resource_panel();

private:
    UIMgr() = default;
    ~UIMgr() { shutdown(); }

    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    int font_size = 18;

    std::unique_ptr<UIComponent> ui_root;   // UI 组件树根节点
    ui_label* resource_labels[4] = { nullptr };   // 快速更新资源数值的指针

    // 资源面板相关
    std::vector<ResourceType> displayed_types;
    std::vector<SDL_FRect> resource_rects;
    std::vector<SDL_Texture*> resource_textures;
    std::vector<int> resource_values;

    // 资源名称映射（与 ResourceType 枚举索引对应）
    std::string resource_names[static_cast<int>(ResourceType::Count)] = {
    "",        // None (索引 0)
    u8"木",    // Wood (索引 1)
    u8"肉",    // Food (索引 2)
    u8"金",    // Gold (索引 3)
    u8"石"     // Stone (索引 4)
    };

    // 相对布局参数（屏幕百分比）
    float panel_w_percent = 0.10f;
    float panel_h_percent = 0.04f;
    float panel_margin_x_percent = 0.01f;
    float panel_margin_y_percent = 0.01f;
    float row_spacing_percent = 0.005f;
    float font_size_percent = 0.025f;

    void rebuild_resource_textures();
    void destroy_resource_textures();
};

#endif // !_UI_MGR_H_
