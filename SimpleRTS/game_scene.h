#ifndef _GAME_SCENE_H_
#define _GAME_SCENE_H_

#include "game_map.h"
#include "scene_mgr.h"
#include "selection_box.h"
#include "camera_controller.h"
#include "factories.h"
#include "render_texture.h"

#include "render_system.h"
#include "move_system.h"
#include "move_feedback_system.h"

#include <vector>

class GameScene : public Scene
{
public:

	GameScene();
	~GameScene();
	void on_input(const SDL_Event& event);
	void on_update(float delta);
	void on_render();
	void on_enter();
	void on_exit();

private:
	int screen_w_ = 1280, screen_h_ = 720;   // 当前逻辑分辨率

	// 小地图UI 相对尺寸（屏幕百分比，最后按照最短边成正方形）
	const float minimap_width_percent = 0.15f;   
	const float minimap_height_percent = 0.22f;  
	const float minimap_margin_percent = 0.02f;   // 右下边距

private:
	GameMap game_map;			
	ObjectFactory factory;
	RenderSystem render_system;
	MoveSystem move_system;
	MoveFeedbackSystem move_feedback_system;
	RenderTexture map_bake_tex;  // 地形烘焙大图
	bool map_baked = false;      // 是否已经烘焙过

private:
	bool is_key_ctrl_down = false;
	bool is_key_alt_down = false;
	bool is_left_btn_down = false;
	bool is_right_btn_down = false;
	Vector2 right_drag_start_position;	// 屏幕坐标
	Vector2 camera_start_position;		// 拖拽开始时的相机世界坐标

	SelectionBox selection_box;

	Camera camera;
	CameraController camera_controller;

	void update_ui_layout();

	void camera_input(const bool* keyState);

	void bake_terrain();
};

#endif // !_GAME_SCENE_H_
