#ifndef _RVO_ADAPTER_H_
#define _RVO_ADAPTER_H_

#include <RVO.h>
#include <unordered_map>
#include <vector>
#include "game_object.h"
#include "vector2.h"
#include "game_map.h"

class RVOAdapter {
public:
    static RVOAdapter* instance();

    void init(GameMap* map);
    void shutdown();

    // 设置固定时间步（需与游戏逻辑帧一致，必须 >0）
    void set_fixed_timestep(float dt) { fixed_timestep_ = dt; }
    float get_fixed_timestep() const { return fixed_timestep_; }

    // 如果模拟器尚未创建，或标记为“需要重建”，则在此处重建
    void ensure_sim_ready();

    void set_pref_velocity(uint64_t entity_id, const Vector2& pref_vel);
    void do_step();
    Vector2 get_agent_velocity(uint64_t entity_id) const;

    // 当静态障碍物或 Agent 数量/碰撞属性变化时，外部调用此函数请求重建
    void request_rebuild();

private:
    RVOAdapter() = default;
    ~RVOAdapter() { shutdown(); }

    void rebuild_simulation();

    RVO::RVOSimulator* sim_ = nullptr;
    GameMap* map_ = nullptr;
    bool rebuild_needed_ = true;
    float fixed_timestep_ = 0.1f;   // 默认 0.1 秒

    std::unordered_map<uint64_t, size_t> entity_to_agent_;
    std::vector<uint64_t> agent_to_entity_;

    std::vector<std::vector<RVO::Vector2>> cached_water_obstacles_;

    static std::vector<RVO::Vector2> box_to_obstacle(const CollisionBox& box);
};

#endif