#include "production_system.h"
#include "world_entity_mgr.h"
#include "game_map.h"
#include "components.h"

void ProductionSystem::on_update(float delta) {
    if (!factory) return;

    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;

        auto* queue = obj->get_component<ProductionQueue>();
        if (!queue || queue->queue.empty()) continue;

        // 处理队列头部
        auto& entry = queue->queue.front();
        entry.elapsed += delta;

        // 如果之前被冻结，新的帧继续尝试生成
        if (queue->frozen_flag) {
            queue->frozen_flag = false;
            // 尝试生成（不增加时间，因为该帧只是重试）
            if (try_spawn_unit(obj, entry.unit_type)) {
                // 生成成功，弹出队列
                queue->queue.erase(queue->queue.begin());
            }
            else {
                // 依旧没空位，重新冻结
                queue->frozen_flag = true;
            }
            continue; // 不继续累加时间
        }

        if (entry.elapsed >= entry.total_time) {
            // 生产完成，尝试生成单位
            if (try_spawn_unit(obj, entry.unit_type)) {
                queue->queue.erase(queue->queue.begin());
            }
            else {
                // 没有空位：冻结，等待下一帧重试
                queue->frozen_flag = true;
                // 注意：elapsed 保持为 total_time，避免多次冻结后超时
                entry.elapsed = entry.total_time;
            }
        }
    }
}

bool ProductionSystem::try_spawn_unit(GameObject* building, UnitEntityType type) {
    if (!factory || !building) return false;

    const auto& building_box = building->get_collision_box();
    float b_center_x = building_box.position.x + building_box.width * 0.5f;
    float b_center_y = building_box.position.y + building_box.height * 0.5f;

    constexpr float UNIT_WIDTH = 32.0f;
    constexpr float UNIT_HEIGHT = 32.0f;
    constexpr float HALF_UNIT = UNIT_WIDTH * 0.5f;
    constexpr float SEARCH_STEP = UNIT_WIDTH;   // 每次步进一个单位宽度
    constexpr int MAX_SEARCH_STEPS = 20;        // 最大步数，防止无限循环

    auto* map = WorldEntityMgr::instance()->get_map();
    if (!map) return false;

    const int cell_size = map->get_cell_size();

    // 四个方向的搜索向量（右、左、下、上）
    const Vector2 dirs[4] = {
        { 1.0f, 0.0f },
        { -1.0f, 0.0f },
        { 0.0f, 1.0f },
        { 0.0f, -1.0f }
    };

    // 起始距离：建筑边缘 + 单位半径
    float base_dist = std::max(building_box.width, building_box.height) * 0.5f + HALF_UNIT;

    for (int step = 0; step < MAX_SEARCH_STEPS; ++step) {
        float dist = base_dist + step * SEARCH_STEP;

        for (int d = 0; d < 4; ++d) {
            Vector2 offset = dirs[d] * dist;
            Vector2 spawn_pos{ b_center_x + offset.x - HALF_UNIT,
                               b_center_y + offset.y - HALF_UNIT };

            // 边界检查
            CollisionBox spawn_box{ spawn_pos, UNIT_WIDTH, UNIT_HEIGHT };
            if (spawn_pos.x < 0 || spawn_pos.y < 0 ||
                spawn_pos.x + UNIT_WIDTH > map->get_width() * cell_size ||
                spawn_pos.y + UNIT_HEIGHT > map->get_height() * cell_size)
                continue;

            // 可通行检查（中心点格子）
            int cx = (int)((spawn_pos.x + HALF_UNIT) / cell_size);
            int cy = (int)((spawn_pos.y + HALF_UNIT) / cell_size);
            if (!map->is_cell_passable(cx, cy)) continue;

            // 重叠检查
            if (factory->check_overlap(spawn_box)) continue;

            // 安全：创建单位
            factory->set_player_id(building->get_component<Ownership>()->player_id);
            GameObject* unit = factory->create_unit_by_type(type, spawn_box, false);
            if (unit) {
                return true;
            }
        }
    }
    return false;
}