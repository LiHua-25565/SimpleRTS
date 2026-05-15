#include "selection_mgr.h"
#include "world_entity_mgr.h"

#include <vector>

SelectionMgr* SelectionMgr::instance()
{
	static SelectionMgr mgr;
	return &mgr;
}

SelectionMgr::SelectionMgr() = default;
SelectionMgr::~SelectionMgr() = default;

void SelectionMgr::clear()
{
	std::vector<GameObject*>object_list = WorldEntityMgr::instance()->get_object_by_id(selected_object_id_set);
	for (GameObject* obj : object_list)
	{
		auto selectable = obj->get_component<Selectable>();
		if (selectable)
			selectable->is_selected = false;
	}

	selected_object_id_set.clear();
}

void SelectionMgr::select_single(GameObject* obj)
{
	if (!obj) return;

	if (current_mode == SelectMode::Normal)
	{
		clear();
	}

	auto* selectable = obj->get_component<Selectable>();
	if (!selectable) return;

	// 减选模式
	if (current_mode == SelectMode::Remove)
	{
		selectable->is_selected = false;
		selected_object_id_set.erase(obj->get_id());
		return;
	}

	// 普通 / 加选
	selectable->is_selected = true;
	selected_object_id_set.insert(obj->get_id());
}

void SelectionMgr::select_in_area(const CollisionBox& world_area)
{
	// 从四叉树获取区域内所有实体
	std::vector<GameObject*> result;
	WorldEntityMgr::instance()->query_area(world_area, result);

	// 普通模式：先清空
	if (current_mode == SelectMode::Normal)
	{
		clear();
	}

	for (auto* obj : result)
	{
		auto* selectable = obj->get_component<Selectable>();
		if (!selectable) continue;

		// 框选时只选中己方单位
		auto* ownership = obj->get_component<Ownership>();
		auto* unit_type = obj->get_component<UnitType>();
		if (!ownership || ownership->player_id != local_player_id || !unit_type)
			continue;

		const CollisionBox& obj_box = obj->get_collision_box();

		if (world_area.intersects(obj_box))
		{
			if (current_mode == SelectMode::Remove)
			{
				selectable->is_selected = false;
				selected_object_id_set.erase(obj->get_id());
			}
			else
			{
				selectable->is_selected = true;
				selected_object_id_set.insert(obj->get_id());
			}
		}
	}
}

bool SelectionMgr::select_at_point(const Vector2& world_pos)
{
	CollisionBox click_area{ world_pos, 1.0f, 1.0f };
	std::vector<GameObject*> result;
	WorldEntityMgr::instance()->query_area(click_area, result);

	// 过滤出有 Selectable 组件且碰撞盒包含该点的单位
	GameObject* hit = nullptr;
	for (auto* obj : result)
	{
		if (obj->get_component<Selectable>() && obj->get_collision_box().contains_point(world_pos))
		{
			hit = obj;
			break;   // 只取第一个
		}
	}

	if (hit)
	{
		select_single(hit);
		return true;
	}

	// 没点到任何单位
	return false;
}

void SelectionMgr::set_select_mode(SelectMode mode)
{
	if (mode == SelectMode::Normal || current_mode == SelectMode::Normal)
		current_mode = mode;
}

const SelectionMgr::SelectMode& SelectionMgr::get_current_mode() const
{
	return current_mode;
}

void SelectionMgr::set_local_player_id(int player_id) 
{ 
	local_player_id = player_id; 
}

int SelectionMgr::get_local_player_id() const 
{ 
	return local_player_id;
}