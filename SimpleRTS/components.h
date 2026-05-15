#ifndef _COMPONENTS_H_
#define _COMPONENTS_H_

#include "color.h"
#include "render_def.h"
#include "resources_type.h"
#include "unit_type.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

class GameObject;

class Component {
public:
    virtual ~Component() = default;
};

struct UnitType : public Component {
    UnitEntityType type = UnitEntityType::Villager;
    // 可选：用于同类别内排序的子优先级
    int sub_priority = 0;
};

struct Renderable : public Component 
{
    SDL_Texture* texture = nullptr;
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

// 移动组件
struct Movable : public Component {
    float speed = 60.0f;
    Vector2 target = { -1.0f, -1.0f };  // 个人精确停止点
    Vector2 flow_target = { -1.0f, -1.0f };  // 流场导航目标（命令中心）
    Vector2 velocity = { 0.0f, 0.0f };
    MoveMode move_mode = MoveMode::Land;

    // 新增：让路撤离状态
    bool is_evading = false;
    Vector2 evade_target = { 0.0f, 0.0f };
    float evade_time_left = 0.0f;       // 撤离剩余时间（秒）

    bool is_moving() const { return target.x >= 0.0f; }
    void stop() { target = { -1.0f, -1.0f }; flow_target = { -1.0f, -1.0f }; }
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

// 采集单位组件
// 如果还未提交已有资源就开采其他种类资源
// 原来持有的资源将会消失
struct Gatherer : public Component {
    float gather_interval = 1.0f;       // 每次采集间隔（秒）
    int gather_amount = 10;             // 每次采集伤害（及获得的基础数量）

    // 携带相关
    ResourceType carried_type = ResourceType::Wood; // 当前携带的资源类型（可设一个None值）
    int carried_amount = 0;             // 当前已携带数量
    int carry_capacity = 30;            // 最大携带量

    float timer = 0.0f;                // 采集计时器
    GameObject* target_resource = nullptr; // 当前采集目标
};

// 标记建筑实体
struct Structure : public Component {
    // 标记建筑实体（用于寻路时视为障碍）
};

// 资源实体
struct Harvestable : public Component {
    ResourceEntityType entity_type = ResourceEntityType::Wood;  // 实体种类
    ResourceType output_type = ResourceType::Wood;              // 采集后产出
};

// 抛射物实体
struct Projectile : public Component
{
    // 抛射物实体
};

#endif // !_COMPONENTS_H_

