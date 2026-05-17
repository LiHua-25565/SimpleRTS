#include "resource_submit_system.h"
#include "world_entity_mgr.h"
#include "resources_mgr.h"
#include "components.h"
#include "util.h"

void ResourceSubmitSystem::on_update(float delta)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;

        auto* gatherer = obj->get_component<Gatherer>();
        if (!gatherer) continue;
        if (!gatherer->dropoff_target || gatherer->carried_amount <= 0) continue;

        auto* building = gatherer->dropoff_target;
        if (!building->check_valid()) {
            gatherer->dropoff_target = nullptr;
            continue;
        }
        auto* dropoff = building->get_component<ResourceDropoff>();
        if (!dropoff) {
            gatherer->dropoff_target = nullptr;
            continue;
        }

        auto* movable = obj->get_component<Movable>();
        if (!movable) continue;

        // 获取速度方向（静止时跳过）
        Vector2 vel = movable->velocity;
        if (vel.length() < 0.01f) continue;
        Vector2 dir = vel.normalize();

        const auto& unit_box = obj->get_collision_box();
        Vector2 center = unit_box.get_center_position();
        float ray_len = std::max(unit_box.width, unit_box.height) * 0.5f + 10.0f;

        // 射线碰撞检测
        if (!ray_intersects_box(center, dir, ray_len, building->get_collision_box()))
            continue;

        // 检查掩码
        uint8_t mask = 1 << (static_cast<int>(gatherer->carried_type) - 1);
        if (!(dropoff->accept_mask & mask)) {
            gatherer->dropoff_target = nullptr;
            continue;
        }

        // 提交资源
        int player_id = 0;
        auto* ownership = obj->get_component<Ownership>();
        if (ownership) player_id = ownership->player_id;
        ResourcesMgr::instance()->add_resource(player_id, gatherer->carried_type, gatherer->carried_amount);
        gatherer->carried_amount = 0;
        gatherer->carried_type = ResourceType::None;
        gatherer->dropoff_target = nullptr;

        // 停止移动
        movable->target = { -1.0f, -1.0f };
        movable->flow_target = { -1.0f, -1.0f };
        movable->velocity = { 0.0f, 0.0f };
    }
}