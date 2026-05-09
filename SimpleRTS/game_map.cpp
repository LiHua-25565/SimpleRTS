#include "game_map.h"
#include "world_entity_mgr.h"
#include <cmath>
#include <queue>

bool GameMap::is_cell_passable(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    return grid[y][x] == TerrainType::Mud;
}

std::vector<std::vector<Vector2>> GameMap::generate_goal_flow_field(const Vector2& world_goal) const
{
    static const float INF = 1e20f;
    static const float SQRT2 = 1.41421356237f;
    static const int dx[8] = { 1,-1,0,0,1,1,-1,-1 };
    static const int dy[8] = { 0,0,1,-1,1,-1,1,-1 };
    static const float cost[8] = { 1.0f,1.0f ,1.0f ,1.0f ,SQRT2 ,SQRT2 ,SQRT2 ,SQRT2 };
    std::vector<std::vector<Vector2>> flow_field(height, std::vector<Vector2>(width, { 0.0f, 0.0f }));
    
    // 距离矩阵 INF标记未到达，-1.0f标记不可通过
    std::vector<std::vector<float>> dist_field(height, std::vector<float>(width, INF));    

    // 标记动态障碍（建筑，资源）
    const std::unordered_set<GameObject*>& object_set = WorldEntityMgr::instance()->get_object_set();
    for (GameObject* object : object_set)
    {
        if (object->get_component<Structure>() || object->get_component<Harvestable>())
        {
            CollisionBox collision_box = object->get_collision_box();
            int minx = std::max(int(collision_box.position.x / cell_size), 0);
            int miny = std::max(int(collision_box.position.y / cell_size), 0);
            int maxx = std::min(int((collision_box.position.x + collision_box.width) / cell_size), width - 1);
            int maxy = std::min(int((collision_box.position.y + collision_box.height) / cell_size), height - 1);
            for (int x = minx;x <= maxx;++x)
                for (int y = miny;y <= maxy;++y)
                    dist_field[y][x] = -1.0f;
        }
    }

    // 标记静态障碍（水域）
    for (int x = 0;x < width;++x)
        for (int y = 0; y < height;++y)
            if (grid[y][x] == TerrainType::Water) dist_field[y][x] = -1.0f;

    // 计算距离场（Dijkstra）
    int gx = int(world_goal.x / cell_size);
    int gy = int(world_goal.y / cell_size);
    if (dist_field[gy][gx] < 0.0f)  // 目标点不可通行
        return flow_field;

    using State = std::pair<float, std::pair<int, int>>;
    std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
    dist_field[gy][gx] = 0.0f;
    pq.push({ 0.0f,{ gx,gy } });
    
    while (!pq.empty())
    {
        // 当前格距离和位置
        auto [cur_dist, pos] = pq.top();
        pq.pop();
        int cx = pos.first, cy = pos.second;
        if (cur_dist > dist_field[cy][cx]) continue;

        for (int i = 0;i < 8;++i)
        {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            if (dist_field[ny][nx] < 0.0f) continue;    // 跳过不可通行格

            float new_dist = cur_dist + cost[i];
            if (new_dist < dist_field[ny][nx])
            {
                dist_field[ny][nx] = new_dist;
                pq.push({ new_dist,{nx,ny} });
            }
        }
    }

    // 计算向量场
    Vector2 dir_vectors[8];
    for (int i = 0; i < 8; ++i)
    {
        dir_vectors[i] = Vector2((float)dx[i], (float)dy[i]).normalize();
    }
    
    for (int y = 0;y < height;++y)
    {
        for (int x = 0;x < width;++x)
        {
            if (dist_field[y][x] <= 0.0f || dist_field[y][x] >= INF / 2) continue;  // 终点和不可通行点

            float best_dist = dist_field[y][x];
            int best_idx = -1;
            for (int i = 0;i < 8;++i)
            {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                float d = dist_field[ny][nx];
                if (d > 0 && d < best_dist)
                {
                    best_dist = d;
                    best_idx = i;
                }
            }
            if (best_idx > -1)
                flow_field[y][x] = dir_vectors[best_idx];
        }
    }

    return flow_field;
}

