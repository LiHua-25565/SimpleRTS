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
        if (!gatherer || !gatherer->dropoff_target || gatherer->carried_amount <= 0) 
            continue;

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

        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();
        const auto& build_box = building->get_collision_box();
        Vector2 build_center = build_box.get_center_position();

        // 朝向：从单位指向建筑中心
        Vector2 to_build = build_center - unit_center;
        float dist_to_build = to_build.length();
        if (dist_to_build < 0.01f) continue;
        Vector2 dir = to_build.normalize();

        // ---- 平移矩形检测 ----
        float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
        CollisionBox advanced_box = unit_box;
        advanced_box.position.x += dir.x * advance;
        advanced_box.position.y += dir.y * advance;

        if (!advanced_box.intersects(build_box))
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

        // 如果单位有目标资源，那么移动并采集
        GameObject* target_resource = gatherer->target_resource;
        if (target_resource)
        {
            const auto& res_box = target_resource->get_collision_box();
            Vector2 res_center = res_box.get_center_position();
            Vector2 dir_to_res = (unit_center - res_center);
            if (dir_to_res.length() < 0.01f) dir_to_res = { 1.0f, 0.0f };
            dir_to_res = dir_to_res.normalize();
            float dist = unit_box.width * 0.5f + res_box.width * 0.5f + 10.0f;
            movable->target = res_center + dir_to_res * dist;
            movable->flow_target = movable->target;
        }
        else
        {
            // 停止移动
            movable->target = { -1.0f, -1.0f };
            movable->flow_target = { -1.0f, -1.0f };
            movable->velocity = { 0.0f, 0.0f };
        }
        
    }
}