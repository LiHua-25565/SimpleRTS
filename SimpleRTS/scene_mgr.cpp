#include "scene_mgr.h"
#include "cursor_mgr.h"
#include "game_scene.h"

SceneMgr* SceneMgr::instance()
{
	static SceneMgr mgr;
	return &mgr;
}

SceneMgr::SceneMgr() = default;
SceneMgr::~SceneMgr() = default;

void SceneMgr::set_current_scene(Scene* scene)
{
	current_scene = scene;
	current_scene->on_enter();
}

void SceneMgr::on_switch(SceneType type)
{
	if (current_scene)
		current_scene->on_exit();
	switch (type)
	{
	case SceneType::Menu:
		set_current_scene(menu_scene);
		break;
	case SceneType::Game:
		set_current_scene(game_scene);
		break;
	case SceneType::Selector:
		set_current_scene(selector_scene);
		break;
	}
}

void SceneMgr::on_render()
{
	if (current_scene)
		current_scene->on_render();
}

void SceneMgr::on_update(float delta)
{
	if (current_scene)
		current_scene->on_update(delta);
}

void SceneMgr::on_input(const SDL_Event& event)
{
	CursorMgr::instance()->on_input(event);
	if (current_scene)
		current_scene->on_input(event);
}

GameScene* SceneMgr::get_game_scene() const {
	return static_cast<GameScene*>(game_scene);
}