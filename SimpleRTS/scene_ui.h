#ifndef _SCENE_UI_H_
#define _SCENE_UI_H_

#include <SDL3/SDL.h>
#include <string>
#include "texture_cache.h"

// 逻辑分辨率（与 main.cpp 中 SDL_SetRenderLogicalPresentation 对齐）。
// 菜单/选择场景按逻辑坐标布局与命中判定，鼠标事件已被转换到逻辑坐标。
constexpr float LOGICAL_W = 1280.0f;
constexpr float LOGICAL_H = 720.0f;

// 绘制实心矩形（可带边框）
inline void ui_fill_rect(SDL_Renderer* r, const SDL_FRect& rect, SDL_Color fill,
                         SDL_Color border = { 0,0,0,0 }, int border_w = 0)
{
    if (!r) return;
    if (fill.a > 0) {
        SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(r, &rect);
    }
    if (border_w > 0 && border.a > 0) {
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        SDL_FRect top{ rect.x, rect.y, rect.w, (float)border_w };
        SDL_FRect bot{ rect.x, rect.y + rect.h - border_w, rect.w, (float)border_w };
        SDL_FRect lft{ rect.x, rect.y, (float)border_w, rect.h };
        SDL_FRect rgt{ rect.x + rect.w - border_w, rect.y, (float)border_w, rect.h };
        SDL_RenderFillRect(r, &top);
        SDL_RenderFillRect(r, &bot);
        SDL_RenderFillRect(r, &lft);
        SDL_RenderFillRect(r, &rgt);
    }
}

// 绘制文字；center=true 时 (x,y) 为文字中心，否则为左上角
inline void ui_draw_text(SDL_Renderer* r, const std::string& text, int font_size,
                         SDL_Color color, float x, float y, bool center = false)
{
    if (!r || text.empty()) return;
    uint32_t id = TextureCache::instance()->get_text_texture(text, color, font_size);
    if (!id) return;
    SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(id);
    if (!tex) return;
    float w = 0.0f, h = 0.0f;
    if (!SDL_GetTextureSize(tex, &w, &h)) return;
    SDL_FRect dst;
    if (center) dst = SDL_FRect{ x - w * 0.5f, y - h * 0.5f, w, h };
    else        dst = SDL_FRect{ x, y, w, h };
    SDL_RenderTexture(r, tex, nullptr, &dst);
}

// 绘制文字：水平左对齐(x)，垂直以 cy 为中心（用于输入框内文字，避免压出边框）
inline void ui_draw_text_vcenter(SDL_Renderer* r, const std::string& text, int font_size,
                                 SDL_Color color, float x, float cy)
{
    if (!r || text.empty()) return;
    uint32_t id = TextureCache::instance()->get_text_texture(text, color, font_size);
    if (!id) return;
    SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(id);
    if (!tex) return;
    float w = 0.0f, h = 0.0f;
    if (!SDL_GetTextureSize(tex, &w, &h)) return;
    SDL_FRect dst{ x, cy - h * 0.5f, w, h };
    SDL_RenderTexture(r, tex, nullptr, &dst);
}

#endif // !_SCENE_UI_H_
