#include "texture_cache.h"
#include "color.h"
#include <string>

// 辅助函数：根据资源类型返回暗色背景
static SDL_Color get_resource_bg_color(ResourceEntityType type)
{
    switch (type) {
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:  return to_sdl_color(Color::DarkGold);
    case ResourceEntityType::Stone:  return to_sdl_color(Color::MediumGray);
    case ResourceEntityType::Wood:   return to_sdl_color(Color::DarkGreen);
    case ResourceEntityType::Berries:return to_sdl_color(Color::LightGreen);
    default:                         return to_sdl_color(Color::DarkGray);
    }
}

// 根据资源类型返回文字颜色
static SDL_Color get_resource_text_color(ResourceEntityType type) {
    switch (type) {
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:  return to_sdl_color(Color::Gold);
    case ResourceEntityType::Stone:  return to_sdl_color(Color::Silver);
    case ResourceEntityType::Wood:   return to_sdl_color(Color::LeafGreen);
    case ResourceEntityType::Berries:return to_sdl_color(Color::Crimson);
    default:                         return to_sdl_color(Color::White);
    }
}

TextureCache* TextureCache::instance()
{
	static TextureCache mgr;
	return &mgr;
}

void TextureCache::init(SDL_Renderer* r, TTF_Font* f)
{
	renderer = r;
	font = f;
}

void TextureCache::shutdown()
{
    for (auto& [key, tex] : resource_cache)
        SDL_DestroyTexture(tex);
    resource_cache.clear();
    for (auto& [key, tex] : unit_cache)
        SDL_DestroyTexture(tex);
    unit_cache.clear();
    renderer = nullptr;
    font = nullptr;
}

SDL_Texture* TextureCache::get_resource_texture(ResourceEntityType type, int width, int height)
{
    ResourceKey key{ type,width, height };
    auto it = resource_cache.find(key);
    if (it != resource_cache.end())
        return it->second;

    SDL_Texture* tex = create_resource_texture(type, width, height);
    if (tex)
        resource_cache[key] = tex;
    return tex;
}

SDL_Texture* TextureCache::create_resource_texture(ResourceEntityType type, int width, int height)
{
    if (!renderer || !font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    // 1. 创建暗色背景表面
    SDL_Surface* bg = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!bg) return nullptr;

    SDL_PixelFormat pixel_format = bg->format;
    const SDL_PixelFormatDetails* format_details = SDL_GetPixelFormatDetails(pixel_format);
    SDL_Rect full_rect = { 0, 0, width, height };

    SDL_Color bg_color = get_resource_bg_color(type);
    Uint32 bg_pixel = SDL_MapRGBA(format_details, nullptr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_FillSurfaceRects(bg, &full_rect, 1, bg_pixel);

    // 2. 生成文字表面
    std::string text = get_resource_name(type);
    SDL_Color text_color = get_resource_text_color(type);
    SDL_Surface* text_surf = TTF_RenderText_Blended(font, text.c_str(), 0, text_color);
    if (text_surf) {
        // 3. 将文字 blit 到背景中央
        SDL_Rect dst = {
            (width - text_surf->w) / 2,
            (height - text_surf->h) / 2,
            text_surf->w,
            text_surf->h
        };
        SDL_BlitSurface(text_surf, nullptr, bg, &dst);
        SDL_DestroySurface(text_surf);
    }

    // 4. 转换为纹理
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, bg);
    SDL_DestroySurface(bg);
    SDL_Log("Created texture for %s: %p", get_resource_name(type).c_str(), tex);
    return tex;
}

std::string TextureCache::get_resource_name(ResourceEntityType type) const
{
    switch (type) {
    case ResourceEntityType::Wood:    return u8"木";
    case ResourceEntityType::SGold:   return u8"金矿";
    case ResourceEntityType::LGold:   return u8"金矿";
    case ResourceEntityType::Stone:   return u8"石头";
    case ResourceEntityType::Berries: return u8"浆果";
    default: return "?";
    }
}

std::string TextureCache::get_unit_name(UnitEntityType type) const
{
    switch (type) {
    case UnitEntityType::Villager:     return u8"民";
    case UnitEntityType::Cavalry:      return u8"骑";
    case UnitEntityType::Spearman:     return u8"矛";
    case UnitEntityType::Archer:       return u8"弓";
    case UnitEntityType::Crossbowman:  return u8"弩";
    default: return "?";
    }
}

// 创建单位纹理（透明底 + 阵营色边框 + 阵营色居中文字）
SDL_Texture* TextureCache::create_unit_texture(UnitEntityType type, SDL_Color color, int width, int height)
{
    if (!renderer || !font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    // 1. 创建透明表面
    SDL_Surface* surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return nullptr;

    SDL_PixelFormat fmt = surf->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    Uint32 transparent = SDL_MapRGBA(details, nullptr, 0, 0, 0, 0);

    // 填充全透明
    SDL_Rect full = { 0, 0, width, height };
    SDL_FillSurfaceRects(surf, &full, 1, transparent);

    // 2. 渲染居中文字（阵营色）
    std::string text = get_unit_name(type);
    SDL_Surface* textSurf = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (textSurf) {
        SDL_Rect dst = {
            (width - textSurf->w) / 2,
            (height - textSurf->h) / 2,
            textSurf->w,
            textSurf->h
        };
        SDL_BlitSurface(textSurf, nullptr, surf, &dst);
        SDL_DestroySurface(textSurf);
    }

    // 3. 转换为纹理
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
}

// 获取单位纹理（带缓存）
SDL_Texture* TextureCache::get_unit_texture(UnitEntityType type, SDL_Color color, int width, int height)
{
    UnitKey key{ type, color.r | (color.g << 8) | (color.b << 16) | (color.a << 24), width, height };
    auto it = unit_cache.find(key);
    if (it != unit_cache.end()) return it->second;

    SDL_Texture* tex = create_unit_texture(type, color, width, height);
    if (tex) unit_cache[key] = tex;
    return tex;
}

SDL_Color TextureCache::get_player_color(int playerId) const
{
    switch (playerId) {
    case 1:  return to_sdl_color(Color::DarkBlue);
    case 2:  return to_sdl_color(Color::DarkRed);
    default: return to_sdl_color(Color::LightGray);
    }
}
