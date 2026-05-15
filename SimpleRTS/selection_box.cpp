#include "selection_box.h"
#include "selection_mgr.h"
#include "collision_box.h" 
#include <unordered_set>

void SelectionBox::on_start(float x, float y)
{
	start_position = { x, y };
	current_position = { x, y };
	is_dragging = true;
}

void SelectionBox::on_update(float x, float y)
{
	if (!is_dragging)
		return;

	current_position = { x,y };
}

void SelectionBox::on_render()
{
	if (!is_dragging)
		return;

	float rect_w = fabsf(current_position.x - start_position.x);
	float rect_h = fabsf(current_position.y - start_position.y);

	// 如果拖拽距离太小，视为单点，不绘制框选框
	if (rect_w < MIN_DRAG || rect_h < MIN_DRAG)
		return;

	float rect_x = std::min(start_position.x, current_position.x);
	float rect_y = std::min(start_position.y, current_position.y);

	RenderCmd cmd{};
	cmd.layer = RenderLayer::SelectBox;
	cmd.position.x = rect_x;
	cmd.position.y = rect_y;
	cmd.w = rect_w;
	cmd.h = rect_h;

	cmd.color = to_sdl_color(Color::None);
	cmd.border_color = to_sdl_color(Color::SelectionBlue);
	cmd.border_width = 2;

	RenderMgr::instance()->push_main_cmd(cmd);
}

bool SelectionBox::on_end(Camera& camera)
{
	is_dragging = false;

	float x0 = std::min(start_position.x, current_position.x);
	float y0 = std::min(start_position.y, current_position.y);
	float x1 = std::max(start_position.x, current_position.x);
	float y1 = std::max(start_position.y, current_position.y);
	float w = x1 - x0;
	float h = y1 - y0;

	// 小拖拽 → 单点
	if (w < MIN_DRAG || h < MIN_DRAG)
	{
		Vector2 world_click = camera.screen_to_world(start_position);
		return SelectionMgr::instance()->select_at_point(world_click);
	}

	// 框选
	Vector2 world_min = camera.screen_to_world({ x0, y0 });
	Vector2 world_max = camera.screen_to_world({ x1, y1 });
	CollisionBox world_box(world_min, world_max.x - world_min.x, world_max.y - world_min.y);
	SelectionMgr::instance()->select_in_area(world_box);
	return true;   // 框选不会触发移动
}