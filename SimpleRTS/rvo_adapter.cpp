#include "rvo_adapter.h"
#include "world_entity_mgr.h"
#include <cmath>
#include <queue>

// ---------- 顶点顺序：逆时针 ----------
std::vector<RVO::Vector2> RVOAdapter::box_to_obstacle(const CollisionBox& box) {
    float left = box.position.x;
    float top = box.position.y;
    float right = left + box.width;
    float bottom = top + box.height;
    // 逆时针：左下 → 右下 → 右上 → 左上
    return {
        RVO::Vector2(left,  bottom),
        RVO::Vector2(right, bottom),
        RVO::Vector2(right, top),
        RVO::Vector2(left,  top)
    };
}

// ---------- 水域提取（顶点顺序修正为逆时针）----------
static std::vector<std::vector<RVO::Vector2>> extract_water_contours(const GameMap* map) {
    const int cell_size = map->get_cell_size();
    const int w = map->get_width();
    const int h = map->get_height();
    std::vector<std::vector<bool>> visited(h, std::vector<bool>(w, false));
    std::vector<std::vector<RVO::Vector2>> obstacles;

    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (visited[y][x] || map->get_grid()[y][x] != TerrainType::Water) continue;

            std::vector<std::pair<int, int>> cells;
            std::queue<std::pair<int, int>> q;
            q.push({ x, y });
            visited[y][x] = true;

            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                cells.push_back({ cx, cy });
                for (int i = 0; i < 4; ++i) {
                    int nx = cx + dx[i], ny = cy + dy[i];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    if (!visited[ny][nx] && map->get_grid()[ny][nx] == TerrainType::Water) {
                        visited[ny][nx] = true;
                        q.push({ nx, ny });
                    }
                }
            }

            int min_x = w, max_x = 0, min_y = h, max_y = 0;
            for (auto& c : cells) {
                min_x = std::min(min_x, c.first);
                max_x = std::max(max_x, c.first);
                min_y = std::min(min_y, c.second);
                max_y = std::max(max_y, c.second);
            }

            float left = min_x * cell_size;
            float top = min_y * cell_size;
            float right = (max_x + 1) * cell_size;
            float bottom = (max_y + 1) * cell_size;

            // 逆时针：左下 → 右下 → 右上 → 左上
            std::vector<RVO::Vector2> vertices = {
                RVO::Vector2(left,  bottom),
                RVO::Vector2(right, bottom),
                RVO::Vector2(right, top),
                RVO::Vector2(left,  top)
            };
            obstacles.push_back(vertices);
        }
    }
    return obstacles;
}

RVOAdapter* RVOAdapter::instance() {
    static RVOAdapter adapter;
    return &adapter;
}

void RVOAdapter::init(GameMap* map) {
    shutdown();
    map_ = map;
    if (map_) {
        cached_water_obstacles_ = extract_water_contours(map_);
    }
    rebuild_needed_ = true;
}

void RVOAdapter::shutdown() {
    delete sim_; sim_ = nullptr;
    entity_to_agent_.clear();
    agent_to_entity_.clear();
    rebuild_needed_ = true;
}

void RVOAdapter::request_rebuild() {
    rebuild_needed_ = true;
}

void RVOAdapter::ensure_sim_ready() {
    if (rebuild_needed_) {
        rebuild_simulation();
        rebuild_needed_ = false;
    }
}

void RVOAdapter::rebuild_simulation() {
    delete sim_;
    sim_ = new RVO::RVOSimulator();
    sim_->setTimeStep(fixed_timestep_);   // 使用固定步长

    // 1. 添加静态障碍物
    for (const auto& vertices : cached_water_obstacles_) {
        sim_->addObstacle(vertices);
    }

    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (const auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Structure>() || obj->get_component<Harvestable>()) {
            auto vertices = box_to_obstacle(obj->get_collision_box());
            sim_->addObstacle(vertices);
        }
    }
    sim_->processObstacles();

    // 2. 添加 Agent（仅可移动实体）
    entity_to_agent_.clear();
    agent_to_entity_.clear();

    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;
        auto* movable = obj->get_component<Movable>();
        if (!movable) continue;

        const CollisionBox& cb = obj->get_collision_box();
        Vector2 center = cb.get_center_position();

        // 修正半径：外接圆半径的 80%
        float diagonal = std::sqrt(cb.width * cb.width + cb.height * cb.height);
        float radius = diagonal * 0.5f * 0.8f;

        size_t idx = sim_->addAgent(
            RVO::Vector2(center.x, center.y),
            80.0f, 10,
            5.0f, 3.0f,
            radius,
            movable->speed,
            RVO::Vector2(movable->velocity.x, movable->velocity.y)
        );
        if (idx != RVO::RVO_ERROR) {
            entity_to_agent_[id] = idx;
            if (agent_to_entity_.size() <= idx)
                agent_to_entity_.resize(idx + 1);
            agent_to_entity_[idx] = id;
        }
    }
}

void RVOAdapter::set_pref_velocity(uint64_t entity_id, const Vector2& pref_vel) {
    if (!sim_) return;
    auto it = entity_to_agent_.find(entity_id);
    if (it != entity_to_agent_.end())
        sim_->setAgentPrefVelocity(it->second, RVO::Vector2(pref_vel.x, pref_vel.y));
}

void RVOAdapter::do_step() {
    if (sim_) sim_->doStep();
}

Vector2 RVOAdapter::get_agent_velocity(uint64_t entity_id) const {
    if (!sim_) return { 0, 0 };
    auto it = entity_to_agent_.find(entity_id);
    if (it != entity_to_agent_.end()) {
        RVO::Vector2 v = sim_->getAgentVelocity(it->second);
        return Vector2(v.x(), v.y());
    }
    return { 0, 0 };
}