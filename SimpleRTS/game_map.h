#ifndef _GAME_MAP_H_
#define _GAME_MAP_H_

#include "vector2.h"
#include "collision_box.h"
#include <vector>

enum class TerrainType : uint8_t
{
    Mud,
    Water,
    Wood,
    Rock,
    Gold
};

class GameMap
{
public:
    GameMap(int width = 400, int height = 400)
        : width(width),
        height(height),
        world_bounds{
            {0.0f, 0.0f },                // position
            (float)(width * cell_size),    // width
            (float)(height * cell_size)    // height
        }
    {
        grid.resize(height, std::vector<TerrainType>(width,TerrainType::Mud));

        for (int y = 0; y < 50;y++)
        {
            for (int x = 1;x < 10;x++)
            {
                grid[y+  25*(x%2)][x*10] = TerrainType::Water;
                grid[y + 25 * (x % 2)][x * 10+1] = TerrainType::Water;
                grid[y + 25 * (x%2)][x * 10+2] = TerrainType::Water;
            }
        }

        for (int y = 0;y < 40;y++)
        {
            for (int x = 0;x < 20;x++)
            {
                if((y+100)<=height-1&&(x+300)<=width-1)
                    grid[y + 100][x + 100] = TerrainType::Water;
            }
        }
    }


    // 查询某一格子是否可通行
    bool is_cell_passable(int x, int y) const;  

    // 为目标点生成方向场
    std::vector<std::vector<Vector2>> generate_goal_flow_field(const Vector2& world_goal) const;

    // 新增：计算从 world_goal 出发的可达距离场（只返回距离二维数组）
    // 返回值：height × width 的float矩阵，-1.0f 表示障碍，INF 表示不可达（被隔开）
    std::vector<std::vector<float>> compute_distance_field(const Vector2& world_goal, float radius = -1.0f) const;

    // 生成局部流场：只计算以 world_goal 为中心、半径 radius_cells 格子范围内的方向
    // 范围外的格子在返回的流场中保持 (0,0)
    std::vector<std::vector<Vector2>> generate_local_flow_field(
        const Vector2& world_goal, float max_dist_cells) const;

    // 从指定流场中双线性插值采样方向（世界坐标）
    Vector2 sample_flow_from_box(const std::vector<std::vector<Vector2>>& flow, const CollisionBox& box) const;

    // 若目标点不可通行（水中），返回最近的可通行格子中心（世界坐标）
    // 若已可通行，返回原坐标
    Vector2 find_nearest_passable(const Vector2& world_goal) const;

    int get_cell_size() const
    {
        return cell_size;
    }

    int get_width() const
    {
        return width;
    }

    int get_height() const
    {
        return height;
    }

    const std::vector<std::vector<TerrainType>>& get_grid() const
    {
        return grid;
    }

    void set_grid_by_pos(int x, int y, TerrainType type)
    {
        grid[y][x] = type;
    }

private:
    int cell_size = 10;       // 格子边长
    int width = 0;        // 地图宽度（格子数）
    int height = 0;        // 地图高度（格子数）

    // 格子地形
    std::vector<std::vector<TerrainType>> grid;

    CollisionBox world_bounds;
};

#endif // !_GAME_MAP_H_
