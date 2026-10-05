#ifndef _AI_SYSTEM_H_
#define _AI_SYSTEM_H_

#include "vector2.h"
#include "game_object.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ObjectFactory;

// ========== 电脑玩家 AI ==========
//
// 目标：自主发育（造农民/分配采集/铺建筑）、骚扰对手经济、
// 攒兵按波次进攻、配合队友（协同进攻 + 协防）。
//
// 设计要点：
// 1. AI 只通过“命令层”操作 ECS —— 与人类输入系统下发的完全同一种命令
//    （Movable 目标、Gatherer 任务、ProductionQueue 入队），因此复用
//    现成的移动/采集/攻击/生产系统，不另起一套逻辑。
// 2. 每个 AI 玩家是一个独立状态机（积攒 → 进攻 → 冷却 → 积攒），
//    决策节流到 0.25s 一次，扫描一次实体池（规模小，线性扫描足够）。
// 3. 队伍信号（进攻/防守）挂在 team_id 上：任一成员发起进攻，队友在
//    兵力足够时跟随；任一成员基地被袭，队友派兵协防（含人类队友基地）。
// 4. AI 不依赖工厂生成单位（生产走建筑队列、由 ProductionSystem 落地），
//    工厂只用于铺设建筑；无头测试里 factory 为空时自动跳过铺建筑。
class AISystem {
public:
    // ---- 配置 ----
    struct AIConfig {
        int   target_villagers   = 12;     // 农民目标数
        int   food_villagers     = 3;      // 采食物农民目标数
        int   gold_villagers     = 4;      // 采金农民目标数
        int   wave_threshold     = 8;      // 首波兵力阈值
        int   wave_growth        = 2;      // 每波阈值递增
        int   max_army           = 24;     // 常备军上限
        float first_wave_grace   = 30.0f;  // 开局发育宽限期
        float attack_timeout     = 45.0f;  // 单波最长持续时间
        float retreat_ratio      = 0.30f;  // 兵力降到出击时比例以下撤退
        float reattack_cooldown  = 25.0f;  // 撤退后重新攒兵时间
        float harass_interval    = 50.0f;  // 骚扰周期
        int   harass_group       = 2;      // 每次骚扰兵力
        int   defend_group       = 4;      // 协防兵力（进攻中减半）
        int   join_min_army      = 6;      // 队友进攻信号：低于此兵力不跟随
        float threat_radius      = 260.0f; // 基地威胁判定半径
        float engage_radius      = 620.0f; // 交战/索敌半径
        float decision_interval  = 0.25f;  // 决策节流
    };

    // ---- 队伍信号（供测试观察） ----
    struct TeamSignal {
        bool   active        = false;
        float  time_left     = 0.0f;
        int    target_player = -1;   // 进攻信号：集火目标玩家
        Vector2 pos;                 // 防守信号：威胁位置
    };

    void add_ai_player(int player_id, const Vector2& rally_point);
    void set_factory(ObjectFactory* factory) { factory_ = factory; }
    void set_config(const AIConfig& cfg) { config_ = cfg; }
    const AIConfig& get_config() const { return config_; }

    void on_update(float delta);

    const TeamSignal& get_attack_signal(int team_id) const;
    const TeamSignal& get_defense_signal(int team_id) const;

    // 供测试观察（-1 = 未配置）
    int  get_phase(int player_id) const;        // 0=积攒 1=进攻 2=冷却
    int  get_wave_number(int player_id) const;
    int  count_military(int player_id) const;
    int  count_villagers(int player_id) const;

private:
    struct PlayerState {
        int  player_id = 0;
        int  team_id   = 0;
        Vector2 rally;                       // 集结/基地点
        enum class Phase { Massing, Attacking, Cooldown };
        Phase phase      = Phase::Massing;
        float phase_time = 0.0f;
        int   wave_number = 0;
        int   threshold  = 0;                 // 本波所需兵力
        int   army_at_attack_start = 0;
        int   target_player = -1;             // 进攻目标
        float decide_timer   = 0.0f;
        float harass_timer   = 0.0f;
        float build_retry    = 0.0f;
        bool  has_archery_range = false;
        std::unordered_set<uint64_t> harassers;
        std::unordered_set<uint64_t> defenders;
        Vector2 defend_dest;
    };

    // ---- 决策子步骤 ----
    void run_decision(PlayerState& st);
    void assign_idle_villagers(PlayerState& st);
    void manage_production(PlayerState& st);
    void manage_buildings(PlayerState& st);
    void detect_threats_and_defend(PlayerState& st);
    void update_wave(PlayerState& st);
    void begin_attack(PlayerState& st, int target_player);
    void command_military(PlayerState& st);
    void launch_harass(PlayerState& st);
    void cleanup_groups(PlayerState& st);

    // ---- 工具 ----
    std::vector<GameObject*> military_of(int player_id);
    std::vector<GameObject*> villagers_of(int player_id);
    std::vector<GameObject*> buildings_of(int player_id);
    int  count_military_internal(int player_id) const;
    int  count_villagers_internal(int player_id) const;
    GameObject* find_enemy_base(int my_team, int* out_owner_player) const; // 最弱敌人的城镇中心
    GameObject* nearest_enemy_unit(const Vector2& center, int my_team, float max_dist, float* out_dist = nullptr) const;
    GameObject* nearest_enemy_military(const Vector2& center, int my_team, float max_dist) const;
    GameObject* nearest_enemy_building(const Vector2& center, int my_team, float max_dist) const;
    GameObject* nearest_resource_of(const Vector2& center, ResourceType type) const;
    Vector2 enemy_resource_area(int my_team) const;      // 敌方城镇中心附近的资源点
    GameObject* own_town_center(int player_id) const;
    GameObject* own_archery_range(int player_id) const;
    void order_attack_move(GameObject* u, const Vector2& dest);
    void order_gather(GameObject* u, GameObject* resource);
    void enqueue_unit(GameObject* building, UnitEntityType type, float time,
                      int food_cost, int gold_cost, int player_id);
    bool try_place_building(PlayerState& st, BuildingEntityType type);
    Vector2 spread_offset(uint64_t id) const;            // 集结/驻守散开偏移
    void set_attack_signal(int team_id, int target_player, float duration);
    void set_defense_signal(int team_id, const Vector2& pos, float duration);
    int  gatherer_target_type(GameObject* villager) const; // 返回其目标资源类型
    std::vector<int> enemy_players_of(int my_team) const;
    bool is_enemy(GameObject* o, int my_team) const;

    ObjectFactory* factory_ = nullptr;
    std::unordered_map<int, PlayerState> players_;
    std::unordered_map<int, TeamSignal> attack_signals_;
    std::unordered_map<int, TeamSignal> defense_signals_;
    AIConfig config_;
};

#endif // !_AI_SYSTEM_H_
