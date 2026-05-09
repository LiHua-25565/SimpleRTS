#ifndef _CAMERA_CONTROLLER
#define _CAMERA_CONTROLLER

#include "camera.h"
#include "game_object.h"

class CameraController
{
public:
	CameraController() = default;
	~CameraController() = default;

	void set_camera(Camera* camera)
	{
		this->camera = camera;
	}

	void on_update(float delta);

	void look_at(const Vector2& center_position);

	void set_speed_scale(float scale)
	{
		speed_scale = scale;
	}

	void set_angle(float angle)
	{
		this->angle = angle;
	}

private:
	Camera* camera;

	static constexpr float PI = 3.1415926535f;
	float speed_scale = 1.0f;
	float scroll_speed = 1500.0f;
	float angle = -1;	// 取值为0~360，-1表示静止
};

#endif // !_CAMERA_CONTROLLER
