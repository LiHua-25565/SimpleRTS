#include "game_map.h"
#include "world_entity_mgr.h"
#include <cmath>
#include <queue>
#include <algorithm>

bool GameMap::is_cell_passable(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    return static_obstacle_field[y][x] >= 0.0f && dynamic_obstacle_field[y][x] >= 0.0f;
}

bool GameMap::is_box_passable(const CollisionBox& box) const {
    int minx = (int)(box.position.x / cell_size);
    int miny = (int)(box.position.y / cell_size);
    int maxx = (int)((box.position.x + box.width) / cell_size);
    int maxy = (int)((box.position.y + box.height) / cell_size);

    // 右/下边界恰好落在格子边界上时不包含该格
    const float eps = 0.0001f;
    if ((box.position.x + box.width) - maxx * cell_size < eps) maxx--;
    if ((box.position.y + box.height) - maxy * cell_size < eps) maxy--;

    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x)
            if (!is_cell_passable(x, y)) return false;
    return true;
}

std::vector<std::vector<float>> GameMap::compute_distance_field(const Vector2& world_goal, float radius) const
{
    static const float INF = 1e20f;
    static const float SQRT2 = 1.41421356237f;
    static const int dx[8] = { 1,-1,0,0,1,1,-1,-1 };
    static const int dy[8] = { 0,0,1,-1,1,-1,1,-1 };
    static const float cost[8] = { 1.0f,1.0f ,1.0f ,1.0f ,SQRT2 ,SQRT2 ,SQRT2 ,SQRT2 };
    std::vector<std::vector<float>> dist_field(height, std::vector<float>(width, INF));

    // 统一用 is_cell_passable 标记障碍（同时覆盖水域与建筑/资源）。
    // 这里原本自己重算建筑包围盒，少了边界 eps 修正，比动态障碍场多标记了一格，
    // 会让落在建筑边缘的目标点被误判为不可通行、进而返回空流场
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (!is_cell_passable(x, y)) dist_field[y][x] = -1.0f;

    // 计算距离场（Dijkstra）
    int gx = int(world_goal.x / cell_size);
    int gy = int(world_goal.y / cell_size);
    if (dist_field[gy][gx] < 0.0f)  // 目标点不可通行
        return dist_field;

    using State = std::pair<float, std::pair<int, int>>;
    std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
    dist_field[gy][gx] = 0.0f;
    pq.push({ 0.0f,{ gx,gy } });

    while (!pq.empty())
    {
        auto [cur_dist, pos] = pq.top();
        pq.pop();
        int cx = pos.first, cy = pos.second;
        if (cur_dist > dist_field[cy][cx]) continue;

        for (int i = 0;i < 8;++i)
        {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            if (dist_field[ny][nx] < 0.0f) continue;

            float new_dist = cur_dist + cost[i];
            if (radius >= 0.0f && new_dist > radius) continue;

            if (new_dist < dist_field[ny][nx])
            {
                dist_field[ny][nx] = new_dist;
                pq.push({ new_dist,{nx,ny} });
            }
        }
    }

    return dist_field;
}

