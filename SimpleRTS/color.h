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
    case Color::None:    return { 0,0,0,0 };

             // 默认返回黑色
    default:      return { 0, 0, 0, 255 };
    }
}

#endif // !_COLOR_H_

