#include "move_feedback_system.h"
#include "world_entity_mgr.h"
#include "render_mgr.h"

void MoveFeedbackSystem::add_line_for_unit(GameObject* unit, const Vector2& end, float duration)
{
    if (!unit) return;

    // 移除该单位已有的旧线（确保一个单位只有一条动态反馈线）
    for (auto it = line_list.begin(); it != line_list.end(); )
    {
        if (it->owner == unit)
            it = line_list.erase(it);
        else
            ++it;
    }

    FeedbackLine line;
    line.end = end;
    line.time_left = duration;
    line.owner = unit;
    line_list.push_back(line);
}

void MoveFeedbackSystem::on_update(float delta)
{
    auto& obj_set = WorldEntityMgr::instance()->get_object_set();

    for (auto it = line_list.begin(); it != line_list.end(); )
    {
        // 1. 绑定的单位已销毁 → 移除线条
        if (it->owner && obj_set.find(it->owner) == obj_set.end())
        {
            it = line_list.erase(it);
            continue;
        }

        // 2. 单位不再移动 → 移除线条
        if (it->owner)
        {
            auto* movable = it->owner->get_component<Movable>();
            if (!movable || !movable->is_moving())
            {
                it = line_list.erase(it);
                continue;
            }
        }

        // 3. 倒计时过期
        it->time_left -= delta;
        if (it->time_left <= 0.0f)
        {
            it = line_list.erase(it);
            continue;
        }

        ++it;
    }
}

void MoveFeedbackSystem::on_render()
{
    for (const auto& line : line_list)
    {
        if (!line.owner)   // 仅渲染有绑定的线条（无绑定则跳过，完全由绑定单位控制）
            continue;

        const CollisionBox& cb = line.owner->get_collision_box();
        Vector2 center = { cb.position.x + cb.width * 0.5f,
                           cb.position.y + cb.height * 0.5f };

        RenderCmd cmd;
        cmd.layer = RenderLayer::FeedbackLine;            // 在UI层绘制，保证在最上面
        cmd.color = { 255, 255, 255, 200 };    // 白色半透明
        cmd.is_line = true;
        cmd.line_start = center;                // 动态起点
        cmd.line_end = line.end;                // 固定终点

        RenderMgr::instance()->push_main_cmd(cmd);
    }
}