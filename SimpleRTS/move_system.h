#ifndef _MOVE_SYSTEM_H_
#define _MOVE_SYSTEM_H_

#include "game_map.h"
#include <vector>
#include <unordered_map>

class GameObject;

// 移动模式开关
// 注意：components.h 中已有 MoveMode（陆地/水面），此处换名避免冲突
enum class MoveModeKind
{
    Legacy,     // 直线移动：无寻路、无避让（切回完整移动前的基线）
    FlowOnly,   // 流场寻路，不走 RVO
    FlowRVO     // 流场寻路 + RVO 局部避让
};

class MoveSystem {
public:
    void set_map(GameMap* map) { this->map = map; }
    void set_move_mode(MoveModeKind mode) { mode_ = mode; }
    MoveModeKind get_move_mode() const { return mode_; }
    void on_update(float delta);

private:
    static constexpr float LOCAL_FLOW_RADIUS_CELLS = 20.0f;

    // 到达判定阈值（像素）。比原来的 5.0 略宽松，用于抵消后续 RVO 的横向抖动
    static constexpr float ARRIVE_EPS = 6.0f;
    // 流场缓存上限与每帧生成预算（防止单帧生成多张全图流场造成卡顿）
    static constexpr int MAX_GLOBAL_FLOW_CACHE = 8;
    static constexpr int MAX_LOCAL_FLOW_CACHE = 6;
    static constexpr int MAX_GLOBAL_FLOW_PER_FRAME = 1;
    static constexpr int MAX_LOCAL_FLOW_PER_FRAME = 2;

    // 流场与导航
    void correct_unwalkable_targets();
    void update_global_flow_cache();
    void update_local_flow_cache();
    void update_projectiles(float delta);
    void compute_pref_velocities();
    void integrate_positions(float delta);
    void move_units(float delta);
    Vector2 get_flow_direction(const GameObject* unit, const Vector2& target,
        const Vector2& flow_target, float dist_to_target);

    // 编队排列（到达后自动排成方阵）
    void arrange_units(float delta);
    void build_formation_grid(Vector2 center, int total_units, float spacing);

    GameMap* map = nullptr;

    // 移动模式（A/B 开关）与 RVO 定步长累加器
    MoveModeKind mode_ = MoveModeKind::Legacy;
    float m_rvo_accumulator = 0.0f;

    // 帧计数（流场缓存 LRU 用）与本帧剩余生成预算
    uint32_t m_frame = 0;
    int m_flow_gen_budget = 0;

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
