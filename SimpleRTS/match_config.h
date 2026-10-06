#ifndef _MATCH_CONFIG_H_
#define _MATCH_CONFIG_H_

// 对局/阵容配置：由 SelectorScene 填写，GameScene 进入对局时读取。
struct MatchConfig {
    int human_team    = 0;   // 人类阵营：0=蓝方 1=红方（决定人类起始位置/队伍）
    int total_players = 4;   // 总玩家数：2=1v1, 4=2v2
    int ai_difficulty = 1;   // AI 难度：0=简单 1=普通 2=困难
};

#endif // !_MATCH_CONFIG_H_
