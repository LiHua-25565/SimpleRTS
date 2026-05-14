#ifndef _FACTORIES_H_
#define _FACTORIES_H_

#include "game_object.h"
#include "world_entity_mgr.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

class ObjectFactory
{
public:
	// 初始化：预创建所有资源纹理（需要 renderer）
	void init(SDL_Renderer* renderer, TTF_Font* font, GameMap* map);
	// 清理纹理资源
	void shutdown();

	GameObject* create_unit(const CollisionBox& collision_box);
	GameObject* create_resource(ResourceEntityType type, int grid_x, int grid_y, const GameMap* map);

private:
	GameMap* map = nullptr;
	TTF_Font* font = nullptr;
	SDL_Renderer* renderer = nullptr;
};
#endif // !_FACTORIES_H_