std::vector<std::vector<Vector2>> GameMap::generate_local_flow_field(
    const Vector2& world_goal, float max_dist_cells) const
{
    std::vector<std::vector<Vector2>> flow_field(height, std::vector<Vector2>(width, { 0.0f, 0.0f }));

    // 与全局流场完全一致的距离场定义：
    // INF 表示未到达，-1.0f 表示不可通过（障碍）
    static const float INF = 1e20f;
    std::vector<std::vector<float>> dist(height, std::vector<float>(width, INF));

    // 标记动态障碍（建筑、资源）
    const auto& object_set = WorldEntityMgr::instance()->get_object_set();
    for (auto* obj : object_set) {
        if (obj->get_component<Structure>() || obj->get_component<Harvestable>()) {
            CollisionBox box = obj->get_collision_box();
            int minx = std::max((int)(box.position.x / cell_size), 0);
            int miny = std::max((int)(box.position.y / cell_size), 0);
            int maxx = std::min((int)((box.position.x + box.width) / cell_size), width - 1);
            int maxy = std::min((int)((box.position.y + box.height) / cell_size), height - 1);
            for (int y = miny; y <= maxy; ++y)
                for (int x = minx; x <= maxx; ++x)
                    dist[y][x] = -1.0f;   // 不可通过
        }
    }

    // 标记静态障碍 Water
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (grid[y][x] == TerrainType::Water)
                dist[y][x] = -1.0f;

    // 目标点格子坐标
    int gx = (int)(world_goal.x / cell_size);
    int gy = (int)(world_goal.y / cell_size);
    gx = std::clamp(gx, 0, width - 1);
    gy = std::clamp(gy, 0, height - 1);

    // 若目标点不可通行，寻找最近可通行点（在远处也可，这里做简单处理）
    if (dist[gy][gx] < 0.0f) {
        bool found = false;
        for (int r = 1; r <= (int)max_dist_cells && !found; ++r) {
            for (int dy = -r; dy <= r && !found; ++dy)
                for (int dx = -r; dx <= r && !found; ++dx) {
                    int nx = gx + dx, ny = gy + dy;
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    if (dist[ny][nx] >= 0.0f) {   // 可通行
                        gx = nx; gy = ny;
                        found = true;
                    }
                }
        }
        if (!found) return flow_field; // 无通路
    }

    // Dijkstra（与全局流场一致）
    using State = std::pair<float, std::pair<int, int>>;
    std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
    dist[gy][gx] = 0.0f;
    pq.push({ 0.0f, { gx, gy } });

    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    const float costs[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };

    while (!pq.empty()) {
        auto [cur_dist, pos] = pq.top(); pq.pop();
        int cx = pos.first, cy = pos.second;
        if (cur_dist > dist[cy][cx]) continue;

        // ***** 距离限制：只扩散 max_dist_cells 步 *****
        if (cur_dist >= max_dist_cells) continue;

        for (int i = 0; i < 8; ++i) {
            int nx = cx + dirs[i][0];
            int ny = cy + dirs[i][1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (dist[ny][nx] < 0.0f) continue;  // 障碍

            float new_dist = cur_dist + costs[i];
            if (new_dist < dist[ny][nx]) {
                dist[ny][nx] = new_dist;
                pq.push({ new_dist, { nx, ny } });
            }
        }
    }

    // 生成方向场（与全局流场一致）
    Vector2 dir_vectors[8];
    for (int i = 0; i < 8; ++i)
        dir_vectors[i] = Vector2((float)dirs[i][0], (float)dirs[i][1]).normalize();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (dist[y][x] <= 0.0f || dist[y][x] >= INF / 2.0f) continue; // 终点或障碍或未到达

            float best_dist = dist[y][x];
            int best_idx = -1;
            for (int i = 0; i < 8; ++i) {
                int nx = x + dirs[i][0];
                int ny = y + dirs[i][1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                if (dist[ny][nx] >= 0.0f && dist[ny][nx] < best_dist) {
                    best_dist = dist[ny][nx];
                    best_idx = i;
                }
            }
            if (best_idx >= 0)
                flow_field[y][x] = dir_vectors[best_idx];
        }
    }
    flow_field[gy][gx] = { 0.0f, 0.0f };
    return flow_field;
}

