#include "rvo_adapter.h"
#include "world_entity_mgr.h"
#include "components.h"
#include <cmath>
#include <queue>

// RVO2 要求障碍物顶点按逆时针给出（其内部用 leftOf 判定凸性，
// 顺序反了会被当成非凸顶点，导致障碍约束不生成、单位直接穿墙）。
// 屏幕坐标 y 轴向下，因此逆时针顺序为：左上 → 右上 → 右下 → 左下
std::vector<RVO::Vector2> RVOAdapter::box_to_obstacle(const CollisionBox& box) {
    float left = box.position.x;
    float top = box.position.y;
    float right = left + box.width;
    float bottom = top + box.height;
    // 逆时针：左上 → 右上 → 右下 → 左下
    return {
        RVO::Vector2(left,  top),
        RVO::Vector2(right, top),
        RVO::Vector2(right, bottom),
        RVO::Vector2(left,  bottom)
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

            // 逆时针：左上 → 右上 → 右下 → 左下
            std::vector<RVO::Vector2> vertices = {
                RVO::Vector2(left,  top),
                RVO::Vector2(right, top),
                RVO::Vector2(right, bottom),
                RVO::Vector2(left,  bottom)
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

// RVO 邻居与时间窗参数。
//
// 手感目标（星际争霸 2 风格）：单位“贴身才让、一让就过”，而不是老远就开始
// 礼貌性地减速。关键配平关系：timeHorizon * maxSpeed 必须与 neighborDist 同量级。
//   timeHorizon 越大 → VO 锥越大 → 可行速度空间被切得越多 → 单位越早、越狠地减速。
// 原先的 5.0s/3.0s 失配（5s 可走 300px >> 感知半径 80px）导致“靠近任何东西
// 速度骤降”；1.5s/1.2s 仍有明显余量——实测贴墙单次绕行平均只有满速的 85%。
// 这里进一步收短：与其它单位向前看 0.9s（≈54px），与静态障碍 0.7s（≈42px），
// 让减速只发生在真正需要避让的最后一刻；配合上层切向转向与软推挤，
// 单位贴近障碍/同伴时仍能保持速度，像 SC2 一样“挤”过去而不是停下来排队。
static constexpr float  RVO_NEIGHBOR_DIST     = 52.0f;  // 邻居查询半径（像素）
static constexpr size_t RVO_MAX_NEIGHBORS     = 8;      // 单个 agent 最多考虑的邻居数
static constexpr float  RVO_TIME_HORIZON      = 0.9f;   // 与其它单位：向前看 0.9s（≈54px 行程）
static constexpr float  RVO_TIME_HORIZON_OBST = 0.7f;   // 与静态障碍：≈42px 行程，贴墙保持速度
static constexpr float  RVO_RADIUS_SCALE      = 0.70f;  // agent 半径 = 外接圆半径 × 该系数

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
        // 投射物不作为 agent：它们靠自身速度飞行，不参与避让
        if (obj->get_component<Projectile>()) continue;
        auto* movable = obj->get_component<Movable>();
        if (!movable) continue;

        const CollisionBox& cb = obj->get_collision_box();
        Vector2 center = cb.get_center_position();

        // 修正半径：外接圆半径的 70%。
        // 系数越大，agent 越“胖”，通道越窄、约束越强、减速越明显；
        // 太小则会在建筑拐角处轻微擦模。0.7 是贴墙与通行效率的折中
        float diagonal = std::sqrt(cb.width * cb.width + cb.height * cb.height);
        float radius = diagonal * 0.5f * RVO_RADIUS_SCALE;

        size_t idx = sim_->addAgent(
            RVO::Vector2(center.x, center.y),
            RVO_NEIGHBOR_DIST, RVO_MAX_NEIGHBORS,
            RVO_TIME_HORIZON, RVO_TIME_HORIZON_OBST,
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

void RVOAdapter::set_agent_position(uint64_t entity_id, const Vector2& pos) {
    if (!sim_) return;
    auto it = entity_to_agent_.find(entity_id);
    if (it != entity_to_agent_.end())
        sim_->setAgentPosition(it->second, RVO::Vector2(pos.x, pos.y));
}

bool RVOAdapter::has_agent(uint64_t entity_id) const {
    return entity_to_agent_.find(entity_id) != entity_to_agent_.end();
}

size_t RVOAdapter::get_agent_count() const {
    return entity_to_agent_.size();
}