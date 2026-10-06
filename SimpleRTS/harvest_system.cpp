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

        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();

        // ===== 目标死亡/失效的恢复 =====
        // 农民在赶往资源的路上目标被采空/摧毁时，以前只是清掉任务、让单位
        // 继续走向尸体然后原地发呆。现在：背着货就改道去提交点；空手就
        // 自动换矿（同类型优先，其次任意活矿）；全图无矿才停下
        if (!target_resource || !target_resource->check_valid())
        {
            gatherer->target_resource_id = 0;
            if (gatherer->carried_amount > 0)
            {
                // 背着货：改道去提交点，别抱着货走向尸体
                GameObject* dropoff = nullptr;
                uint64_t did = gatherer->dropoff_target_id;
                if (did != 0)
                    dropoff = WorldEntityMgr::instance()->get_object_by_id(did);
                if (!dropoff || !dropoff->check_valid())
                {
                    auto* own = obj->get_component<Ownership>();
                    int pid = own ? own->player_id : 0;
                    dropoff = find_nearest_dropoff(unit_center, pid, gatherer->carried_type, 50.0f);
                    gatherer->dropoff_target_id = dropoff ? dropoff->get_id() : 0;
                }
                if (dropoff)
                {
                    gatherer->stand_target = compute_perimeter_target(
                        obj->get_id(), unit_center, unit_box,
                        dropoff->get_collision_box(), 2.0f, movable->target);
                    movable->target = gatherer->stand_target;
                    movable->flow_target = movable->target;
                }
                else
                {
                    // 没有提交点：就地停下，至少别走向尸体
                    movable->target = { -1.0f, -1.0f };
                    movable->flow_target = { -1.0f, -1.0f };
                }
            }
            else
            {
                // 空手：自动换矿（同类型优先，其次任意类型）
                GameObject* alt = nullptr;
                if (gatherer->carried_type != ResourceType::None)
                    alt = find_nearest_resource_of_type(gatherer->carried_type, unit_center, 60.0f);
                if (!alt) alt = find_nearest_any_resource(unit_center, 60.0f);
                if (alt)
                {
                    gatherer->target_resource_id = alt->get_id();
                    gatherer->stand_target = compute_perimeter_target(
                        obj->get_id(), unit_center, unit_box,
                        alt->get_collision_box(), 2.0f);
                    movable->target = gatherer->stand_target;
                    movable->flow_target = movable->target;
                }
                else
                {
                    movable->target = { -1.0f, -1.0f };
                    movable->flow_target = { -1.0f, -1.0f };
                }
            }
            continue;
        }

        auto* resource_harvestable = target_resource->get_component<Harvestable>();
        if (!resource_harvestable)
        {
            gatherer->target_resource_id = 0;
            continue;
        }

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
        const bool adjacent = advanced_box.intersects(res_box);

        // ===== 拥堵自愈（采集侧） =====
        // 情形 1：被同伴挤离站位点后原地停着（is_moving == false，被推出去后
        //   RVO 早就判定到达了）——立即走回自己的站位点（stand_target），
        //   而不是换新点：换新点会把本来稳定的队伍搅成抢座位的循环。
        // 情形 2：目标被占/被 RVO 顶住，长时间（2s 窗口）向目标的推进不足
        //   5px——换一个"最宽敞"的站位点，围着资源散开。
        // 用"推进量"而不是瞬时速度判断：被顶住时单位会来回推挤，速度并不低
        if (!adjacent && gatherer->carried_amount < gatherer->carry_capacity)
        {
            bool need_new_target = !movable->is_moving();
            if (movable->is_moving())
            {
                if (gatherer->blocked_time <= 0.0f)
                    gatherer->last_target_dist = (movable->target - unit_center).length();
                gatherer->blocked_time += delta;
                if (gatherer->blocked_time >= 2.0f)
                {
                    const float d = (movable->target - unit_center).length();
                    const float progress = gatherer->last_target_dist - d;
                    if (progress < 5.0f)
                    {
                        need_new_target = true;
                        gatherer->blocked_time = 0.0f;
                    }
                    else
                    {
                        // 推进正常：刷新快照，进入下一个 2s 窗口
                        gatherer->last_target_dist = d;
                        gatherer->blocked_time = 0.0f;
                    }
                }
            }

            if (need_new_target)
            {
                Vector2 nt;
                // 被挤离的闲置单位：走回自己的站位点（大概率就在几像素外）。
                // 行进中被堵的单位：当前目标就是自己的站位点，走回去等于
                // 原地踏步，必须重新挑一个“最宽敞”的槽位
                if (!movable->is_moving() && gatherer->stand_target.x >= 0.0f &&
                    (gatherer->stand_target - unit_center).length() < 40.0f)
                {
                    nt = gatherer->stand_target;
                }
                else
                {
                    nt = compute_perimeter_target(
                        obj->get_id(), unit_center, unit_box, res_box, 2.0f, movable->target);
                    gatherer->stand_target = nt;
                }
                movable->target = nt;
                movable->flow_target = nt;
                gatherer->blocked_time = 0.0f;
            }
            continue;
        }

        // ===== 拥堵自愈（送货侧） =====
        // 满载走向提交建筑时被堵：同样的推进量检测，换一个提交站位点
        if (gatherer->dropoff_target_id != 0 && movable->is_moving())
        {
            GameObject* dropoff = WorldEntityMgr::instance()->get_object_by_id(gatherer->dropoff_target_id);
            if (dropoff && dropoff->check_valid())
            {
                const auto& build_box = dropoff->get_collision_box();
                Vector2 to_build = build_box.get_center_position() - unit_center;
                Vector2 build_dir = to_build.normalize();
                CollisionBox adv_build = unit_box;
                adv_build.position.x += build_dir.x * advance;
                adv_build.position.y += build_dir.y * advance;
                if (!adv_build.intersects(build_box))
                {
                    if (gatherer->blocked_time <= 0.0f)
                        gatherer->last_target_dist = (movable->target - unit_center).length();
                    gatherer->blocked_time += delta;
                    if (gatherer->blocked_time >= 2.0f)
                    {
                        const float d = (movable->target - unit_center).length();
                        const float progress = gatherer->last_target_dist - d;
                        if (progress < 5.0f)
                        {
                            gatherer->stand_target = compute_perimeter_target(
                                obj->get_id(), unit_center, unit_box, build_box, 2.0f, movable->target);
                            movable->target = gatherer->stand_target;
                            movable->flow_target = movable->target;
                        }
                        else
                        {
                            gatherer->last_target_dist = d;
                        }
                        gatherer->blocked_time = 0.0f;
                    }
                }
            }
        }

        if (!adjacent) continue;
        gatherer->blocked_time = 0.0f;

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

            if (gatherer->carried_amount >= gatherer->carry_capacity)
            {
                // 装满了：优先交货，别背着货赶往下一个矿白跑一趟
                gatherer->target_resource_id = 0;
                need_to_find_dropoff = true;
            }
            else
            {
                // 搜索同类型新资源（60 格），没有就换任意活矿；
                // 全图无矿才放下任务去交手里的零头
                float search_radius = 60.0f;
                GameObject* new_target = find_nearest_resource_of_type(resource_type, unit_center, search_radius);
                if (!new_target)
                    new_target = find_nearest_any_resource(unit_center, search_radius);
                if (new_target)
                {
                    gatherer->target_resource_id = new_target->get_id();
                    gatherer->stand_target = compute_perimeter_target(
                        obj->get_id(), unit_center, unit_box,
                        new_target->get_collision_box(), 2.0f);
                    movable->target = gatherer->stand_target;
                    movable->flow_target = movable->target;
                }
                else
                {
                    gatherer->target_resource_id = 0;
                    need_to_find_dropoff = true;
                }
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
                // 提交站位同样分散到建筑周界，避免满载农民全挤在建筑同一侧
                gatherer->stand_target = compute_perimeter_target(
                    obj->get_id(), unit_center, unit_box,
                    dropoff->get_collision_box(), 2.0f, movable->target);
                movable->target = gatherer->stand_target;
                movable->flow_target = movable->target;
            }
        }
    }
}