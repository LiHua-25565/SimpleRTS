#include "texture_cache.h"
#include <string>

// 辅助函数：根据资源类型返回暗色背景
static SDL_Color get_resource_bg_color(ResourceEntityType type)
{
    switch (type) {
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:
        return { 80, 60, 20, 255 };   // 暗金色
    case ResourceEntityType::Stone:
        return { 60, 60, 60, 255 };   // 深灰色
    case ResourceEntityType::Wood:
        return { 30, 80, 30, 255 };   // 暗绿色
    case ResourceEntityType::Berries:
        return { 50, 100, 40, 255 };  // 亮一点的绿
    default:
        return { 40, 40, 40, 255 };   // 默认深灰
    }
}

// 根据资源类型返回文字颜色
static SDL_Color get_resource_text_color(ResourceEntityType type)
{
    switch (type) {
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:
        return { 255, 215, 0, 255 };   // 金色
    case ResourceEntityType::Stone:
        return { 220, 220, 220, 255 }; // 亮灰色
    case ResourceEntityType::Wood:
        return { 60, 180, 60, 255 };   // 绿色
    case ResourceEntityType::Berries:
        return { 220, 60, 60, 255 };   // 红色
    default:
        return { 255, 255, 255, 255 }; // 白色
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
    for (auto& [key, tex] : cache)
        SDL_DestroyTexture(tex);
    cache.clear();
    renderer = nullptr;
    font = nullptr;
}

SDL_Texture* TextureCache::get_resource_texture(ResourceEntityType type, int playerId,
    int width, int height)
{
    CacheKey key{ type, playerId, width, height };
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    SDL_Texture* tex = create_resource_texture(type, playerId, width, height);
    if (tex)
        cache[key] = tex;
    return tex;
}

SDL_Texture* TextureCache::create_resource_texture(ResourceEntityType type, int playerId,
    int width, int height)
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

SDL_Color TextureCache::get_player_color(int playerId) const
{
    // TODO: 根据玩家ID返回阵营颜色
    // 预留：0 中立白色，1 玩家1蓝色，2 玩家2红色等
    return { 255, 255, 255, 255 };
}