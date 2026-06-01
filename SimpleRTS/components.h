#ifndef _COMPONENTS_H_
#define _COMPONENTS_H_

#include "color.h"
#include "timer.h"
#include "collision_box.h"
#include "render_def.h"
#include "resources_type.h"
#include "unit_type.h"
#include "armor_type.h"
#include "building_type.h"
#include "projectile_type.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

inline constexpr uint8_t WOOD_MASK = 1 << 0;
inline constexpr uint8_t FOOD_MASK = 1 << 1;
inline constexpr uint8_t GOLD_MASK = 1 << 2;
inline constexpr uint8_t STONE_MASK = 1 << 3;
inline constexpr uint8_t ALL_MASK = WOOD_MASK | FOOD_MASK | GOLD_MASK | STONE_MASK;

class GameObject;

class Component {
public:
    virtual ~Component() = default;
};

struct UnitType : public Component 
{
    UnitEntityType type = UnitEntityType::Villager;
    // 用于同类别内排序的子优先级
    int sub_priority = 0;
};

struct BuildingType :public Component
{
    BuildingEntityType type = BuildingEntityType::TownCenter;
};

struct Renderable : public Component 
{
    uint32_t texture_id = 0;
    CollisionBox collision_box;
    Color color = Color::White;                 // 矩形颜色（会被纹理覆盖）
    Color border_color = Color::None;           // 边框颜色
    RenderLayer layer = RenderLayer::Ground;
    float scale = 1.0f;
};

struct Selectable : public Component 
{
    bool is_selected = false;
    float selection_radius = 20.0f;
};

enum class MoveMode
{
    Land,
    Water       // 可走水路，暂未实现
};

// 近战与采集的撞击动画
struct ImpactAnimation : public Component {
    bool is_attacking = false;      // 是否播放中
    float anim_pass_time = 0.0f;    // 动画计时器
    float anim_wait_time = 0.3f;    // 动画总时长（秒）
    float impact_distance = 0.8f;   // 实际撞击距离（边长比例）
    Vector2 direction = { 1.0f, 0.0f }; // 冲击方向（单位向量），默认向右
};

// 闪烁组件
struct FlashComponent : public Component {
    float flash_timer = 0.0f;       // 当前闪烁计时（倒计时）
    float flash_duration = 0.5f;    // 总闪烁时间（秒）
    bool flash_active = false;      // 是否正在闪烁
    float blink_interval = 0.1f;    // 闪烁间隔（亮/暗切换）
    float blink_timer = 0.0f;       // 当前间隔计时
    bool blink_on = true;           // 当前是否为亮状态
};

// 移动组件
struct Movable : public Component {
    float speed = 60.0f;
    Vector2 target = { -1.0f, -1.0f };   // 个人精确停止点
    Vector2 flow_target = { -1.0f, -1.0f };   // 流场导航目标（命令中心）
    Vector2 velocity = { 0.0f, 0.0f };
    MoveMode move_mode = MoveMode::Land;

    // 排列状态（到达目标附近后使用）
    bool is_arranging = false;
    int  formation_slot = -1;

    bool is_moving() const { return target.x >= 0.0f || is_arranging; }
    void stop() {
        target = { -1.0f, -1.0f };
        flow_target = { -1.0f, -1.0f };
        velocity = { 0.0f,0.0f };
        is_arranging = false;
    }
};

// 阵营组件
struct Ownership : public Component 
{
    int player_id = 0;
    int team_id = 0;
};

// 生命值组件
struct Health : public Component 
{
    int current_health = 100;
    int max_health = 100;
};

// 攻击组件
struct Attack : public Component {
    bool can_attack = true;
    float attack_interval = 1.0f;       // 攻击间隔（秒）
    float attack_pass_time = 0.0f;      // 当前冷却计时
    int damage = 10;                    // 每次攻击伤害
    float range = 30.0f;                // 攻击范围（像素），近战约 30，远程 200+
    bool is_ranged = false;             // 是否远程攻击（影响距离检测方式）
    uint64_t target_id = 0;             // 攻击目标的实体 ID

    bool auto_attack = false;           // 是否自动索敌（右键攻击后为 true，移动/停止后为 false）

    int armor_penetration[static_cast<int>(ArmorType::Count)] = { 0 };  // 护甲穿透
};

// 护甲组件
struct Armor : public Component {
    ArmorType type = ArmorType::None;
    int armor_value = 0;   // 基础护甲值，可由具体实体覆盖
};

// 采集单位组件
// 如果还未提交已有资源就开采其他种类资源
// 原来持有的资源将会消失
struct Gatherer : public Component {
    bool can_gather = true;
    float gather_interval = 1.0f;       // 每次采集间隔（秒）
    float gather_pass_time = 0.0f;      // 采集等待时间
    int gather_amount = 10;             // 每次采集伤害（及获得的基础数量）

    // 携带相关
    ResourceType carried_type = ResourceType::None; // 当前携带的资源类型（可设一个None值）
    int carried_amount = 0;             // 当前已携带数量
    int carry_capacity = 30;            // 最大携带量

    uint64_t target_resource_id = 0;   // 采集目标实体 ID
    uint64_t dropoff_target_id = 0;    // 提交目标建筑实体 ID
};

// 标记建筑实体
struct Structure : public Component {
    // 标记建筑实体（用于寻路时视为障碍）
};

// ---------- 生产队列组件（挂载在建筑上） ----------
struct ProductionQueue : public Component {
    struct QueueEntry {
        UnitEntityType unit_type;
        float elapsed = 0.0f;       // 已生产时间
        float total_time = 0.0f;    // 总生产时间
    };
    std::vector<QueueEntry> queue;  // 队列，最多可配置上限
    bool frozen_flag = false;       // 当周围无空位时冻结，下一帧继续尝试
};

// 提交资源建筑组件
struct ResourceDropoff : public Component {
    uint8_t accept_mask = 0;   // 初始0，通过 | 添加可接受的资源种类
};

// 资源实体
struct Harvestable : public Component {
    ResourceEntityType entity_type = ResourceEntityType::Wood;  // 实体种类
    ResourceType output_type = ResourceType::Wood;              // 采集后产出
};

// 抛射物实体
struct Projectile : public Component {
    float speed = 400.0f;            // 飞行速度（像素/秒）
    uint64_t target_id = 0;         // 目标实体 ID
    int damage = 0;                 // 命中伤害
    int attacker_team_id = 0;       // 攻击方队伍 ID（暂未用，可预留）
};

#endif // !_COMPONENTS_H_

