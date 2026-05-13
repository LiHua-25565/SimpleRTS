#ifndef _COMPONENTS_H_
#define _COMPONENTS_H_

#include "color.h"
#include "render_def.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

class Component {
public:
    virtual ~Component() = default;
};

enum class UnitCategory : uint8_t {
    Melee,      // 近战
    Ranged,     // 远程
    Siege,      // 攻城
    Villager    // 农民
};

struct UnitType : public Component {
    UnitCategory category = UnitCategory::Melee;
    // 可选：用于同类别内排序的子优先级
    int sub_priority = 0;
};

struct Renderable : public Component 
{
    SDL_Texture* texture = nullptr;
    Color color = Color::White;
    Color border_color = Color::None;
    RenderLayer layer = RenderLayer::Ground;
    float scale = 1.0f;
};

struct Selectable : public Component 
{
    bool is_selected = false;
    float selectionRadius = 20.0f;
};

enum class MoveMode
{
    Land,
    Water       // 可走水路，暂未实现
};

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

struct Ownership : public Component 
{
    int playerId = 0;
    int teamId = 0;
};

struct Health : public Component 
{
    int current_health = 100;
    int max_health = 100;
};

struct Structure : public Component {
    // 标记建筑实体（用于寻路时视为障碍）
};

struct Harvestable : public Component {
    // 资源实体 (寻路时视为障碍)
};

#endif // !_COMPONENTS_H_

