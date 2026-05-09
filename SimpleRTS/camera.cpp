#include "camera.h"
#include <cmath>

void Camera::init(float screen_width, float screen_height, float map_width, float map_height)
{
    screen_w = screen_width;
    screen_h = screen_height;
    map_w = map_width;
    map_h = map_height;
}

void Camera::set_position(const Vector2& pos)
{
    position = pos;
    clamp_position();
}

void Camera::set_map_size(float x, float y)
{
    map_w = x;
    map_h = y;
}

void Camera::set_scale(float scale)
{
    this->scale = scale;
}

Vector2 Camera::world_to_screen(const Vector2& world_pos) const
{
    return {
        (world_pos.x - position.x) * scale,
        (world_pos.y - position.y) * scale
    };
}

Vector2 Camera::screen_to_world(const Vector2& screen_pos) const
{
    return {
        (screen_pos.x / scale) + position.x,
        (screen_pos.y / scale) + position.y
    };
}

const Vector2& Camera::get_position() const
{
    return position;
}

float Camera::get_scale() const
{
    return scale;
}

float Camera::get_screen_w() const
{
    return screen_w;
}

float Camera::get_screen_h() const
{
    return screen_h;
}

float Camera::get_map_w() const
{
    return map_w;
}

float Camera::get_map_h() const
{
    return map_h;
}

void Camera::clamp_position()
{
    float visible_w = screen_w / scale;
    float visible_h = screen_h / scale;

    if (position.x < 0) position.x = 0;
    if (position.y < 0) position.y = 0;
    if (position.x > map_w - visible_w) position.x = map_w - visible_w;
    if (position.y > map_h - visible_h) position.y = map_h - visible_h;
}