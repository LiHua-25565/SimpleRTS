#include "attack_system.h"
#include "world_entity_mgr.h"
#include "util.h"
#include "components.h"

// 暂时占位：寻找最近的敌方单位（后续可完善）
static GameObject* find_nearest_enemy(const Vector2& center, float radius) {
    // 目前直接返回 nullptr，表示不自动搜寻新目标
    return nullptr;
}

void AttackSystem::on_update(float delta)
{
    auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : obj_pool)
    {
        if (!obj->check_valid()) continue;

        auto* attack = obj->get_component<Attack>();
        auto* movable = obj->get_component<Movable>();
        if (!attack || !movable) continue;

        GameObject* target = attack->target;
        if (!target) continue;

        // 目标失效检查
        if (!target->check_valid()) {
            attack->target = nullptr;
            continue;
        }

        const auto& unit_box = obj->get_collision_box();
        Vector2 unit_center = unit_box.get_center_position();
        const auto& target_box = target->get_collision_box();
        Vector2 target_center = target_box.get_center_position();

        // 计算朝向方向：从单位指向目标
        Vector2 to_target = target_center - unit_center;
        float dist_to_target = to_target.length();
        if (dist_to_target < 0.01f) continue;
        Vector2 dir = to_target.normalize();

        // ---- 平移矩形检测 ----
        float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
        CollisionBox advanced_box = unit_box;
        advanced_box.position.x += dir.x * advance;
        advanced_box.position.y += dir.y * advance;

        if (!advanced_box.intersects(target_box))
            continue;

        // 攻击冷却
        attack->attack_pass_time += delta;
        if (attack->attack_pass_time >= attack->attack_interval)
        {
            attack->attack_pass_time = 0.0f;
            attack->can_attack = true;
        }

        if (!attack->can_attack) continue;

        attack->can_attack = false;

        // 触发攻击动画
        auto* animation = obj->get_component<ImpactAnimation>();
        if (animation && !animation->is_attacking)
        {
            animation->is_attacking = true;
            animation->target = target;
        }

        // 造成伤害
        auto* target_health = target->get_component<Health>();
        if (target_health)
        {
            // 如果目标血量不足一次攻击，直接杀死
            if (target_health->current_health <= attack->damage)
            {
                target_health->current_health = 0;
                attack->target = nullptr;

                // 尝试寻找新目标（暂时未实现）
                float search_radius = 30.0f;
                GameObject* new_target = find_nearest_enemy(unit_center, search_radius);
                attack->target = new_target;
            }
            else
            {
                target_health->current_health -= attack->damage;
            }
        }
    }
}