std::vector<std::vector<Vector2>> GameMap::generate_goal_flow_field(const Vector2& world_goal) const
{
    static const float INF = 1e20f;
    static const float SQRT2 = 1.41421356237f;
    static const int dx[8] = { 1,-1,0,0,1,1,-1,-1 };
    static const int dy[8] = { 0,0,1,-1,1,-1,1,-1 };
    static const float cost[8] = { 1.0f,1.0f ,1.0f ,1.0f ,SQRT2 ,SQRT2 ,SQRT2 ,SQRT2 };
    std::vector<std::vector<Vector2>> flow_field(height, std::vector<Vector2>(width, { 0.0f, 0.0f }));

    std::vector<std::vector<float>> dist_field(height, std::vector<float>(width, INF));

    // 统一用 is_cell_passable 标记障碍（同时覆盖水域与建筑/资源）。
    // 这里原本自己重算建筑包围盒，少了边界 eps 修正，比动态障碍场多标记了一格，
    // 会让落在建筑边缘的目标点被误判为不可通行、进而返回空流场
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (!is_cell_passable(x, y)) dist_field[y][x] = -1.0f;

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
        auto [cur_dist, pos] = pq.top();
        pq.pop();
        int cx = pos.first, cy = pos.second;
        if (cur_dist > dist_field[cy][cx]) continue;

        for (int i = 0;i < 8;++i)
        {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            if (dist_field[ny][nx] < 0.0f) continue;

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
            if (dist_field[y][x] <= 0.0f || dist_field[y][x] >= INF / 2) continue;

            float best_dist = dist_field[y][x];
            int best_idx = -1;
            for (int i = 0;i < 8;++i)
            {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                // d >= 0：允许取到目标格（d==0）。原来用 d > 0 会排除目标格，
                // 导致目标点周围一圈格子拿不到指向目标的方向（零向量死环）
                float d = dist_field[ny][nx];
                if (d >= 0.0f && d < best_dist)
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

    static const float INF = 1e20f;
    std::vector<std::vector<float>> dist(height, std::vector<float>(width, INF));

    // 同 generate_goal_flow_field：统一用 is_cell_passable，避免多标记一格
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (!is_cell_passable(x, y)) dist[y][x] = -1.0f;

    int gx = (int)(world_goal.x / cell_size);
    int gy = (int)(world_goal.y / cell_size);
    gx = std::clamp(gx, 0, width - 1);
    gy = std::clamp(gy, 0, height - 1);

    if (dist[gy][gx] < 0.0f) {
        bool found = false;
        for (int r = 1; r <= (int)max_dist_cells && !found; ++r) {
            for (int dy = -r; dy <= r && !found; ++dy)
                for (int dx = -r; dx <= r && !found; ++dx) {
                    int nx = gx + dx, ny = gy + dy;
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    if (dist[ny][nx] >= 0.0f) {
                        gx = nx; gy = ny;
                        found = true;
                    }
                }
        }
        if (!found) return flow_field;
    }

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

        if (cur_dist >= max_dist_cells) continue;

        for (int i = 0; i < 8; ++i) {
            int nx = cx + dirs[i][0];
            int ny = cy + dirs[i][1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (dist[ny][nx] < 0.0f) continue;

            float new_dist = cur_dist + costs[i];
            if (new_dist < dist[ny][nx]) {
                dist[ny][nx] = new_dist;
                pq.push({ new_dist, { nx, ny } });
            }
        }
    }

    Vector2 dir_vectors[8];
    for (int i = 0; i < 8; ++i)
        dir_vectors[i] = Vector2((float)dirs[i][0], (float)dirs[i][1]).normalize();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (dist[y][x] <= 0.0f || dist[y][x] >= INF / 2.0f) continue;

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

// 以中心点所在格判断可通行。
// 不用整个碰撞盒：单位按格子流场行走时必然会与障碍格部分重叠（单位宽 20px > 格子 10px），
// 用整个盒子判断会让贴着障碍走被误判为不可通行；真正的分离由 RVO 负责。
static bool center_passable(const Vector2& center, const GameMap* map)
{
    int cs = map->get_cell_size();
    return map->is_cell_passable((int)(center.x / cs), (int)(center.y / cs));
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

    // 以碰撞盒中心所在格的方向为基准。
    // 若对所有覆盖格直接做面积加权，当盒子横跨“分水岭”（左右绕行代价相同，
    // 两侧方向相反）时两个方向会相互抵消，单位会径直撞向障碍。
    // 因此与基准方向相反的格子不参与加权。
    int cx = std::clamp((int)((box.position.x + box.width * 0.5f) / cell_size), 0, width - 1);
    int cy = std::clamp((int)((box.position.y + box.height * 0.5f) / cell_size), 0, height - 1);
    Vector2 base = flow[cy][cx];
    const bool has_base = base.length() > 0.01f;

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
                if (dir.length() <= 0.01f) continue;                       // 障碍格/目标格
                if (has_base && (dir.x * base.x + dir.y * base.y) <= 0.0f) continue;  // 与基准反向，丢弃
                total_dir = total_dir + dir * area;
            }
        }
    }
    if (total_dir.length() > 0.01f) {
        Vector2 dir = total_dir.normalize();
        // 邻格的平滑分量可能把方向“切”进障碍（贴着障碍走时尤其明显）。
        // 若沿该方向走一步就会进入不可通行格，依次退化为：
        // 纯中心格方向 → 纯纵向 → 纯横向（分轴滑动）
        const float probe = (float)cell_size * 1.5f;
        Vector2 center = box.position + Vector2(box.width * 0.5f, box.height * 0.5f);

        // 沿方向分多点采样：只检查终点会漏掉中途擦到的障碍格
        auto path_ok = [&](const Vector2& d) {
            for (float t = 2.0f; t <= probe; t += 2.0f)
                if (!center_passable(center + d * t, this)) return false;
            return true;
        };

        if (!path_ok(dir)) {
            Vector2 candidates[3] = {
                base.normalize(),
                Vector2(0.0f, dir.y).normalize(),
                Vector2(dir.x, 0.0f).normalize()
            };
            for (const Vector2& c : candidates) {
                if (c.length() > 0.01f && path_ok(c)) return c;
            }
        }
        return dir;
    }

    // 兜底：中心格无效（站在障碍上）时，螺旋向外找最近的有效方向
    for (int r = 1; r <= 4; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                Vector2 dir = flow[ny][nx];
                if (dir.length() > 0.01f) return dir.normalize();
            }
        }
    }
    return { 0.0f, 0.0f };
}

