#ifndef _WORLD_ENTITY_MGR_H_
#define _WORLD_ENTITY_MGR_H_

#include "quad_tree.h"
#include "game_object.h"
#include "game_map.h"
#include <vector>
#include <unordered_set>

class WorldEntityMgr
{
public:
	static WorldEntityMgr* instance();

	void init_world(GameMap* map);
	void on_update();

	void insert_object(GameObject* obj);
	void destroy_object(GameObject* obj);

	void query_area(const CollisionBox& area, std::vector<GameObject*>& out);
	const std::unordered_set<GameObject*>& get_object_set() const
	{
		return object_set;
	}

	GameMap* get_map()
	{
		return map;
	}

private:
	WorldEntityMgr();
	~WorldEntityMgr();

private:

	QuadTree* quadtree = nullptr;
	GameMap* map = nullptr;
	std::unordered_set<GameObject*> object_set;
};
#endif // !_WORLD_ENTITY_MGR_H_
