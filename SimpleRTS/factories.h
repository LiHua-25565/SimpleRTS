#ifndef _FACTORIES_H_
#define _FACTORIES_H_

#include "game_object.h"
#include "world_entity_mgr.h"

class ObjectFactory
{
public:
	GameObject* create_unit(CollisionBox& collision_box);

};
#endif // !_FACTORIES_H_