Vector2 GameMap::find_nearest_passable(const Vector2& world_goal) const
{
    int gx = (int)(world_goal.x / cell_size);
    int gy = (int)(world_goal.y / cell_size);
    gx = std::max(0, std::min(gx, width - 1));
    gy = std::max(0, std::min(gy, height - 1));

    // 同时考虑水（静态）与建筑/资源（动态），
    // 否则命令点落在建筑上时全局流场会返回全零，单位永久冻结
    if (is_cell_passable(gx, gy))
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
        if (is_cell_passable(x, y)) {
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

void GameMap::generate_static_obstacle_field() {
    static_obstacle_field.resize(height, std::vector<float>(width, 0.0f));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (grid[y][x] == TerrainType::Water)
                static_obstacle_field[y][x] = -1.0f;
}

void GameMap::reset_terrain(TerrainType type) {
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            grid[y][x] = type;
    generate_static_obstacle_field();
}

void GameMap::resize(int new_width, int new_height) {
    if (new_width <= 0 || new_height <= 0) return;
    width = new_width;
    height = new_height;

    grid.assign(height, std::vector<TerrainType>(width, TerrainType::Mud));
    world_bounds = CollisionBox{ {0.0f, 0.0f},
                                 (float)(width * cell_size),
                                 (float)(height * cell_size) };
    generate_static_obstacle_field();
    dynamic_obstacle_field.assign(height, std::vector<float>(width, 0.0f));
}

void GameMap::rebuild_static_obstacle_field() {
    generate_static_obstacle_field();
}

void GameMap::fill_dynamic_box(const CollisionBox& box)
{
    int minx = (int)(box.position.x / cell_size);
    int miny = (int)(box.position.y / cell_size);
    int maxx = (int)((box.position.x + box.width) / cell_size);
    int maxy = (int)((box.position.y + box.height) / cell_size);

    // 如果右/下边界恰好落在格子边界上，不包含该格子
    float eps = 0.0001f;
    if ((box.position.x + box.width) - maxx * cell_size < eps) maxx--;
    if ((box.position.y + box.height) - maxy * cell_size < eps) maxy--;

    minx = std::max(0, minx);
    miny = std::max(0, miny);
    maxx = std::min(width - 1, maxx);
    maxy = std::min(height - 1, maxy);

    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x)
            dynamic_obstacle_field[y][x] = -1.0f;
}

void GameMap::add_object_to_dynamic_obstacle_field(const GameObject* object) {
    if (!object || !object->check_valid()) return;
    fill_dynamic_box(object->get_collision_box());
    ++obstacle_version_;
}

void GameMap::rebuild_dynamic_obstacle_field()
{
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            dynamic_obstacle_field[y][x] = 0.0f;

    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj || !obj->check_valid()) continue;
        if (obj->get_component<Structure>() || obj->get_component<Harvestable>())
            fill_dynamic_box(obj->get_collision_box());
    }
    ++obstacle_version_;
}

void GameMap::clear_dynamic_obstacle_field()
{
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            dynamic_obstacle_field[y][x] = 0.0f;
    ++obstacle_version_;
}

void GameMap::remove_object_from_dynamic_obstacle_field(const GameObject* object) {
    if (!object) return;
    const CollisionBox& box = object->get_collision_box();

    int minx = (int)(box.position.x / cell_size);
    int miny = (int)(box.position.y / cell_size);
    int maxx = (int)((box.position.x + box.width) / cell_size);
    int maxy = (int)((box.position.y + box.height) / cell_size);

    float eps = 0.0001f;
    if ((box.position.x + box.width) - maxx * cell_size < eps) maxx--;
    if ((box.position.y + box.height) - maxy * cell_size < eps) maxy--;

    minx = std::max(0, minx);
    miny = std::max(0, miny);
    maxx = std::min(width - 1, maxx);
    maxy = std::min(height - 1, maxy);

    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x)
            dynamic_obstacle_field[y][x] = 0.0f;
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