#include "color.h"
#include "UI_mgr.h"
#include "render_mgr.h"

UIMgr* UIMgr::instance()
{
	static UIMgr mgr;
	return &mgr;
}

void UIMgr::init(SDL_Renderer* renderer, TTF_Font* font)
{
	this->renderer = renderer;
    this->font = font;
    TTF_OpenFont("font/SourceHanSansSC-Bold.otf", 18);

}

void UIMgr::shutdown()
{
	destroy_resource_textures();
	if (font)
	{
		TTF_CloseFont(font);
		font = nullptr;
	}
	renderer = nullptr;
}

void UIMgr::update_layout(int screen_w, int screen_h)
{
    if (screen_w <= 0 || screen_h <= 0) return;

    // 获取当前玩家需要显示的资源类型
    auto* res = ResourcesMgr::instance();
    int player = res->get_local_player_id();
    SDL_Log("current player_id is: %d", player);
    displayed_types = res->get_player_resource_types(player);

    int count = static_cast<int>(displayed_types.size());
    SDL_Log("count = %d", count);
    resource_rects.resize(count);
    resource_textures.assign(count, nullptr);
    resource_values.assign(count, 0);

    // 根据百分比计算绝对尺寸
    float panel_w = screen_w * panel_w_percent;
    float panel_h = screen_h * panel_h_percent;
    float margin_x = screen_w * panel_margin_x_percent;
    float margin_y = screen_h * panel_margin_y_percent;
    float row_spacing = screen_h * row_spacing_percent;

    float total_height = count * panel_h + (count - 1) * row_spacing;
    float start_y = screen_h - margin_y - total_height;

    for (int i = 0; i < count; ++i) {
        float y = start_y + i * (panel_h + row_spacing);
        resource_rects[i] = { margin_x, y, panel_w, panel_h };
    }

    // 动态字体大小
    int new_font_size = static_cast<int>(screen_h * font_size_percent);
    if (new_font_size != font_size) {
        font_size = new_font_size;
        TTF_CloseFont(font);
        font = TTF_OpenFont("font/SourceHanSansSC-Bold.otf", font_size);
        if (!font) {
            SDL_Log("UIMgr: Failed to reload font at size %d", font_size);
        }
        rebuild_resource_textures();
    }
}

void UIMgr::update_content()
{
    auto* res = ResourcesMgr::instance();
    int player = res->get_local_player_id();
    for (int i = 0; i < 4; ++i) {
        ResourceType type = static_cast<ResourceType>(i + 1); // 跳过 None
        int val = res->get_resource(player, type);
        if (resource_labels[i])
            resource_labels[i]->set_text(std::to_string(val));
    }
}

void UIMgr::on_render()
{
    int count = static_cast<int>(resource_rects.size());
    for (int i = 0; i < count; ++i)
    {
        const SDL_FRect& bg = resource_rects[i];

        // 1. 白色背景矩形
        RenderCmd bg_cmd;
        bg_cmd.layer = RenderLayer::UI;
        bg_cmd.color = to_sdl_color(Color::DarkGray);
        bg_cmd.position = { bg.x, bg.y };
        bg_cmd.w = bg.w;
        bg_cmd.h = bg.h;
        RenderMgr::instance()->push_main_cmd(bg_cmd);

        // 2. 文字纹理
        SDL_Texture* tex = resource_textures[i];
        if (tex) {
            float tw, th;
            if (SDL_GetTextureSize(tex, &tw, &th)) {
                RenderCmd tex_cmd;
                tex_cmd.layer = RenderLayer::UI;
                tex_cmd.texture = tex;
                tex_cmd.color = to_sdl_color(Color::White);   // 保持纹理原色
                tex_cmd.position = {
                    bg.x + (bg.w - tw) / 2.0f,
                    bg.y + (bg.h - th) / 2.0f
                };
                tex_cmd.w = tw;
                tex_cmd.h = th;
                RenderMgr::instance()->push_main_cmd(tex_cmd);
            }
        }
    }
}

void UIMgr::rebuild_resource_textures()
{
    destroy_resource_textures();
    if (!font || !renderer) return;

    int count = static_cast<int>(displayed_types.size());
    resource_textures.resize(count, nullptr);

    SDL_Color yellow = to_sdl_color(Color::Gold);
    for (int i = 0; i < count; ++i) {
        ResourceType type = displayed_types[i];
        if (type == ResourceType::None) continue;
        std::string name = resource_names[static_cast<int>(type)];
        std::string text = name + " " + std::to_string(resource_values[i]);

        SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), 0, yellow);
        if (surf) {
            resource_textures[i] = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_DestroySurface(surf);
        }
    }
}

void UIMgr::destroy_resource_textures()
{
    for (auto* tex : resource_textures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    resource_textures.clear();
}

void UIMgr::build_resource_panel()
{
    // 根面板：透明，位于左下角
    auto root = std::make_unique<ui_panel>(SDL_Color{ 0, 0, 0, 0 });
    root->set_anchor(UIAnchor::BottomLeft);
    root->set_size_percent(0.12f, 0.16f);    // 宽 12%，高 16%
    root->set_offset(10, -10);                // 左间距 10，底间距 10

    // 四个资源条：金、木、肉、石，垂直排列
    for (int i = 0; i < 4; ++i)
    {
        auto bar = root->add_child<ui_panel>(SDL_Color{ 30, 30, 30, 255 });
        bar->set_anchor(UIAnchor::TopLeft);
        bar->set_size_percent(1.0f, 0.25f);
        bar->set_offset(0, i * root->get_rect().h * 0.25f);

        // 资源名称标签（固定在左侧）
        auto name_label = bar->add_child<ui_label>(font, resource_names[i], SDL_Color{ 255, 215, 0, 255 });
        name_label->set_anchor(UIAnchor::MiddleLeft);
        name_label->set_offset(5, 0);

        // 资源数值标签（靠右显示，动态更新）
        auto value_label = bar->add_child<ui_label>(font, "0", SDL_Color{ 255, 215, 0, 255 });
        value_label->set_anchor(UIAnchor::MiddleRight);
        value_label->set_offset(-5, 0);
        resource_labels[i] = value_label;
    }

    ui_root = std::move(root);
}