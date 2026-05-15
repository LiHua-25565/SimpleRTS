#ifndef _COLOR_H_
#define _COLOR_H_

#include <SDL3/SDL.h>

enum class Color {
    // 经典颜色
    Red,
    Orange,
    Yellow,
    Green,
    Cyan,
    Blue,
    Purple,

    // 黑、白、灰
    Black,
    White,
    Gray,

    // 额外常用颜色
    Pink,
    Brown,
    Lime,
    Magenta,
    Teal,
    Navy,

    // 新增游戏用色
    DarkGold,    // {80,60,20,255}
    Gold,        // {255,215,0,255}
    Silver,      // {220,220,220,255}
    LightGray,   // {200,200,200,255} (中立白)
    MediumGray,  // {60,60,60,255}
    DarkGray,    // {40,40,40,255} (深灰)
    DarkGreen,   // {30,80,30,255}
    LightGreen,  // {50,100,40,255}
    LeafGreen,   // {60,180,60,255}
    Crimson,     // {220,60,60,255}
    DarkBlue,    // {50,100,255,255}
    DarkRed,     // {255,80,80,255}
    SoftBlue,    // {100,120,200,255}
    SoftRed,     // {200,100,100,255}
    SelectionBlue, // {100,180,255,255}

    None
};

// 将 Color 转换为 SDL3 的 SDL_Color 
inline SDL_Color to_sdl_color(Color c) noexcept {
    switch (c) {
    case Color::Red:     return { 255, 0,   0,   255 };
    case Color::Orange:  return { 255, 165, 0,   255 };
    case Color::Yellow:  return { 255, 255, 0,   255 };
    case Color::Green:   return { 0,   255, 0,   255 };
    case Color::Cyan:    return { 0,   255, 255, 255 };
    case Color::Blue:    return { 0,   0,   255, 255 };
    case Color::Purple:  return { 128, 0,   128, 255 };

    case Color::Black:   return { 0,   0,   0,   255 };
    case Color::White:   return { 255, 255, 255, 255 };
    case Color::Gray:    return { 128, 128, 128, 255 };

    case Color::Pink:    return { 255, 192, 203, 255 };
    case Color::Brown:   return { 165, 42,  42,  255 };
    case Color::Lime:    return { 50,  205, 50,  255 };
    case Color::Magenta: return { 255, 0,   255, 255 };
    case Color::Teal:    return { 0,   128, 128, 255 };
    case Color::Navy:    return { 0,   0,   128, 255 };

    case Color::DarkGold:       return { 80,  60,  20,  255 };
    case Color::Gold:           return { 255, 215, 0,   255 };
    case Color::Silver:         return { 220, 220, 220, 255 };
    case Color::LightGray:      return { 200, 200, 200, 255 };
    case Color::MediumGray:     return { 60,60,60,255 };
    case Color::DarkGray:       return { 40,  40,  40,  255 };
    case Color::DarkGreen:      return { 30,  80,  30,  255 };
    case Color::LightGreen:     return { 50,  100, 40,  255 };
    case Color::LeafGreen:      return { 60,  180, 60,  255 };
    case Color::Crimson:        return { 220, 60,  60,  255 };
    case Color::DarkBlue:       return { 50,  100, 255, 255 };
    case Color::DarkRed:        return { 255, 80,  80,  255 };
    case Color::SoftBlue:       return { 100, 120, 200, 255 };
    case Color::SoftRed:        return { 200, 100, 100, 255 };
    case Color::SelectionBlue:  return { 100, 180, 255, 255 };

    case Color::None:    return { 0,0,0,0 };         
    default:    return { 0, 0, 0, 255 };  // 默认返回黑色
    }
}

#endif // !_COLOR_H_

