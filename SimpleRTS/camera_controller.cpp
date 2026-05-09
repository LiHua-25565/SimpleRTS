#include "camera_controller.h"
#include <cmath>

void CameraController::on_update(float delta)
{
	if (!camera || angle == -1.0f) return;

	float rad = angle * PI / 180.0f;

	Vector2 position = camera->get_position();
	position.x += (float)cos(rad) * scroll_speed * speed_scale * delta;
	position.y += (float)sin(rad) * scroll_speed * speed_scale * delta;
	camera->set_position(position);
}

void CameraController::look_at(const Vector2& center_position)
{
	if (!camera) return;

	Vector2 dst_position = center_position;
	dst_position.x -= camera->get_screen_w() / camera->get_scale() / 2;
	dst_position.y -= camera->get_screen_h() / camera->get_scale() / 2;
	camera->set_position(dst_position);
}