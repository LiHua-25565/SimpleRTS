#include "harvest_system.h"
#include "world_entity_mgr.h"
#include "util.h"

void HarvestSystem::on_update(float delta)
{
    auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : obj_pool)
    {
        if (!obj->check_valid()) continue;

        auto* gatherer = obj->get_component<Gatherer>();
        auto* movable = obj->get_component<Movable>();
        if (!gatherer || !movable) continue;

        // 通过 ID 获取采集目标
        uint64_t res_id = gatherer->target_resource_id;
        if (res_id == 0) continue;
        GameObject* target_resource = WorldEntityMgr::instance()->get_object_by_id(res_id);
        if (!target_resource || !target_resource->check_valid())
        {
            gatherer->target_resource_id = 0;
            continue;
        }

        auto* resource_harvestable = target_resource->get_component<Harvestable>();
        if (!resource_harvestable)
        {
            gatherer->target_resource_id = 0;
            continue;
        }

        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();
        const auto& res_box = target_resource->get_collision_box();
        Vector2 res_center = res_box.get_center_position();

        Vector2 to_res = res_center - unit_center;
        float dist_to_res = to_res.length();
        if (dist_to_res < 0.01f) continue;
        Vector2 dir = to_res.normalize();

        // 前移矩形检测
        float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
        CollisionBox advanced_box = unit_box;
        advanced_box.position.x += dir.x * advance;
        advanced_box.position.y += dir.y * advance;

        if (!advanced_box.intersects(res_box)) continue;

        // 采集冷却
        gatherer->gather_pass_time += delta;
        if (gatherer->gather_pass_time >= gatherer->gather_interval)
        {
            gatherer->gather_pass_time = 0;
            gatherer->can_gather = true;
        }
        if (!gatherer->can_gather) continue;
        gatherer->can_gather = false;

        auto resource_type = resource_harvestable->output_type;
        if (gatherer->carried_type != resource_type)
        {
            gatherer->carried_type = resource_type;
            gatherer->carried_amount = 0;
        }

        bool need_to_find_dropoff = false;

        // 携带满
        if (gatherer->carried_amount >= gatherer->carry_capacity)
            need_to_find_dropoff = true;

        // 播放动画（存储目标 ID）
        auto* anim = obj->get_component<ImpactAnimation>();
        if (!need_to_find_dropoff && anim && !anim->is_attacking)
        {
            anim->is_attacking = true;
            anim->direction = dir; // dir 是单位指向目标的方向
        }

        int gather_amount = std::min(gatherer->gather_amount, gatherer->carry_capacity - gatherer->carried_amount);
        auto* resource_health = target_resource->get_component<Health>();
        if (resource_health->current_health <= gather_amount)
        {
            // 资源枯竭
            gatherer->carried_amount += resource_health->current_health;
            resource_health->current_health = 0;

            // 搜索同类型新资源
            float search_radius = 20.0f;
            GameObject* new_target = find_nearest_resource_of_type(resource_type, unit_center, search_radius);
            if (new_target)
            {
                gatherer->target_resource_id = new_target->get_id();
                movable->target = compute_outer_target(
                    unit_center,
                    new_target->get_collision_box().get_center_position(),
                    unit_box,
                    new_target->get_collision_box(),
                    10.0f
                );
                movable->flow_target = movable->target;
            }
            else
            {
                gatherer->target_resource_id = 0;
                need_to_find_dropoff = true;
            }
        }
        else
        {
            gatherer->carried_amount += gather_amount;
            resource_health->current_health -= gather_amount;
        }

        if (need_to_find_dropoff)
        {
            uint64_t dropoff_id = gatherer->dropoff_target_id;
            GameObject* dropoff = nullptr;
            if (dropoff_id != 0)
                dropoff = WorldEntityMgr::instance()->get_object_by_id(dropoff_id);

            if (!dropoff || !dropoff->check_valid())
            {
                auto* ownership = obj->get_component<Ownership>();
                int player_id = ownership ? ownership->player_id : 0;
                dropoff = find_nearest_dropoff(unit_center, player_id, gatherer->carried_type, 50.0f);
                if (dropoff)
                    gatherer->dropoff_target_id = dropoff->get_id();
                else
                    gatherer->dropoff_target_id = 0;
            }

            if (dropoff)
            {
                Vector2 build_center = dropoff->get_collision_box().get_center_position();
                movable->target = compute_outer_target(unit_center, build_center, unit_box, dropoff->get_collision_box(), 10.0f);
                movable->flow_target = movable->target;
            }
        }
    }
}