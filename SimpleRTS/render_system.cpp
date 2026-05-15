#include "render_system.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"

// render_system.cpp
void RenderSystem::on_render()
{
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const auto& selected_ids = SelectionMgr::instance()->get_selected_object_id_set();
    int local_player_id = ResourcesMgr::instance()->get_local_player_id();

    for (const auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;

        auto* renderable = obj->get_component<Renderable>();
        if (!renderable) continue;

        const auto& collider = obj->get_collision_box();
        const auto& pos = collider.position;
        float w = collider.width;
        float h = collider.height;

        RenderCmd cmd{};
        cmd.position = pos;
        cmd.w = w;
        cmd.h = h;
        cmd.texture = renderable->texture;
        cmd.layer = RenderLayer::Unit;

        // 纹理原色，默认不染色
        cmd.color = to_sdl_color(Color::White);

        bool is_selected = selected_ids.count(id) > 0;
        auto* ownership = obj->get_component<Ownership>();
        int player_id = ownership ? ownership->player_id : 0;
        bool is_local = (player_id == local_player_id);

        if (is_selected)
        {
            // 选中状态：边框亮白且加粗
            cmd.border_width = 2;
            cmd.border_color = to_sdl_color(Color::White);

            // 己方单位选中时纹理变亮（可选，这里通过纹理染色实现）
            if (is_local)
                cmd.color = to_sdl_color(Color::White);  // 保持原色即可
        }
        else
        {
            // 未选中状态：根据阵营显示边框颜色
            cmd.border_width = 1;
            if (is_local)
                cmd.border_color = to_sdl_color(Color::SoftBlue);  // 淡蓝（己方）
            else if (player_id == 0)
                cmd.border_color = to_sdl_color(Color::Gray);  // 灰色（中立）
            else
                cmd.border_color = to_sdl_color(Color::SoftRed);  // 淡红（敌方示例）
        }

        RenderMgr::instance()->push_cmd(cmd);
    }
}