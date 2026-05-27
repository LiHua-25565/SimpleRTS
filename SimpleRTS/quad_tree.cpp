#include "quad_tree.h"
#include "world_entity_mgr.h"   // 通过ID获取碰撞盒需要访问实体管理器

// 辅助函数：通过ID获取实体碰撞盒（无效ID返回空碰撞盒）
static CollisionBox get_box_by_id(uint64_t id)
{
    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
    if (!obj || !obj->check_valid())
        return CollisionBox();   // 无效实体返回空盒（宽高为0）
    return obj->get_collision_box();
}

// 节点区域（格子）与对象碰撞盒（世界）的相交检测
bool QuadTree::intersects(const CollisionBox& rect) const
{
    float left1 = position.x * cell_size;
    float right1 = (position.x + width) * cell_size;
    float top1 = position.y * cell_size;
    float bottom1 = (position.y + height) * cell_size;

    float left2 = rect.position.x;
    float right2 = rect.position.x + rect.width;
    float top2 = rect.position.y;
    float bottom2 = rect.position.y + rect.height;

    return left1 < right2 && right1 > left2 && top1 < bottom2 && bottom1 > top2;
}

QuadTree::QuadTree(int depth, Vector2 position, int width, int height, int cell_size)
    : depth(depth), position(position), width(width), height(height), cell_size(cell_size)
{
    nodes[0] = nullptr;
    nodes[1] = nullptr;
    nodes[2] = nullptr;
    nodes[3] = nullptr;
}

QuadTree::~QuadTree()
{
    clear();
}

void QuadTree::clear()
{
    object_ids.clear();
    for (int i = 0; i < 4; i++)
    {
        if (nodes[i])
            delete nodes[i];
        nodes[i] = nullptr;
    }
}

void QuadTree::split()
{
    if (width < 2 || height < 2)
        return;

    int half_w = width / 2;
    int half_h = height / 2;

    nodes[0] = new QuadTree(depth + 1, Vector2(position.x, position.y), half_w, half_h, cell_size);
    nodes[1] = new QuadTree(depth + 1, Vector2(position.x + half_w, position.y), half_w, half_h, cell_size);
    nodes[2] = new QuadTree(depth + 1, Vector2(position.x, position.y + half_h), half_w, half_h, cell_size);
    nodes[3] = new QuadTree(depth + 1, Vector2(position.x + half_w, position.y + half_h), half_w, half_h, cell_size);
}

int QuadTree::get_index(const CollisionBox& rect) const
{
    float mid_x = (position.x + width * 0.5f) * cell_size;
    float mid_y = (position.y + height * 0.5f) * cell_size;

    bool top = rect.position.y + rect.height <= mid_y;
    bool bottom = rect.position.y >= mid_y;
    bool left = rect.position.x + rect.width <= mid_x;
    bool right = rect.position.x >= mid_x;

    if (top && left)   return 0;
    if (top && right)  return 1;
    if (bottom && left)return 2;
    if (bottom && right)return 3;

    return -1;
}

bool QuadTree::remove(uint64_t entity_id)
{
    // 获取实体碰撞盒
    CollisionBox box = get_box_by_id(entity_id);

    // 如果实体无效，或者与当前节点区域不相交，直接返回 false
    if (!intersects(box))
        return false;

    // 在当前节点的ID列表中搜索
    for (auto it = object_ids.begin(); it != object_ids.end(); ++it)
    {
        if (*it == entity_id)
        {
            object_ids.erase(it);
            return true;
        }
    }

    // 递归到子节点
    if (nodes[0])
    {
        for (int i = 0; i < 4; ++i)
        {
            if (nodes[i]->remove(entity_id))
                return true;
        }
    }
    return false;
}

void QuadTree::insert(uint64_t entity_id)
{
    // 获取碰撞盒
    CollisionBox box = get_box_by_id(entity_id);
    if (box.width <= 0.0f) return;   // 无效实体不插入

    // 如果与当前区域不相交，不插入
    if (!intersects(box)) return;

    // 尝试放入子节点
    if (nodes[0])
    {
        int idx = get_index(box);
        if (idx != -1)
        {
            nodes[idx]->insert(entity_id);
            return;
        }
    }

    // 子节点不存在或者无法完全放入，放在当前节点
    object_ids.push_back(entity_id);

    // 检查是否需要分裂
    if (object_ids.size() > QUADTREE_MAX_OBJECTS && depth < QUADTREE_MAX_DEPTH)
    {
        if (!nodes[0]) split();

        // 分裂成功后，尝试将本节点的对象下放
        if (nodes[0])
        {
            int i = 0;
            while (i < (int)object_ids.size())
            {
                // 使用碰撞盒判断归属
                CollisionBox cur_box = get_box_by_id(object_ids[i]);
                int idx = get_index(cur_box);
                if (idx != -1)
                {
                    nodes[idx]->insert(object_ids[i]);
                    object_ids.erase(object_ids.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        }
    }
}

void QuadTree::retrieve(std::vector<uint64_t>& return_ids, const CollisionBox& area) const
{
    if (!intersects(area))
        return;

    // 添加当前节点的所有ID
    for (uint64_t id : object_ids)
        return_ids.push_back(id);

    // 递归子节点
    if (nodes[0])
    {
        for (int i = 0; i < 4; ++i)
            nodes[i]->retrieve(return_ids, area);
    }
}