#ifndef _SELECTOR_SCENE_H_
#define _SELECTOR_SCENE_H_

#include "scene.h"

class SelectorScene :public Scene
{
public:

	SelectorScene() = default;
	~SelectorScene() = default;
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
#endif // !_SELECTOR_SCENE_H_