// 按照碰撞箱占格子面积加权
Vector2 GameMap::sample_flow_from_box(const std::vector<std::vector<Vector2>>& flow, const CollisionBox& box) const {
    if (flow.empty() || flow[0].empty()) return { 0.0f, 0.0f };

    int min_x = (int)(box.position.x / cell_size);
    int min_y = (int)(box.position.y / cell_size);
    int max_x = (int)((box.position.x + box.width) / cell_size);
    int max_y = (int)((box.position.y + box.height) / cell_size);

    min_x = std::max(0, min_x);
    min_y = std::max(0, min_y);
    max_x = std::min(max_x, (int)flow[0].size() - 1);
    max_y = std::min(max_y, (int)flow.size() - 1);

    Vector2 total_dir{ 0.0f, 0.0f };

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            float cell_left = x * cell_size;
            float cell_top = y * cell_size;
            float cell_right = cell_left + cell_size;
            float cell_bottom = cell_top + cell_size;

            float overlap_left = std::max(box.position.x, cell_left);
            float overlap_right = std::min(box.position.x + box.width, cell_right);
            float overlap_top = std::max(box.position.y, cell_top);
            float overlap_bottom = std::min(box.position.y + box.height, cell_bottom);

            if (overlap_left < overlap_right && overlap_top < overlap_bottom) {
                float area = (overlap_right - overlap_left) * (overlap_bottom - overlap_top);
                const Vector2& dir = flow[y][x];
                total_dir = total_dir + dir * area;
            }
        }
    }
    return total_dir.normalize(); 
}

Vector2 GameMap::find_nearest_passable(const Vector2& world_goal) const
{
    int gx = (int)(world_goal.x / cell_size);
    int gy = (int)(world_goal.y / cell_size);
    gx = std::max(0, std::min(gx, width - 1));
    gy = std::max(0, std::min(gy, height - 1));

    if (grid[gy][gx] != TerrainType::Water)
        return world_goal; // 本来就可通行

    // BFS 查找最近可通行格子
    std::vector<std::vector<bool>> visited(height, std::vector<bool>(width, false));
    std::queue<std::pair<int, int>> q;
    q.push({ gx, gy });
    visited[gy][gx] = true;

    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    while (!q.empty())
    {
        auto [x, y] = q.front(); q.pop();
        if (grid[y][x] != TerrainType::Water) {
            return { x * cell_size + cell_size * 0.5f,
                     y * cell_size + cell_size * 0.5f };
        }
        for (auto d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (!visited[ny][nx]) {
                visited[ny][nx] = true;
                q.push({ nx, ny });
            }
        }
    }
    return world_goal; // 全图无路，保持原值
}

// 后续地图文件化...输入和读取

//// --- 写入 .srmap 文件 ---
//void GameMap::save_to_file(const std::string& filename) const {
//    std::ofstream out(filename, std::ios::binary);
//    if (!out) return;
//
//    const char magic[4] = { 'S', 'R', 'M', 'P' };
//    int version = 1;
//
//    out.write(magic, 4);
//    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
//    out.write(reinterpret_cast<const char*>(&width), sizeof(width));
//    out.write(reinterpret_cast<const char*>(&height), sizeof(height));
//    out.write(reinterpret_cast<const char*>(&cell_size), sizeof(cell_size));
//
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            auto terrain = static_cast<uint8_t>(grid[y][x]);
//            out.write(reinterpret_cast<const char*>(&terrain), sizeof(terrain));
//        }
//    }
//}
//
//// --- 从 .srmap 文件读取并构建 GameMap ---
//std::unique_ptr<GameMap> GameMap::load_from_file(const std::string& filename) {
//    std::ifstream in(filename, std::ios::binary);
//    if (!in) return nullptr;
//
//    char magic[4];
//    int version, width, height, cell_size;
//    in.read(magic, 4);
//    if (magic[0] != 'S' || magic[1] != 'R' || magic[2] != 'M' || magic[3] != 'P') return nullptr;
//
//    in.read(reinterpret_cast<char*>(&version), sizeof(version));
//    // 未来可以根据 version 处理不同地图版本
//
//    in.read(reinterpret_cast<char*>(&width), sizeof(width));
//    in.read(reinterpret_cast<char*>(&height), sizeof(height));
//    in.read(reinterpret_cast<char*>(&cell_size), sizeof(cell_size));
//
//    auto map = std::make_unique<GameMap>(width, height, cell_size);
//
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            uint8_t terrain_byte;
//            in.read(reinterpret_cast<char*>(&terrain_byte), sizeof(terrain_byte));
//            map->grid[y][x] = static_cast<TerrainType>(terrain_byte);
//        }
//    }
//    return map;
//}