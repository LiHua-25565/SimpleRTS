#include "render_mgr.h"
#include "color.h"

#include <algorithm>

RenderMgr* RenderMgr::instance()
{
    static RenderMgr mgr;
	return &mgr;
}

void RenderMgr::begin_frame()
{
	main_cmd_list.clear();
	minimap_cmd_list.clear();
}

void RenderMgr::push_cmd(const RenderCmd& cmd)
{
    push_main_cmd(cmd);
    push_minimap_cmd(cmd);
}

void RenderMgr::push_main_cmd(const RenderCmd& cmd)
{
	main_cmd_list.push_back(cmd);
}

void RenderMgr::push_minimap_cmd(const RenderCmd& cmd)
{
	minimap_cmd_list.push_back(cmd);
}

void RenderMgr::end_frame(SDL_Renderer* renderer)
{
    if (!renderer) return;
	sort_cmds();

    render_main(renderer);
    render_minimap(renderer);

    // 清空本帧所有指令
    main_cmd_list.clear();
    minimap_cmd_list.clear();
}

void RenderMgr::render_main(SDL_Renderer* renderer)
{
    for (auto& cmd : main_cmd_list)
    {
        // 线条类型
        if (cmd.is_line) {
            if (camera) {
                cmd.line_start = camera->world_to_screen(cmd.line_start);
                cmd.line_end = camera->world_to_screen(cmd.line_end);
            }
            SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
            SDL_RenderLine(renderer, cmd.line_start.x, cmd.line_start.y, cmd.line_end.x, cmd.line_end.y);
            continue;
        }

        // 矩形和纹理
        if (camera &&
            cmd.layer != RenderLayer::SelectBox &&
            cmd.layer != RenderLayer::UI)
        {
            cmd.position = camera->world_to_screen(cmd.position);
            cmd.w *= camera->get_scale();
            cmd.h *= camera->get_scale();
        }

        // 构建绘制矩形
        SDL_FRect dst_rect;
        dst_rect.x = cmd.position.x;
        dst_rect.y = cmd.position.y;
        dst_rect.w = cmd.w;
        dst_rect.h = cmd.h;

        // 有纹理 → 渲染贴图
        if (cmd.texture)
        {
            SDL_SetTextureColorMod(cmd.texture, cmd.color.r, cmd.color.g, cmd.color.b);
            SDL_SetTextureAlphaMod(cmd.texture, cmd.color.a);
            SDL_RenderTexture(renderer, cmd.texture, nullptr, &dst_rect);
        }
        // 无纹理 → 渲染纯色矩形（框选、小地图、选中框）
        else if (cmd.color.a != 0)
        {
            SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
            SDL_RenderFillRect(renderer, &dst_rect);
        }

        if (cmd.border_width > 0 && cmd.border_color.a > 0)
        {
            SDL_SetRenderDrawColor(renderer, cmd.border_color.r, cmd.border_color.g, cmd.border_color.b, cmd.border_color.a);
            for (int i = 0;i < cmd.border_width;i++)
            {
                SDL_RenderRect(renderer, &dst_rect);
                dst_rect.x--, dst_rect.y--;
                dst_rect.w += 2, dst_rect.h += 2;
            }
        }
    }
}

