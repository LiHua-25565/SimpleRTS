#include "world_entity_mgr.h"
#include "rvo_adapter.h"
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
    quadtree = new QuadTree(0, { 0,0 }, map->get_width(), map->get_height(), map->get_cell_size());
}

void WorldEntityMgr::on_update()
{
    if (!quadtree || !map)
    {
        MessageBoxA(NULL, "错误：WorldEntityMgr 未初始化！", "WorldEntityMgr 错误", MB_ICONERROR | MB_OK);
        return;
    }

    for (auto it = object_pool.begin(); it != object_pool.end(); )
    {
        GameObject* object = it->second;

        // 如果对象血量为0，就设置为失效
        auto* health = object->get_component<Health>();
        if (health && health->current_health <= 0)
            object->set_valid(false);

        // 如果对象失效就删除
        if (!object->check_valid())
        {
            quadtree->remove(object->get_id());
            it = object_pool.erase(it);
            delete object;
            continue;
        }

        if (object->check_dirty())
        {
            quadtree->remove(object->get_id());
            quadtree->insert(object->get_id());
            object->clear_dirty();
        }
        ++it;
    }
}

void WorldEntityMgr::insert_object(GameObject* obj)
{
    if (!quadtree || !obj)
    {
        MessageBoxA(NULL, "错误：WorldEntityMgr 未初始化！", "WorldEntityMgr 错误", MB_ICONERROR | MB_OK);
        return;
    }

    if (obj->get_id() == 0)
        obj->set_id(generate_id());

    object_pool.insert_or_assign(obj->get_id(), obj);
    quadtree->insert(obj->get_id());
    obj->clear_dirty();
}

void WorldEntityMgr::destroy_object(GameObject* obj)
{
    if (!obj || !obj->check_valid()) return;

    obj->set_valid(false);

    if (quadtree)
        quadtree->remove(obj->get_id());

    RVOAdapter::instance()->request_rebuild();
}

void WorldEntityMgr::query_area(const CollisionBox& area, std::vector<GameObject*>& out)
{
    out.clear();
    if (!quadtree) return;

    std::vector<uint64_t> ids;
    quadtree->retrieve(ids, area);

    for (uint64_t id : ids)
    {
        GameObject* obj = get_object_by_id(id);
        if (obj)
            out.push_back(obj);
    }
}

const std::unordered_map<uint64_t, GameObject*>& WorldEntityMgr::get_object_pool() const
{
    return object_pool;
}

GameObject* WorldEntityMgr::get_object_by_id(uint64_t id)
{
    auto it = object_pool.find(id);
    if (it != object_pool.end() && it->second->check_valid())
        return it->second;
    return nullptr;
}

std::vector<GameObject*> WorldEntityMgr::get_object_by_id(std::vector<uint64_t>& id_list)
{
    std::vector<GameObject*> obj_list;
    std::vector<uint64_t> valid_ids;
    valid_ids.reserve(id_list.size());
    obj_list.reserve(id_list.size());

    for (uint64_t id : id_list)
    {
        auto it = object_pool.find(id);
        if (it != object_pool.end() && it->second->check_valid())
        {
            obj_list.push_back(it->second);
            valid_ids.push_back(id);
        }
    }

    id_list.swap(valid_ids);
    return obj_list;
}

std::vector<GameObject*> WorldEntityMgr::get_object_by_id(std::unordered_set<uint64_t>& id_set)
{
    std::vector<GameObject*> obj_list;
    obj_list.reserve(id_set.size());

    for (auto it = id_set.begin(); it != id_set.end(); )
    {
        auto found = object_pool.find(*it);
        if (found != object_pool.end() && found->second->check_valid())
        {
            obj_list.push_back(found->second);
            ++it;
        }
        else
            it = id_set.erase(it);
    }

    return obj_list;
}

GameMap* WorldEntityMgr::get_map()
{
    return map;
}