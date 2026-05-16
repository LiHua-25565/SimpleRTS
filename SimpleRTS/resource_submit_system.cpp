#include "resource_submit_system.h"
#include "world_entity_mgr.h"
#include "resources_mgr.h"
#include "components.h"

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

        // 检查建筑是否仍有 ResourceDropoff 组件
        auto* dropoff = building->get_component<ResourceDropoff>();
        if (!dropoff) {
            gatherer->dropoff_target = nullptr;
            continue;
        }

        // 计算两个碰撞盒的最小距离
        const auto& unit_box = obj->get_collision_box();
        const auto& building_box = building->get_collision_box();

        // 矩形最小距离（轴对齐）
        float dx = std::max(unit_box.position.x - (building_box.position.x + building_box.width),
            building_box.position.x - (unit_box.position.x + unit_box.width));
        dx = std::max(0.0f, dx);
        float dy = std::max(unit_box.position.y - (building_box.position.y + building_box.height),
            building_box.position.y - (unit_box.position.y + unit_box.height));
        dy = std::max(0.0f, dy);
        float min_dist = std::sqrt(dx * dx + dy * dy);

        if (min_dist > 10.0f) continue; // 允许10像素误差

        // 检查掩码
        uint8_t mask = 1 << (static_cast<int>(gatherer->carried_type) - 1);
        if (!(dropoff->accept_mask & mask)) {
            // 建筑不接受此资源，取消提交
            gatherer->dropoff_target = nullptr;
            continue;
        }

        // 提交资源
        int player_id = 0; // 从单位所有权获取
        auto* ownership = obj->get_component<Ownership>();
        if (ownership) player_id = ownership->player_id;
        ResourcesMgr::instance()->add_resource(player_id, gatherer->carried_type, gatherer->carried_amount);
        gatherer->carried_amount = 0;
        gatherer->carried_type = ResourceType::None;
        gatherer->dropoff_target = nullptr;

        // 可选：停止单位移动（如果之前是移动过来的）
        auto* movable = obj->get_component<Movable>();
        if (movable) {
            movable->target = { -1.0f, -1.0f };
            movable->flow_target = { -1.0f, -1.0f };
        }
    }
}