void RenderMgr::render_minimap(SDL_Renderer* renderer)
{
    if (world_w <= 0 || world_h <= 0) return;

    // 整个小地图方形区域（背景）
    SDL_FRect full_rect = { minimap_pos.x, minimap_pos.y, minimap_w, minimap_h };
    SDL_Color bg = to_sdl_color(Color::Black);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &full_rect);

    // 直接使用已缓存的内容矩形，不再重复计算
    SDL_FRect map_rect = minimap_content_rect;

    // 绘制地形纹理
    if (minimap_terrain)
        SDL_RenderTexture(renderer, minimap_terrain, nullptr, &map_rect);
    else {
        SDL_Color color = to_sdl_color(Color::DarkGray);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &map_rect);
    }

    // 小地图边框（画在完整方形区域上）
    SDL_Color border = to_sdl_color(Color::Gray);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderRect(renderer, &full_rect);

    // 绘制单位点（基于 map_rect 映射）
    for (const auto& cmd : minimap_cmd_list)
    {
        float mm_x = map_rect.x + (cmd.position.x / world_w) * map_rect.w;
        float mm_y = map_rect.y + (cmd.position.y / world_h) * map_rect.h;
        float dot_size = cmd.is_selected ? 5.0f : 3.0f;
        SDL_FRect dot = { mm_x - dot_size / 2.0f, mm_y - dot_size / 2.0f, dot_size, dot_size };
        SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
        SDL_RenderFillRect(renderer, &dot);
    }

    // 绘制相机视野框
    if (camera)
    {
        Vector2 cam_pos = camera->get_position();
        float cam_w = camera->get_screen_w() / camera->get_scale();
        float cam_h = camera->get_screen_h() / camera->get_scale();
        float rx = map_rect.x + (cam_pos.x / world_w) * map_rect.w;
        float ry = map_rect.y + (cam_pos.y / world_h) * map_rect.h;
        float rw = (cam_w / world_w) * map_rect.w;
        float rh = (cam_h / world_h) * map_rect.h;
        SDL_FRect cam_rect = { rx, ry, rw, rh };
        SDL_Color cam = to_sdl_color(Color::White);
        cam.a = 200;
        SDL_SetRenderDrawColor(renderer, cam.r, cam.g, cam.b, cam.a);
        SDL_RenderRect(renderer, &cam_rect);
    }
}

void RenderMgr::update_minimap_content_rect()
{
    if (world_w <= 0 || world_h <= 0 || minimap_w <= 0 || minimap_h <= 0) return;

    float world_aspect = world_w / world_h;
    float rect_aspect = minimap_w / minimap_h;

    if (world_aspect > rect_aspect) {
        // 世界更宽，宽度撑满，高度按比例缩小，垂直居中
        minimap_content_rect.w = minimap_w;
        minimap_content_rect.h = minimap_w / world_aspect;
        minimap_content_rect.x = minimap_pos.x;
        minimap_content_rect.y = minimap_pos.y + (minimap_h - minimap_content_rect.h) / 2.0f;
    }
    else {
        // 世界更高，高度撑满，宽度按比例缩小，水平居中
        minimap_content_rect.h = minimap_h;
        minimap_content_rect.w = minimap_h * world_aspect;
        minimap_content_rect.x = minimap_pos.x + (minimap_w - minimap_content_rect.w) / 2.0f;
        minimap_content_rect.y = minimap_pos.y;
    }
}

void RenderMgr::sort_cmds()
{
	std::sort(main_cmd_list.begin(), main_cmd_list.end(),
		[](const RenderCmd& a, const RenderCmd& b) {
			return (int)a.layer < (int)b.layer;
		});
}

void RenderMgr::set_world_size(float width, float height)
{
    world_w = width;
    world_h = height;
    update_minimap_content_rect();   // 世界尺寸变化后更新内容矩形
}

void RenderMgr::set_minimap_position(float x, float y)
{
    minimap_pos.x = x;
    minimap_pos.y = y;
    update_minimap_content_rect();
}

void RenderMgr::set_minimap_position(const Vector2& position)
{
    minimap_pos = position;
    update_minimap_content_rect();
}

void RenderMgr::set_minimap_size(float w, float h)
{
    minimap_w = w;
    minimap_h = h;
    update_minimap_content_rect();
}

float RenderMgr::get_minimap_width() const
{
    return minimap_w;
}

float RenderMgr::get_minimap_height() const
{
    return minimap_h;
}

void RenderMgr::set_minimap_terrain(SDL_Texture* tex)
{
    minimap_terrain = tex;
}

const Vector2& RenderMgr::get_minimap_position() const
{
    return minimap_pos;
}

void RenderMgr::set_minimap_content_rect(const SDL_FRect& rect)
{
    minimap_content_rect = rect;
}

const SDL_FRect& RenderMgr::get_minimap_content_rect() const
{
    return minimap_content_rect;
}

float RenderMgr::get_world_width() const
{
    return world_w;
}

float RenderMgr::get_world_height() const
{
    return world_h;
}

void RenderMgr::set_camera(Camera* camera)
{
    this->camera = camera;
}