#include "move_feedback_system.h"
#include "world_entity_mgr.h"
#include "render_mgr.h"

void MoveFeedbackSystem::add_line_for_unit(GameObject* unit, const Vector2& end, float duration)
{
    if (!unit) return;

    // 移除该单位已有的旧线（确保一个单位只有一条动态反馈线）
    for (auto it = line_list.begin(); it != line_list.end(); )
    {
        if (it->owner_id == unit->get_id())
            it = line_list.erase(it);
        else
            ++it;
    }

    FeedbackLine line;
    line.end = end;
    line.time_left = duration;
    line.owner_id = unit->get_id();
    line_list.push_back(line);
}

void MoveFeedbackSystem::on_update(float delta)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();

    for (auto it = line_list.begin(); it != line_list.end(); )
    {
        // 1. 绑定的单位已销毁 (ID不存在或虽存在但对象不匹配)
        if (it->owner_id != 0)
        {
            auto found = pool.find(it->owner_id);
            if (found == pool.end())
            {
                it = line_list.erase(it);
                continue;
            }
            // 对象存在，检查是否仍在移动
            auto* movable = found->second->get_component<Movable>();
            if (!movable || !movable->is_moving())
            {
                it = line_list.erase(it);
                continue;
            }
        }

        // 2. 倒计时过期
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
    auto& pool = WorldEntityMgr::instance()->get_object_pool();

    for (const auto& line : line_list)
    {
        if (line.owner_id == 0) continue;   // 无绑定

        auto found = pool.find(line.owner_id);
        if (found == pool.end()) continue;  // 单位已销毁

        GameObject* owner = found->second;
        const CollisionBox& cb = owner->get_collision_box();
        Vector2 center = cb.get_center_position();

        RenderCmd cmd;
        cmd.layer = RenderLayer::FeedbackLine;
        SDL_Color line_color = to_sdl_color(Color::White);
        line_color.a = 200;
        cmd.color = line_color;
        cmd.is_line = true;
        cmd.line_start = center;          // 动态起点
        cmd.line_end = line.end;        // 固定终点
        RenderMgr::instance()->push_main_cmd(cmd);
    }
}