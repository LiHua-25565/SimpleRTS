#include "attack_system.h"
#include "world_entity_mgr.h"
#include "util.h"
#include "components.h"

void AttackSystem::on_update(float delta)
{
    auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : obj_pool)
    {
        if (!obj->check_valid()) continue;

        auto* attack = obj->get_component<Attack>();
        auto* movable = obj->get_component<Movable>();
        if (!attack || !movable) continue;

        // 提前获取单位碰撞盒（不依赖目标）
        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();

        // 通过 ID 获取目标实体
        uint64_t target_id = attack->target_id;
        GameObject* target = nullptr;
        if (target_id != 0) {
            target = WorldEntityMgr::instance()->get_object_by_id(target_id);
        }

        // 目标无效 → 清空并索敌
        auto* target_health = target ? target->get_component<Health>() : nullptr;
        if (!target || !target->check_valid() || (target_health && target_health->current_health <= 0))
        {
            Vector2 target_center = target ? target->get_collision_box().get_center_position() : unit_center;

            attack->target_id = 0;

            if (attack->auto_attack)
            {
                // 自动索敌
                int attacker_team_id = 0;
                auto* own = obj->get_component<Ownership>();
                if (own) attacker_team_id = own->team_id;

                GameObject* new_target = find_nearest_enemy(unit_center, attacker_team_id, 50.0f);
                if (!new_target)
                    new_target = find_nearest_enemy(target_center, attacker_team_id, 30.0f);

                if (new_target)
                {
                    attack->target_id = new_target->get_id();

                    const auto& new_box = new_target->get_collision_box();
                    Vector2 new_target_center = new_box.get_center_position();

                    if (attack->is_ranged)
                    {
                        float dist_to_new = (new_target_center - unit_center).length();
                        if (dist_to_new > attack->range)
                        {
                            movable->target = compute_ranged_outer_target(
                                unit_center, new_target_center, unit_box, attack->range, 5.0f);
                            movable->flow_target = movable->target;
                        }
                    }
                    else
                    {
                        movable->target = compute_outer_target(
                            unit_center, new_target_center, unit_box, new_box, 10.0f);
                        movable->flow_target = movable->target;
                    }
                }
            }
            continue;   // 目标无效，跳过后续攻击
        }

        // 目标有效，继续攻击逻辑
        const auto& target_box = target->get_collision_box();
        Vector2 target_center = target_box.get_center_position();

        Vector2 to_target = target_center - unit_center;
        float dist_to_target = to_target.length();
        if (dist_to_target < 0.01f) continue;
        Vector2 dir = to_target.normalize();

        // 攻击范围检测
        bool in_range = false;
        if (attack->is_ranged)
        {
            if (dist_to_target <= attack->range && is_line_passable(unit_center, target_center))
                in_range = true;
        }
        else
        {
            float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
            CollisionBox advanced_box = unit_box;
            advanced_box.position.x += dir.x * advance;
            advanced_box.position.y += dir.y * advance;
            if (advanced_box.intersects(target_box))
                in_range = true;
        }

        if (!in_range)
        {
            if (!movable->is_moving())
            {
                if (attack->is_ranged)
                    movable->target = compute_ranged_outer_target(unit_center, target_center, unit_box, attack->range);
                else
                    movable->target = compute_outer_target(unit_center, target_center, unit_box, target_box, 5.0f);
                movable->flow_target = movable->target;
            }
            continue;
        }

        // 攻击冷却
        attack->attack_pass_time += delta;
        if (attack->attack_pass_time >= attack->attack_interval)
        {
            attack->attack_pass_time = 0.0f;
            attack->can_attack = true;
        }
        if (!attack->can_attack) continue;

        attack->can_attack = false;

        if (attack->is_ranged)
        {
            // 生成投射物
            int player_id = 0;
            auto* own = obj->get_component<Ownership>();
            if (own) player_id = own->player_id;
            factory->set_player_id(player_id);
            factory->create_projectile_by_type(ProjectileType::Arrow, unit_center, target_center, attack->damage, target->get_id());
            // 触发反向 ImpactAnimation 模拟后坐力
            auto* anim = obj->get_component<ImpactAnimation>();
            if (anim && !anim->is_attacking) {
                anim->is_attacking = true;
                anim->anim_pass_time = 0.0f;
                anim->direction = (unit_center - target_center).normalize(); // 向后
                anim->impact_distance = 0.3f;  // 后坐力较小
                anim->anim_wait_time = 0.2f;   // 更快
            }
        }
        else
        {
            // 近战逻辑不变
            if (target_health->current_health <= attack->damage)
            {
                target_health->current_health = 0;
                attack->target_id = 0;
            }
            else
            {
                target_health->current_health -= attack->damage;
            }

            // 触发撞击动画
            auto* anim = obj->get_component<ImpactAnimation>();
            if (anim && !anim->is_attacking)
            {
                anim->is_attacking = true;
                anim->direction = dir; // dir 是单位指向资源的方向
            }
        }
    }
}

void AttackSystem::set_factory(ObjectFactory* factory)
{
    this->factory = factory;
}