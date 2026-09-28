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

    // 清空流场缓存与绕行侧记忆。切换地图/重开一局时必须调用，
    // 否则上一局的缓存（尤其是绕行方向记忆）会被沿用
    void reset_caches();

private:
    static constexpr float LOCAL_FLOW_RADIUS_CELLS = 20.0f;
    // 自适应局部流场的半径上限（格子数）。防止超远距离目标把 Dijkstra 撑爆
    static constexpr float MAX_LOCAL_FLOW_RADIUS_CELLS = 60.0f;

    // 到达判定阈值（像素）。比原来的 5.0 略宽松，用于抵消后续 RVO 的横向抖动
    static constexpr float ARRIVE_EPS = 6.0f;

    // 到达减速带：距目标小于 speed*ARRIVE_SLOW_TIME 时线性收尾，
    // 且速度不低于 MIN_ARRIVE_SCALE —— 保留一个下限，避免在障碍附近
    // 反复"减速到 0 → 再启动"的抽搐，也保证贴着目标时仍能挤进去
    static constexpr float ARRIVE_SLOW_TIME = 0.35f;
    static constexpr float MIN_ARRIVE_SCALE = 0.30f;

    // 速度变化率平滑：从静止加速到满速所需时间（秒）。
    // 对速度做每帧限幅，消除急停急起与转向抖动
    static constexpr float ACCEL_TIME = 0.12f;

    // 切向逃逸的前瞻距离 = max(单位外接圆直径, speed * PROBE_TIME)
    static constexpr float PROBE_TIME = 0.50f;
    // 流场缓存上限与每帧生成预算（防止单帧生成多张全图流场造成卡顿）。
    // 上限必须大于"同时存在的不同目标点数"，否则缓存会抖动：
    // 每帧淘汰旧场又重建新场，表现为持续的高 CPU 占用。
    // 经验值：一局内不同目标点通常不超过 10 个
    static constexpr int MAX_GLOBAL_FLOW_CACHE = 6;
    static constexpr int MAX_LOCAL_FLOW_CACHE = 8;
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
    // RVO 的一次定步推进：同步位置 → 算首选速度 → doStep → 取回速度（不积分位置）
    void rvo_step(float fixed_dt);
    Vector2 get_flow_direction(const GameObject* unit, const Vector2& target,
        const Vector2& flow_target, float dist_to_target);

    // 局部避障：把受阻的前进方向偏转到可通行的切向上（只改方向、不降速）
    Vector2 steer_around_obstacles(uint64_t id, const Vector2& center,
        const Vector2& desired_dir, float probe_dist);

    // 带 TTL 的直线可达查询（缓存 is_line_passable 的结果）
    bool line_of_sight(uint64_t id, const Vector2& from, const Vector2& to);

    // 绕行侧记忆：避免单位在障碍两侧来回摇摆
    // side = +1 / -1 表示上次选中的偏转方向，until_frame 为该选择的保持期限
    struct AvoidState { int side = 0; uint32_t until_frame = 0; };
    std::unordered_map<uint64_t, AvoidState> m_avoid_side;

    // 视线查询缓存：id -> {检查时的帧号, 是否通视}
    std::unordered_map<uint64_t, std::pair<uint32_t, bool>> m_line_ok;

    // 编队排列（到达后自动排成方阵）
    void arrange_units(float delta);
    void build_formation_grid(Vector2 center, int total_units, float spacing);

    GameMap* map = nullptr;

    // 移动模式（A/B 开关）与 RVO 定步长累加器
    MoveModeKind mode_ = MoveModeKind::Legacy;
    float m_rvo_accumulator = 0.0f;

    // 帧计数（流场缓存 LRU 用）
    uint32_t m_frame = 0;

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
