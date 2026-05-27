#include "attack_system.h"
#include "world_entity_mgr.h"
#include "util.h"
#include "components.h"

AttackSystem::AttackSystem()
{
    damage_pipeline.add_modifier(std::make_unique<ArmorModifier>());
}

void AttackSystem::on_update(float delta)
{
    // 获取所有游戏对象的引用
    auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();

    // 遍历每个对象，检查其攻击组件
    for (auto& [id, obj] : obj_pool)
    {
        // 跳过无效对象
        if (!obj->check_valid()) continue;

        // ---- 投射物处理 ----
        if (auto* proj = obj->get_component<Projectile>())
        {
            // 获取目标实体
            auto* target = WorldEntityMgr::instance()->get_object_by_id(proj->target_id);

            // 目标失效则销毁投射物
            if (!target || !target->check_valid())
            {
                WorldEntityMgr::instance()->destroy_object(obj);
                continue;
            }

            // 检测投射物是否命中目标（碰撞盒相交）
            if (obj->get_collision_box().intersects(target->get_collision_box()))
            {
                // 对目标造成伤害（投射物携带的已是最终伤害）
                auto* target_health = target->get_component<Health>();
                if (target_health)
                {
                    target_health->current_health -= proj->damage;
                    if (target_health->current_health <= 0)
                        target_health->current_health = 0;
                }

                // 命中后销毁投射物
                WorldEntityMgr::instance()->destroy_object(obj);
            }
            // 投射物跳过后续的单位攻击逻辑
            continue;
        }

        // 获取攻击和移动组件，如果没有则跳过
        auto* attack = obj->get_component<Attack>();
        auto* movable = obj->get_component<Movable>();
        if (!attack || !movable) continue;

        // 获取当前单位的碰撞盒和中心位置
        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();

        // 通过目标ID获取目标实体
        uint64_t target_id = attack->target_id;
        GameObject* target = nullptr;
        if (target_id != 0) {
            target = WorldEntityMgr::instance()->get_object_by_id(target_id);
        }

        // 获取目标的生命组件，检查目标是否有效
        auto* target_health = target ? target->get_component<Health>() : nullptr;
        if (!target || !target->check_valid() || (target_health && target_health->current_health <= 0))
        {
            // 记录目标中心位置（如果目标还存在）
            Vector2 target_center = target ? target->get_collision_box().get_center_position() : unit_center;

            // 清除当前攻击目标
            attack->target_id = 0;

            // 如果开启了自动攻击，尝试寻找新敌人
            if (attack->auto_attack)
            {
                // 获取攻击者的队伍ID
                int attacker_team_id = 0;
                auto* own = obj->get_component<Ownership>();
                if (own) attacker_team_id = own->team_id;

                // 在自身周围搜索敌人，优先50格，其次用原目标位置搜索30格
                GameObject* new_target = find_nearest_enemy(unit_center, attacker_team_id, 50.0f);
                if (!new_target)
                    new_target = find_nearest_enemy(target_center, attacker_team_id, 30.0f);

                // 如果找到新目标，更新攻击目标并设置移动位置
                if (new_target)
                {
                    attack->target_id = new_target->get_id();
                    const auto& new_box = new_target->get_collision_box();
                    Vector2 new_target_center = new_box.get_center_position();

                    // 根据攻击类型设置不同的移动目标
                    if (attack->is_ranged)
                    {
                        // 远程单位：如果超出射程，移动到射程边缘
                        float dist_to_new = (new_target_center - unit_center).length();
                        if (dist_to_new > attack->range)
                        {
                            movable->target = compute_ranged_outer_target(
                                unit_center, new_target_center, unit_box, new_target->get_collision_box(), attack->range, 5.0f);
                            movable->flow_target = movable->target;
                        }
                    }
                    else
                    {
                        // 近战单位：移动到目标附近
                        movable->target = compute_outer_target(
                            unit_center, new_target_center, unit_box, new_box, 10.0f);
                        movable->flow_target = movable->target;
                    }
                }
            }
            // 跳过当前帧的后续攻击逻辑
            continue;
        }

        // 目标有效，获取目标的碰撞盒和中心
        const auto& target_box = target->get_collision_box();
        Vector2 target_center = target_box.get_center_position();

        // 计算单位到目标的方向和距离
        Vector2 to_target = target_center - unit_center;
        float dist_to_target = to_target.length();
        if (dist_to_target < 0.01f) continue;  // 距离太近，跳过以避免异常方向
        Vector2 dir = to_target.normalize();

        // 判断是否在攻击范围内
        bool in_range = false;
        if (attack->is_ranged)
        {
            // 远程：检查距离和直线可见性
            float nearest_dist = rect_closest_distance(unit_center, target_box);
            if (nearest_dist <= attack->range && is_line_passable(unit_center, target_center, target))
                in_range = true;
        }
        else
        {
            // 近战：使用平移矩形检测，检查向前移动半边长是否能与目标重叠
            float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
            CollisionBox advanced_box = unit_box;
            advanced_box.position.x += dir.x * advance;
            advanced_box.position.y += dir.y * advance;
            if (advanced_box.intersects(target_box))
                in_range = true;
        }

        // 如果不在范围内，且单位没有正在移动，设置移动目标
        if (!in_range)
        {
            if (!movable->is_moving())
            {
                if (attack->is_ranged)
                    movable->target = compute_ranged_outer_target(unit_center, target_center, unit_box, target->get_collision_box(), attack->range);
                else
                    movable->target = compute_outer_target(unit_center, target_center, unit_box, target_box, 5.0f);
                movable->flow_target = movable->target;
            }
            continue;
        }

        // 更新攻击冷却计时器
        attack->attack_pass_time += delta;
        if (attack->attack_pass_time >= attack->attack_interval)
        {
            attack->attack_pass_time = 0.0f;
            attack->can_attack = true;
        }
        if (!attack->can_attack) continue;  // 冷却中，等待

        // 消耗本次攻击机会
        attack->can_attack = false;

        // 通过伤害流水线计算最终伤害（包含护甲、穿透等）
        int final_damage = damage_pipeline.calculate(attack->damage, obj, target);

        // 根据攻击类型执行不同的攻击动作
        if (attack->is_ranged)
        {
            // 远程攻击：创建投射物，携带最终伤害
            int player_id = 0;
            auto* own = obj->get_component<Ownership>();
            if (own) player_id = own->player_id;
            factory->set_player_id(player_id);

            factory->create_projectile_by_type(ProjectileType::Arrow, unit_center, target_center,
                final_damage, target->get_id());

            // 播放后坐力动画
            auto* anim = obj->get_component<ImpactAnimation>();
            if (anim && !anim->is_attacking) {
                anim->is_attacking = true;
                anim->anim_pass_time = 0.0f;
                anim->direction = (unit_center - target_center).normalize(); // 方向向后
                anim->impact_distance = 0.3f;
                anim->anim_wait_time = 0.2f;
            }
        }
        else
        {
            // 近战攻击：直接扣除目标生命值
            if (target_health->current_health <= final_damage)
            {
                target_health->current_health = 0;
                attack->target_id = 0;
            }
            else
            {
                target_health->current_health -= final_damage;
            }

            // 播放撞击动画
            auto* anim = obj->get_component<ImpactAnimation>();
            if (anim && !anim->is_attacking)
            {
                anim->is_attacking = true;
                anim->direction = dir; // 方向指向目标
            }
        }
    }
}

void AttackSystem::set_factory(ObjectFactory* factory)
{
    this->factory = factory;
}