#ifndef _SCENE_H_
#define _SCENE_H_

#include "SDL3/SDL.h"

class Scene
{
public:
	Scene() = default;
	~Scene() = default;

	void set_renderer(SDL_Renderer* renderer) { this->renderer = renderer; }
	virtual void on_input(const SDL_Event& event) {};
	virtual void on_update(float delta) {};
	virtual void on_render() {};
	virtual void on_enter() {};
	virtual void on_exit() {};

protected:
	SDL_Renderer* renderer;
};

#endif // !_SCENE_H_

