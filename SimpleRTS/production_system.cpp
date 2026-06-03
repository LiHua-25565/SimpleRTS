#include "production_system.h"
#include "world_entity_mgr.h"
#include "game_map.h"
#include "components.h"
#include <cmath>

// ========== 建筑环绕槽位生成 ==========
static bool try_spawn_unit_circles(GameObject* building, UnitEntityType type, ObjectFactory* factory) {
    if (!building || !factory) return false;

    const CollisionBox& build_box = building->get_collision_box();
    const float build_cx = build_box.position.x + build_box.width * 0.5f;
    const float build_cy = build_box.position.y + build_box.height * 0.5f;
    const float build_half_w = build_box.width * 0.5f;
    const float build_half_h = build_box.height * 0.5f;

    constexpr float UNIT_WIDTH = 32.0f;
    constexpr float UNIT_HEIGHT = 32.0f;
    constexpr float HALF_UNIT = UNIT_WIDTH * 0.5f;
    constexpr float GAP = 3.0f;        // 单位之间的额外间隙

    const float step = UNIT_WIDTH + GAP;
    const int num_circles = 3;
    // 第一圈紧贴建筑边缘，外圈逐步增加一个单位宽度
    const float base_offset = HALF_UNIT + 6.0f;   // 建筑边缘到槽位中心的距离

    auto* map = WorldEntityMgr::instance()->get_map();
    if (!map) return false;
    const int cell_size = map->get_cell_size();
    const float map_w = map->get_width() * cell_size;
    const float map_h = map->get_height() * cell_size;

    // 保存玩家颜色，工厂可能在创建时使用
    auto* ownership = building->get_component<Ownership>();
    const int player_id = ownership ? ownership->player_id : 0;

    for (int circle = 0; circle < num_circles; ++circle) {
        const float offset = base_offset + circle * UNIT_WIDTH;
        // 建筑外扩矩形，槽位中心需落在该矩形的四条边上
        const float left = build_box.position.x - offset;
        const float right = build_box.position.x + build_box.width + offset;
        const float top = build_box.position.y - offset;
        const float bottom = build_box.position.y + build_box.height + offset;

        // 收集本圈所有候选点（顺时针：上 → 右 → 下 → 左）
        std::vector<Vector2> candidates;

        // 上边：从左到右
        for (float x = left + HALF_UNIT; x <= right - HALF_UNIT; x += step) {
            candidates.emplace_back(x, top);
        }
        // 右边：从上到下
        for (float y = top + HALF_UNIT; y <= bottom - HALF_UNIT; y += step) {
            candidates.emplace_back(right, y);
        }
        // 下边：从右到左
        for (float x = right - HALF_UNIT; x >= left + HALF_UNIT; x -= step) {
            candidates.emplace_back(x, bottom);
        }
        // 左边：从下到上
        for (float y = bottom - HALF_UNIT; y >= top + HALF_UNIT; y -= step) {
            candidates.emplace_back(left, y);
        }

        // 顺时针顺序逐个尝试
        for (const Vector2& spawn_center : candidates) {
            CollisionBox spawn_box{ {spawn_center.x - HALF_UNIT, spawn_center.y - HALF_UNIT},
                                    UNIT_WIDTH, UNIT_HEIGHT };

            // 边界检查
            if (spawn_box.position.x < 0.0f || spawn_box.position.y < 0.0f ||
                spawn_box.position.x + spawn_box.width  > map_w ||
                spawn_box.position.y + spawn_box.height > map_h)
                continue;

            // 中心点地形可通行
            int cx = static_cast<int>(spawn_center.x / cell_size);
            int cy = static_cast<int>(spawn_center.y / cell_size);
            if (!map->is_cell_passable(cx, cy)) continue;

            // 不与现有实体重叠
            if (factory->check_overlap(spawn_box)) continue;

            // 创建单位
            factory->set_player_id(player_id);
            GameObject* unit = factory->create_unit_by_type(type, spawn_box, false);
            if (unit) return true;
        }
    }
    return false;  // 三圈全满
}

void ProductionSystem::on_update(float delta) {
    if (!factory) return;

    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj->check_valid()) continue;

        auto* queue = obj->get_component<ProductionQueue>();
        if (!queue || queue->queue.empty()) continue;

        auto& entry = queue->queue.front();
        entry.elapsed += delta;

        // 之前被冻结，新帧重试
        if (queue->frozen_flag) {
            queue->frozen_flag = false;
            if (try_spawn_unit_circles(obj, entry.unit_type, factory)) {
                queue->queue.erase(queue->queue.begin());
            }
            else {
                queue->frozen_flag = true;
            }
            continue;
        }

        if (entry.elapsed >= entry.total_time) {
            if (try_spawn_unit_circles(obj, entry.unit_type, factory)) {
                queue->queue.erase(queue->queue.begin());
            }
            else {
                queue->frozen_flag = true;
                entry.elapsed = entry.total_time;
            }
        }
    }
}