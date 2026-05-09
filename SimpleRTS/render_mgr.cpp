#include "render_mgr.h"

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

    SDL_FRect terrain_rect = { minimap_pos.x, minimap_pos.y, minimap_w, minimap_h };
    // 小地图背景（黑底）
    if (minimap_terrain)
        SDL_RenderTexture(renderer, minimap_terrain, nullptr, &terrain_rect);
    else
    {
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderFillRect(renderer, &terrain_rect);
    }

    // 小地图边框
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderRect(renderer, &terrain_rect);

    // 遍历所有小地图指令
    for (const auto& cmd : minimap_cmd_list)
    {
        if (!cmd.is_selected)
        {
            float mm_x = minimap_pos.x + (cmd.position.x / world_w) * minimap_w;
            float mm_y = minimap_pos.y + (cmd.position.y / world_h) * minimap_h;

            SDL_FRect dot = { mm_x - 1, mm_y - 1, 3, 3 };
            SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
            SDL_RenderFillRect(renderer, &dot);
        }
    }

    // 后画【选中】的单位（顶层，5x5大点，更醒目）
    for (const auto& cmd : minimap_cmd_list)
    {
        if (cmd.is_selected)
        {
            float mm_x = minimap_pos.x + (cmd.position.x / world_w) * minimap_w;
            float mm_y = minimap_pos.y + (cmd.position.y / world_h) * minimap_h;

            // 选中的单位画大一圈，视觉上更突出
            SDL_FRect dot = { mm_x - 2, mm_y - 2, 5, 5 };
            SDL_SetRenderDrawColor(renderer, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
            SDL_RenderFillRect(renderer, &dot);
        }
    }

    if (camera)
    {
        Vector2 cam_pos = camera->get_position();
        float cam_w = camera->get_screen_w() / camera->get_scale();
        float cam_h = camera->get_screen_h() / camera->get_scale();

        float rx = minimap_pos.x + (cam_pos.x / world_w) * minimap_w;
        float ry = minimap_pos.y + (cam_pos.y / world_h) * minimap_h;
        float rw = (cam_w / world_w) * minimap_w;
        float rh = (cam_h / world_h) * minimap_h;

        SDL_FRect cam_rect = { rx, ry, rw, rh };
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
        SDL_RenderRect(renderer, &cam_rect);
    }
}

void RenderMgr::sort_cmds()
{
	std::sort(main_cmd_list.begin(), main_cmd_list.end(),
		[](const RenderCmd& a, const RenderCmd& b) {
			return (int)a.layer < (int)b.layer;
		});
}