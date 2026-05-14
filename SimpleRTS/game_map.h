#ifndef _GAME_MAP_H_
#define _GAME_MAP_H_

#include "vector2.h"
#include "collision_box.h"
#include <vector>

enum class TerrainType : uint8_t
{
    Mud,
    Water
};

class GameMap
{
public:
    GameMap(int width = 300, int height = 200)
        : width(width),
        height(height),
        world_bounds{
            {0.0f, 0.0f},
            (float)(width * cell_size),
            (float)(height * cell_size)
        }
    {
        // 全部初始化为 Mud
        grid.resize(height, std::vector<TerrainType>(width, TerrainType::Mud));

        // 计算中心偏移，使四个 10×10 方块居中，方块总尺寸为 21×21 (包含间隔)
        const int block_size = 10;
        const int gap = 1;                     // 方块之间的间隔（格数）
        const int total_span = block_size * 2 + gap;   // 21 格
        int start_x = (width - total_span) / 2;
        int start_y = (height - total_span) / 2;

        // 左上：Water
        for (int y = 0; y < block_size; ++y)
            for (int x = 0; x < block_size; ++x)
                grid[start_y + y][start_x + x] = TerrainType::Water;
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
