#ifndef _MENU_SCENE_H_
#define _MENU_SCENE_H_

#include "scene.h"
#include <vector>

class MenuScene : public Scene
{
public:

	MenuScene() = default;
	~MenuScene() = default;
	void on_input(const SDL_Event& event) {};
	void on_update(float delta) {};
	void on_render()
	{

	}

	void on_enter()
	{

	}

	void on_exit()
	{

	}

private:
};
#endif // !_MENU_SCENE_H_
