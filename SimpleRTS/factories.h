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

    GameObject* create_resource_by_type(ResourceEntityType type, int grid_x, int grid_y, bool allow_overlap = false);

    // 通用单位创建（根据类型分发）
    GameObject* create_unit_by_type(UnitEntityType type, const CollisionBox& box, bool allow_overlap = false);

    GameObject* create_villager(const CollisionBox& box, bool allow_overlap = false);   // 快捷创建（农民）

    GameObject* create_town_center(int grid_x, int grid_y, bool allow_overlap = false);

    // 新增：设置玩家ID（用于阵营纹理）
    void set_player_id(int playerId) { current_player_id = playerId; }
    GameObject* check_overlap(const CollisionBox& box) const;

public:
    static Color get_player_color(int player_id);

private:
    GameMap* map = nullptr;
    int current_player_id = 0;   // 当前创建实体所属玩家
};
#endif // !_FACTORIES_H_
