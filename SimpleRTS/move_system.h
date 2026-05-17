#ifndef _MOVE_SYSTEM_H_
#define _MOVE_SYSTEM_H_

#include "game_map.h"
#include <vector>
#include <unordered_map>

class GameObject;

class MoveSystem {
public:
    void set_map(GameMap* map) { this->map = map; }
    void on_update(float delta);

private:
    static constexpr float LOCAL_FLOW_RADIUS_CELLS = 20.0f;

    // 流场与导航
    void correct_unwalkable_targets();
    void update_global_flow_cache();
    void update_local_flow_cache();
    void move_units();
    Vector2 get_flow_direction(const GameObject* unit, const Vector2& target,
        const Vector2& flow_target, float dist_to_target);

    // 编队排列（到达后自动排成方阵）
    void arrange_units(float delta);
    void build_formation_grid(Vector2 center, int total_units, float spacing);

    GameMap* map = nullptr;

    // 排列相关
    std::vector<Vector2> m_formation_slots;
    std::vector<bool>     m_slot_occupied;
    Vector2 m_formation_forward, m_formation_right;
    float   m_formation_spacing = 0.0f;
    int     m_formation_cols = 0, m_formation_rows = 0;
    float   m_arrange_radius = 0.0f;   // 每帧动态计算
};

// 独立工具函数（仍可用于输入系统）
std::unordered_map<GameObject*, Vector2> compute_formation_targets(
    const std::vector<GameObject*>& selected_units,
    const Vector2& command_center,
    const GameMap* map);

#endif // !_MOVE_SYSTEM_H_
