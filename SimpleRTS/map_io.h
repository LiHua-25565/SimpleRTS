#ifndef _MAP_IO_H_
#define _MAP_IO_H_

#include <string>
#include <vector>
#include <cstdint>
#include "game_map.h"

// 一条地图实体记录（资源/建筑/单位的统一表示）。
// 编辑器以此作为权威数据，可序列化到 .srmap 文本文件。
struct EntityRecord {
    enum class Kind : uint8_t { Resource, Building, Unit };

    Kind kind = Kind::Resource;
    int  type   = 0;          // 底层枚举值：ResourceEntityType / BuildingEntityType / UnitEntityType
    int  gx = 0, gy = 0;      // 资源/建筑：网格坐标
    float wx = 0.0f, wy = 0.0f; // 单位：世界坐标
    int  player = 0;          // 归属玩家（建筑/单位）
    int  health = 0;          // 资源血量（建筑/单位忽略）
    uint64_t obj_id = 0;      // 运行期：对应 GameObject id（不参与序列化）
};

// 将地图（地形 + 实体列表 + 玩家数量）写入 path（.srmap）
bool save_map(const std::string& path, const GameMap& map, const std::vector<EntityRecord>& entities, int player_count);

// 从 path 读取地形（写入 map）与实体列表（追加到 entities）。
// 玩家数量写入 player_count（旧格式无此字段时默认为 2）。
// 要求文件的宽/高与 map 一致，否则返回 false。
bool load_map(const std::string& path, GameMap& map, std::vector<EntityRecord>& entities, int& player_count);

#endif // !_MAP_IO_H_
