#ifndef _FACTORIES_H_
#define _FACTORIES_H_

#include "game_object.h"
#include "world_entity_mgr.h"
#include <string>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

class ObjectFactory {
public:
    void init(GameMap* map);

    GameObject* create_unit(const CollisionBox& collision_box);
    GameObject* create_resource(ResourceEntityType type, int grid_x, int grid_y);

    // 新增：设置玩家ID（用于阵营纹理）
    void set_player_id(int playerId) { currentPlayerId = playerId; }

private:
    GameMap* map = nullptr;
    int currentPlayerId = 0;   // 当前创建实体所属玩家
};
#endif // !_FACTORIES_H_
