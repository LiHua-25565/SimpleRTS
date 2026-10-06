#ifndef _GAME_SCENE_H_
#define _GAME_SCENE_H_

#include "game_map.h"
#include "scene_mgr.h"
#include "match_config.h"
#include "selection_box.h"
#include "camera_controller.h"
#include "factories.h"
#include "render_texture.h"
#include "map_io.h"
#include "player_palette.h"

#include "render_system.h"
#include "move_system.h"
#include "move_feedback_system.h"
#include "input_system.h"
#include "resource_submit_system.h"
#include "harvest_system.h"
#include "attack_system.h"
#include "production_system.h"
#include "ai_system.h"

#include <vector>

class GameScene : public Scene
{
public:

	GameScene() = default;
	~GameScene() = default;
	void on_input(const SDL_Event& event);
	void on_update(float delta);
	void on_render();
	void on_render_overlay();   // 覆盖层（暂停菜单/结算）在 end_frame 之后绘制
	void on_enter();
	void on_exit();

	void set_local_player_id(int id) { local_player_id = id; }
	int get_local_player_id() const { return local_player_id; }

	// 阵容配置（由 SelectorScene 在进入对局前设置）
	void set_match_config(const MatchConfig& cfg) { match_config = cfg; }
	const MatchConfig& get_match_config() const { return match_config; }

private:
	int screen_w_ = 1280, screen_h_ = 720;   // 当前逻辑分辨率

	// 小地图UI 相对尺寸（屏幕百分比，最后按照最短边成正方形）
	const float minimap_width_percent = 0.15f;   
	const float minimap_height_percent = 0.22f;  
	const float minimap_margin_percent = 0.02f;   // 右下边距

private:
	int local_player_id = 1;
	MatchConfig match_config;           // 本局阵容配置（默认蓝方 2v2 普通）
	SelectionBox selection_box;
	Camera camera;
	CameraController camera_controller;
	GameMap game_map;			
	ObjectFactory factory;

	RenderSystem render_system;
	MoveSystem move_system;
	MoveFeedbackSystem move_feedback_system;
	InputSystem input_system;
	ResourceSubmitSystem resource_submit_system;
	HarvestSystem harvest_system;
	AttackSystem attack_system;
	ProductionSystem production_system;
	AISystem ai_system;

	RenderTexture map_bake_tex;  // 地形烘焙大图
	bool map_baked = false;      // 是否已经烘焙过

private:
	void update_ui_layout();

	void camera_input(const bool* keyState);

	void bake_terrain();

	// 根据 match_config 铺设基地/单位/资源并注册 AI 玩家
	void setup_match();
	// 根据难度配置 AI
	void apply_ai_difficulty();
	// 相机对准人类基地
	void center_camera_on_human();

	// 载入选定地图（地形 + 实体 + 起始位置）
	void load_selected_map();
	// 铺设地图实体（资源/中立建筑/中立单位，城镇中心作为起始标记跳过）
	void spawn_map_entities();

	// 胜负判定（歼灭模式）：某阵营所有建筑被毁即失败
	void check_win_lose();
	// 覆盖层（暂停菜单 / 胜负结算）渲染
	void render_overlay();
	// 覆盖层按钮命中
	void handle_overlay_click(float x, float y);

	std::vector<EntityRecord> map_entities_;   // 本局地图实体

	bool paused_ = false;      // 是否处于暂停菜单（Esc 开关）
	bool game_over_ = false;   // 是否已分胜负
	bool won_ = false;         // 结果：true=胜利 false=失败

	SDL_FRect resume_btn_{}, quit_btn_{};   // 覆盖层按钮矩形（逻辑坐标）
};

#endif // !_GAME_SCENE_H_
