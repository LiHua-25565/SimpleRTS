#ifndef _SCENE_MGR_H_
#define _SCENE_MGR_H_

#include "scene.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

extern Scene* menu_scene;
extern Scene* game_scene;
extern Scene* selector_scene;

class SceneMgr
{
public:
	enum class SceneType
	{
		Menu,
		Game,
		Selector
	};

public:
	static SceneMgr* instance();

	void set_current_scene(Scene* scene);
	void set_renderer(SDL_Renderer* renderer);
	void on_switch(SceneType type);
	void on_update(float delta);
	void on_render();
	void on_input(const SDL_Event& event);

private:
	SceneMgr();
	~SceneMgr();

private:
	Scene* current_scene = nullptr;
	SDL_Renderer* renderer = nullptr;

};

#endif // !_SCENE_MGR_H_

