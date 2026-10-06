#ifndef _PLAYER_PALETTE_H_
#define _PLAYER_PALETTE_H_

#include <SDL3/SDL.h>
#include "color.h"

// 玩家可选颜色数量（0..7）
constexpr int PLAYER_COLOR_COUNT = 8;

// 颜色索引 0..7 对应的 SDL 颜色（与阵营/小地图外框配色一致）
inline SDL_Color player_color_sdl(int idx) {
    switch (idx) {
    case 0: return { 80, 130, 255, 255 };   // 蓝
    case 1: return { 255, 90, 90, 255 };    // 红
    case 2: return { 90, 200, 90, 255 };    // 绿
    case 3: return { 240, 210, 60, 255 };   // 黄
    case 4: return { 180, 110, 220, 255 };  // 紫
    case 5: return { 255, 160, 60, 255 };   // 橙
    case 6: return { 70, 210, 210, 255 };   // 青
    case 7: return { 240, 110, 220, 255 };  // 品红
    default: return { 150, 150, 150, 255 }; // 灰
    }
}

// 颜色索引 0..7 对应的 Color 枚举（用于工厂着色）
inline Color player_color_enum(int idx) {
    switch (idx) {
    case 0: return Color::DarkBlue;
    case 1: return Color::DarkRed;
    case 2: return Color::LeafGreen;
    case 3: return Color::Gold;
    case 4: return Color::Purple;
    case 5: return Color::Orange;
    case 6: return Color::Cyan;
    case 7: return Color::Magenta;
    default: return Color::LightGray;
    }
}

// 颜色索引 0..7 对应的中文名
inline const char* player_color_name(int idx) {
    switch (idx) {
    case 0: return u8"蓝";
    case 1: return u8"红";
    case 2: return u8"绿";
    case 3: return u8"黄";
    case 4: return u8"紫";
    case 5: return u8"橙";
    case 6: return u8"青";
    case 7: return u8"品红";
    default: return u8"灰";
    }
}

#endif // !_PLAYER_PALETTE_H_
