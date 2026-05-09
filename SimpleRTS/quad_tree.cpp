#include "quad_tree.h"
#include "stdio.h"

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
	object_list.clear();
	for (int i = 0;i < 4;i++)
	{
		if (nodes[i])
			delete nodes[i];
		nodes[i] = nullptr;
	}
}

void QuadTree::split()
{
	// 太小无法继续细分
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
	// 中点转为世界坐标
	float mid_x = (position.x + width * 0.5f) * cell_size;
	float mid_y = (position.y + height * 0.5f) * cell_size;

	bool top = rect.position.y + rect.height <= mid_y;
	bool bottom = rect.position.y >= mid_y;
	bool left = rect.position.x + rect.width <= mid_x;
	bool right = rect.position.x >= mid_x;

	if (top && left) return 0;
	if (top && right) return 1;
	if (bottom && left) return 2;
	if (bottom && right) return 3;

	return -1;
}

bool QuadTree::remove(GameObject* obj)
{
	if (!intersects(obj->get_collision_box()))
		return false;

	for (auto it = object_list.begin(); it != object_list.end(); ++it)
	{
		if (*it == obj)
		{
			object_list.erase(it);
			return true;
		}
	}

	if (nodes[0])
	{
		for (int i = 0; i < 4; ++i)
		{
			if (nodes[i]->remove(obj))
				return true;
		}
	}
	return false;
}

void QuadTree::insert(GameObject* obj)
{
	const CollisionBox& box = obj->get_collision_box();

	if (!intersects(box)) return;

	// 尝试放入子节点
	if (nodes[0])
	{
		int idx = get_index(box);
		if (idx != -1)
		{
			nodes[idx]->insert(obj);
			return;
		}
	}

	// 子节点不存在或者无法放入子节点，那么放入本节点
	object_list.push_back(obj);

	if (object_list.size() > QUADTREE_MAX_OBJECTS && depth < QUADTREE_MAX_DEPTH)
	{
		if (!nodes[0]) split();

		// 分裂后若成功创建子节点，则将本节点所有对象下放
		if (nodes[0])
		{
			int i = 0;
			while (i < (int)object_list.size())
			{
				int idx = get_index(object_list[i]->get_collision_box());
				if (idx != -1)
				{
					nodes[idx]->insert(object_list[i]);
					object_list.erase(object_list.begin() + i);
				}
				else
				{
					++i;
				}
			}
		}
	}
}

void QuadTree::retrieve(std::vector<GameObject*>& return_objects, const CollisionBox& area) const
{
	if (!intersects(area))
		return;

	for (auto* obj : object_list)
		return_objects.push_back(obj);

	if (nodes[0])
	{
		for (int i = 0; i < 4; ++i)
			nodes[i]->retrieve(return_objects, area);
	}
}