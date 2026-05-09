#ifndef _MOVE_SYSTEM_H_
#define _MOVE_SYSTEM_H_

#include "game_map.h"
#include <unordered_map>

class GameObject;

class MoveSystem {
public:
    void set_map(GameMap* map)
    {
        this->map = map;
    }

    void on_update(float delta);
    Vector2 compute_wall_repulsion(const GameObject* unit);
    Vector2 compute_separation(const GameObject* unit);

private:
    const float LOCAL_FLOW_RADIUS_CELLS = 20.0f; // 当距离目标小于此格子数时切换到局部流场

private:
    void correct_unwalkable_targets();
    void update_global_flow_cache();
    void update_local_flow_cache();
    void move_units(float delta);
    void push_idle_units();

    // 定义一个函数，根据单位位置和目标选择正确的流场和方向
    Vector2 get_flow_direction(const GameObject* unit, const Vector2& target,
        const Vector2& flow_target, float dist_to_target);

    GameMap* map = nullptr;
};

std::unordered_map<GameObject*, Vector2> compute_formation_targets(
    const std::vector<GameObject*>& selected_units,
    const Vector2& command_center,
    const GameMap* map);

#endif // !_MOVE_SYSTEM_H_
