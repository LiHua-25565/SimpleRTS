#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "vector2.h"

class Camera
{
public:
    Camera() = default;
    ~Camera() = default;

    void init(float screen_width, float screen_height, float map_width, float map_height);

    void set_position(const Vector2& pos);
    void set_scale(float scale);
    void set_map_size(float x, float y);

    // ×ø±ê×ª»»
    Vector2 world_to_screen(const Vector2& world_pos) const;
    Vector2 screen_to_world(const Vector2& screen_pos) const;

    const Vector2& get_position() const;
    float get_scale() const;
    float get_screen_w() const;
    float get_screen_h() const;
    float get_map_w() const;
    float get_map_h() const;

private:
    void clamp_position();

private:
    Vector2 position{ 0, 0 };
    float scale = 1.0f;

    float screen_w = 0.0f;
    float screen_h = 0.0f;
    float map_w = 0.0f;
    float map_h = 0.0f;
};
#endif // !_CAMERA_H_
