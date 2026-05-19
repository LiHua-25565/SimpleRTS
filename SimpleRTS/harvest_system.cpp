#include "harvest_system.h"
#include "world_entity_mgr.h"
#include "util.h"

void HarvestSystem::on_update(float delta)
{
	auto& obj_pool = WorldEntityMgr::instance()->get_object_pool();
	for (auto& [id, obj] : obj_pool)
	{
		if (!obj->check_valid())
			continue;

		auto* gatherer = obj->get_component<Gatherer>();
		auto* movable = obj->get_component<Movable>();
		if (!gatherer || !movable) continue;

		GameObject* target_resource = gatherer->target_resource;
		if (!target_resource)
			continue;

		auto resource_harvestable = target_resource->get_component<Harvestable>();
		if (!resource_harvestable)
			continue;

		const auto& unit_box = obj->get_collision_box();
		Vector2 unit_center = unit_box.get_center_position();
		const auto& res_box = target_resource->get_collision_box();
		Vector2 res_center = res_box.get_center_position();

		// 计算朝向方向：从单位指向资源
		Vector2 to_res = res_center - unit_center;
		float dist_to_res = to_res.length();
		if (dist_to_res < 0.01f) continue;
		Vector2 dir = to_res.normalize();

		// ---- 新方法：前移矩形检测 ----
		// 将单位碰撞盒沿朝向方向平移一小段距离（半边长），然后检测是否与资源碰撞盒重叠
		float advance = std::max(unit_box.width, unit_box.height) * 0.5f;
		CollisionBox advanced_box = unit_box;
		advanced_box.position.x += dir.x * advance;
		advanced_box.position.y += dir.y * advance;

		if (!advanced_box.intersects(res_box))
			continue;

		gatherer->gather_pass_time += delta;
		if (gatherer->gather_pass_time >= gatherer->gather_interval)
		{
			gatherer->gather_pass_time = 0;
			gatherer->can_gather = true;
		}

		if (!gatherer->can_gather)
			continue;

		gatherer->can_gather = false;

		auto resource_type = resource_harvestable->output_type;
		if (gatherer->carried_type != resource_type)
		{
			gatherer->carried_type = resource_type;
			gatherer->carried_amount = 0;
		}

		bool need_to_find_dropoff = false;

		// 携带满:寻找最近提交点
		if (gatherer->carried_amount >= gatherer->carry_capacity)
			need_to_find_dropoff = true;

		auto animation = obj->get_component<ImpactAnimation>();
		if (!need_to_find_dropoff && animation && !animation->is_attacking)
		{
			animation->is_attacking = true;
			animation->target = target_resource;
		}

		int gather_amount = std::min(gatherer->gather_amount, gatherer->carry_capacity - gatherer->carried_amount);
		auto resource_health = target_resource->get_component<Health>();
		if (resource_health->current_health <= gather_amount)
		{
			// 资源枯竭：拿走所有剩余血量
			gatherer->carried_amount += resource_health->current_health;
			resource_health->current_health = 0;
			
			// 搜索同类型新资源
			float search_radius = 20.0f;
			GameObject* new_target = find_nearest_resource_of_type(resource_type, unit_center, search_radius);
			gatherer->target_resource = new_target;  // 可能为 nullptr
			if (new_target)
			{
				Vector2 new_res_center = new_target->get_collision_box().get_center_position();
				Vector2 dir_to_new = (unit_center - new_res_center);
				if (dir_to_new.length() < 0.01f) dir_to_new = { 1, 0 };
				dir_to_new = dir_to_new.normalize();
				float dist = unit_box.width * 0.5f + new_target->get_collision_box().width * 0.5f + 10.0f;
				movable->target = new_res_center + dir_to_new * dist;
				movable->flow_target = movable->target;
			}
			else
				need_to_find_dropoff = true;
		}
		else
		{
			gatherer->carried_amount += gather_amount;
			resource_health->current_health -= gather_amount;
		}

		if (need_to_find_dropoff)
		{
			GameObject* dropoff = gatherer->dropoff_target;
			if (!dropoff)
			{
				auto* ownership = obj->get_component<Ownership>();
				int player_id = ownership ? ownership->player_id : 0;
				dropoff = find_nearest_dropoff(unit_center, player_id, gatherer->carried_type, 50.0f);
			}

			if (dropoff)
			{
				gatherer->dropoff_target = dropoff;
				Vector2 build_center = dropoff->get_collision_box().get_center_position();
				movable->target = build_center;
				movable->flow_target = movable->target;
			}
		}
	}

}