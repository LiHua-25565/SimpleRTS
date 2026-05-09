#ifndef _QUAD_TREE_H_
#define _QUAD_TREE_H_

#include "vector2.h"
#include "game_object.h"
#include <vector>

class QuadTree
{
public:
    QuadTree(int depth, Vector2 position, int width, int height, int cell_size);
    ~QuadTree();

    void clear();
    void split();

    bool remove(GameObject* obj);
    void insert(GameObject* obj);
    void retrieve(std::vector<GameObject*>& return_objects, const CollisionBox& area) const;

private:
    static constexpr int QUADTREE_MAX_OBJECTS = 8;
    static constexpr int QUADTREE_MAX_DEPTH = 5;

    int depth;
    Vector2 position;   // 格子坐标 (左上角)
    int width;
    int height;
    int cell_size;      // 每格像素大小
    std::vector<GameObject*> object_list;
    QuadTree* nodes[4];

    int get_index(const CollisionBox& rect) const;
    // 判断节点区域（格子）与对象碰撞盒（世界）是否相交
    bool intersects(const CollisionBox& rect) const;
};

#endif