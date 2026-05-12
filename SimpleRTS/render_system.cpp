#include "render_system.h"
#include "render_mgr.h"
#include "selection_mgr.h"

void RenderSystem::on_render()
{
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const auto& selected_ids = SelectionMgr::instance()->get_selected_object_id_set();

    for (const auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;   // 跳过即将销毁的对象

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
        cmd.color = to_sdl_color(renderable->color);
        cmd.border_color = to_sdl_color(renderable->border_color);
        cmd.border_width = 1;
        cmd.layer = RenderLayer::Unit;

        // 如果对象被选中，覆盖为白色高亮
        if (selected_ids.count(id) > 0)
            cmd.color = to_sdl_color(Color::White);

        RenderMgr::instance()->push_cmd(cmd);
    }
}