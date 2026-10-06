#ifndef _MATCH_CONFIG_H_
#define _MATCH_CONFIG_H_

#include <string>
#include <vector>

// 玩家槽位类型：人类 / AI / 中立（缺少的玩家）
enum class SlotKind : int {
    Human = 0,
    AI,
    Neutral
};

// 单个玩家槽位配置（槽位下标 0..N-1 对应玩家 id = 下标 + 1）
struct PlayerSlot {
    SlotKind kind     = SlotKind::Neutral;
    int      color_id = 0;   // 颜色 0..7（见 player_palette.h）
    int      team     = 0;   // 阵营/队伍编号 0..(map_player_count-1)
    int      position = 0;   // 起始位置索引 0..(spawn_points.size()-1)
};

// 地图上一个起始位置（优先取地图上城镇中心，否则默认分布生成）
struct SpawnPoint {
    int  gx = 0, gy = 0;
    bool from_tc = false;   // 是否来自地图上实际摆放的城镇中心
};

// 对局/阵容配置：由 SelectorScene 填写，GameScene 进入对局时读取。
struct MatchConfig {
    std::string map_name = "map01";   // 地图文件名（不含扩展名）
    int map_player_count = 2;         // 玩家上限（来自 .srmap P 行，默认 2）
    int ai_difficulty     = 1;        // AI 难度：0=简单 1=普通 2=困难

    std::vector<SpawnPoint> spawn_points;   // 起始位置（数量 == 玩家上限）
    std::vector<PlayerSlot> slots;          // 每个槽位的配置（数量 == 玩家上限）

    // 人类槽位下标（-1 表示无）
    int human_slot() const {
        for (size_t i = 0; i < slots.size(); ++i)
            if (slots[i].kind == SlotKind::Human) return (int)i;
        return -1;
    }
    // AI 数量
    int ai_count() const {
        int n = 0;
        for (auto& s : slots) if (s.kind == SlotKind::AI) ++n;
        return n;
    }
};

#endif // !_MATCH_CONFIG_H_
