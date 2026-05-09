#ifndef _RENDER_DEF_H_
#define _RENDER_DEF_H_

#include <SDL3/SDL.h>
#include "vector2.h"

// 渲染层级 由低到高
enum class RenderLayer
{
    Map,            // 离屏地图
    Ground,         // 地面、地形
    Unit,           // 单位
    Building,       // 建筑
    Projectile,     // 投掷物
    SelectBox,      // 选中框、框选矩形
    FeedbackLine,   // 指令反馈线
    UI              // 顶层UI
};

// 单条渲染指令
struct RenderCmd
{
    // 线条
    Vector2 line_start, line_end;
    bool is_line = false;

    // 矩形
    SDL_Texture* texture = nullptr;
    Vector2 position;
    float w = 0;
    float h = 0;
    RenderLayer layer = RenderLayer::Ground;
    SDL_Color color{ 255,255,255,255 };

    SDL_Color border_color = { 0,0,0,0 };  // 边框颜色
    int border_width = 0;                  // 边框宽度（0=不绘制）不建议太大！

    // 专门用于小地图层级的选中标记
    bool is_selected = false;
};

#endif // !_RENDER_DEF_H_