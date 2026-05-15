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

    GameObject* create_unit(const CollisionBox& collision_box, bool allow_overlap = false);
    GameObject* create_resource(ResourceEntityType type, int grid_x, int grid_y, bool allow_overlap = false);

    // 新增：设置玩家ID（用于阵营纹理）
    void set_player_id(int playerId) { currentPlayerId = playerId; }
    GameObject* check_overlap(const CollisionBox& box) const;

private:
    GameMap* map = nullptr;
    int currentPlayerId = 0;   // 当前创建实体所属玩家
};
#endif // !_FACTORIES_H_
