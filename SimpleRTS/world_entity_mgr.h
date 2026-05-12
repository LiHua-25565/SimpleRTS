#ifndef _WORLD_ENTITY_MGR_H_
#define _WORLD_ENTITY_MGR_H_

#include "quad_tree.h"
#include "game_object.h"
#include "game_map.h"

#include <vector>
#include <unordered_set>
#include <unordered_map>

class WorldEntityMgr
{
public:
	static WorldEntityMgr* instance();

	void init_world(GameMap* map);
	void on_update();

	void insert_object(GameObject* obj);
	void destroy_object(GameObject* obj);

	void query_area(const CollisionBox& area, std::vector<GameObject*>& out);

	GameObject* get_object_by_id(uint64_t);
	std::vector<GameObject*> get_object_by_id(std::vector<uint64_t>& id_list);
	std::vector<GameObject*> get_object_by_id(std::unordered_set<uint64_t>& id_set);

	const std::unordered_map<uint64_t, GameObject*>& get_object_pool() const;

	GameMap* get_map();

private:
	WorldEntityMgr();
	~WorldEntityMgr();
	uint64_t generate_id() { return next_id_++; };

private:
	uint64_t next_id_ = 1;	// id计数器，用于分配id
	QuadTree* quadtree = nullptr;
	GameMap* map = nullptr;
	std::unordered_map<uint64_t, GameObject*> object_pool;
};
#endif // !_WORLD_ENTITY_MGR_H_
