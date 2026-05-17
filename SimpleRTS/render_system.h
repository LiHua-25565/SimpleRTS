#ifndef _RENDER_SYSTEM_H_
#define _RENDER_SYSTEM_H_

#include "world_entity_mgr.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

class RenderSystem
{
public:
	void on_update(float delta);
	void on_render();
};

#endif // !_RENDER_SYSTEM_H_
