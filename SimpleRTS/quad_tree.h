#ifndef _QUAD_TREE_H_
#define _QUAD_TREE_H_

#include "vector2.h"
#include "collision_box.h"   // 只需要碰撞盒定义，不再需要 game_object.h
#include <vector>
#include <cstdint>

class QuadTree
{
public:
    QuadTree(int depth, Vector2 position, int width, int height, int cell_size);
    ~QuadTree();

    void clear();
    void split();

    // 改为使用实体 ID
    bool remove(uint64_t entity_id);
    void insert(uint64_t entity_id);
    void retrieve(std::vector<uint64_t>& return_ids, const CollisionBox& area) const;

private:
    static constexpr int QUADTREE_MAX_OBJECTS = 8;
    static constexpr int QUADTREE_MAX_DEPTH = 5;

    int depth;
    Vector2 position;       // 格子坐标 (左上角)
    int width;
    int height;
    int cell_size;           // 每格像素大小
    std::vector<uint64_t> object_ids;   // 存储实体 ID
    QuadTree* nodes[4];

    int get_index(const CollisionBox& rect) const;
    bool intersects(const CollisionBox& rect) const;
};

#endif // !_QUAD_TREE_H_