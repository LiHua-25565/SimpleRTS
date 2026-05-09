#include "world_entity_mgr.h"
#include "Windows.h"

WorldEntityMgr* WorldEntityMgr::instance()
{
	static WorldEntityMgr mgr;
	
	return &mgr;
}

WorldEntityMgr::WorldEntityMgr() = default;

WorldEntityMgr::~WorldEntityMgr()
{
	if (quadtree)
		delete quadtree;
}

void WorldEntityMgr::init_world(GameMap* map)
{
	if (!map) 
	{
		MessageBoxA(NULL, "错误：init_world(map) 传入的 GameMap 为空！", "初始化错误", MB_ICONERROR);
		return;
	}

	this->map = map;

	quadtree = new QuadTree(0,{0,0},map->get_width(), map->get_height(), map->get_cell_size());
}

void WorldEntityMgr::on_update()
{
	if (!quadtree || !map)
	{
		MessageBoxA(
			NULL,
			"错误：WorldEntityMgr 未初始化！\n请先调用 init_world() 再进行查询。",
			"WorldEntityMgr 错误",
			MB_ICONERROR | MB_OK
		);

		return;
	}

	for (GameObject* object : object_set)
	{
		if (object->check_dirty())
		{
			quadtree->remove(object);
			quadtree->insert(object);
			object->clear_dirty();
		}
	}
}

void WorldEntityMgr::insert_object(GameObject* obj)
{
	if (!quadtree || !obj)
	{
		MessageBoxA(
			NULL,
			"错误：WorldEntityMgr 未初始化！\n请先调用 init_world() 再进行查询。",
			"WorldEntityMgr 错误",
			MB_ICONERROR | MB_OK
		);

		return;
	}

	object_set.insert(obj);
	quadtree->insert(obj);
	obj->clear_dirty();
}

void WorldEntityMgr::destroy_object(GameObject* obj)
{
	if (!quadtree || !obj)
	{
		MessageBoxA(
			NULL,
			"错误：WorldEntityMgr 未初始化！\n请先调用 init_world() 再进行查询。",
			"WorldEntityMgr 错误",
			MB_ICONERROR | MB_OK
		);

		return;
	}

	// 先从四叉树删掉
	if (quadtree) {
		quadtree->remove(obj);
	}

	// 再从实体列表删掉
	object_set.erase(obj);

	// 最后销毁
	delete obj;
}

void WorldEntityMgr::query_area(const CollisionBox& area, std::vector<GameObject*>& out)
{
	if (!quadtree)
	{
		MessageBoxA(
			NULL,
			"错误：WorldEntityMgr 未初始化！\n请先调用 init_world() 再进行查询。",
			"WorldEntityMgr 错误",
			MB_ICONERROR | MB_OK
		);

		out.clear();
		return;
	}

	out.clear();
	if (quadtree)
		quadtree->retrieve(out,area);
}

