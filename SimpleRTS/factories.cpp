#include "factories.h"

GameObject* ObjectFactory::create_unit(CollisionBox& collision_box)
{
	auto* obj = new GameObject(collision_box);

	obj->add_component<Renderable>()->color = Color::Red;
	obj->add_component<Selectable>();
	obj->add_component<Movable>();
	obj->add_component<UnitType>()->category = UnitCategory::Villager;

	WorldEntityMgr::instance()->insert_object(obj);
	return obj;
}