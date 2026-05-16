#include "render_system.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "texture_cache.h"

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

        SDL_Texture* tex = nullptr;
        auto* gatherer = obj->get_component<Gatherer>();
        if (gatherer) {
            // 农民类单位，根据携带资源决定纹理
            ResourceType carried = gatherer->carried_type; // 可能为 None
            SDL_Color color = to_sdl_color(renderable->color); // 阵营色
            tex = TextureCache::instance()->get_carrying_unit_texture(obj->add_component<UnitType>()->type, color, (int)collider.width, (int)collider.height, carried);
        }
        else {
            tex = renderable->texture; // 普通纹理（可能已经生成过）
        }

        RenderCmd cmd{};
        cmd.position = pos;
        cmd.w = w;
        cmd.h = h;
        if (tex) cmd.texture = tex;
        cmd.layer = RenderLayer::Unit;
        cmd.color = to_sdl_color(renderable->color);

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