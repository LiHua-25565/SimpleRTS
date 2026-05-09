#include "render_system.h"
#include "render_mgr.h"

void RenderSystem::on_render()
{
	const auto& object_set = WorldEntityMgr::instance()->get_object_set();

	for (auto* object : object_set)
	{
		auto* renderable = object->get_component<Renderable>();
		if (!renderable) continue;

		const auto& collider = object->get_collision_box();
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

		auto* selectable = object->get_component<Selectable>();
		if (selectable->is_selected)
			cmd.color = to_sdl_color(Color::White);

		RenderMgr::instance()->push_cmd(cmd);
	}
}