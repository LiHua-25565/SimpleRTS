#include "texture_cache.h"
#include "color.h"
#include <string>
#include <algorithm>

// 辅助函数：字数大于3自动分两行
static int utf8_char_count(const std::string& str)
{
    int count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        if (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;   // 中文占 3 字节
        else i += 4;
        ++count;
    }
    return count;
}

SDL_Surface* TextureCache::render_text_multiline(const std::string& text,
    SDL_Color color,
    int width, int height)
{
    if (!font || width <= 0 || height <= 0) return nullptr;

    // 内边距比例：文字最大宽度/高度为矩形的 85%
    const float inner_scale = 0.85f;
    int inner_w = (int)(width * inner_scale);
    int inner_h = (int)(height * inner_scale);
    if (inner_w < 1) inner_w = 1;
    if (inner_h < 1) inner_h = 1;

    // 创建最终表面（透明背景）
    SDL_Surface* canvas = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!canvas) return nullptr;
    SDL_PixelFormat fmt = canvas->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    Uint32 transparent = SDL_MapRGBA(details, nullptr, 0, 0, 0, 0);
    SDL_Rect full = { 0, 0, width, height };
    SDL_FillSurfaceRects(canvas, &full, 1, transparent);

    int char_count = utf8_char_count(text);
    bool two_lines = (char_count >= 4);

    int max_height = two_lines ? inner_h / 2 : inner_h;
    int init_size = std::clamp((int)(max_height * 0.8f), 10, 48);

    for (int size = init_size; size >= 8; --size) {
        TTF_Font* sized_font = get_font(size);
        if (!sized_font) continue;

        if (two_lines) {
            // 分两行
            int line1_chars = (char_count + 1) / 2;
            std::string line1, line2;
            const char* p = text.c_str();
            int cnt = 0;
            while (cnt < line1_chars && *p) {
                unsigned char c = *p;
                int bytes = 1;
                if (c >= 0xE0 && c < 0xF0) bytes = 3;
                else if (c >= 0xF0) bytes = 4;
                else if (c >= 0xC0 && c < 0xE0) bytes = 2;
                line1.append(p, bytes);
                p += bytes;
                cnt++;
            }
            line2 = p;

            SDL_Surface* s1 = TTF_RenderText_Blended(sized_font, line1.c_str(), 0, color);
            SDL_Surface* s2 = TTF_RenderText_Blended(sized_font, line2.c_str(), 0, color);
            if (!s1 || !s2) {
                if (s1) SDL_DestroySurface(s1);
                if (s2) SDL_DestroySurface(s2);
                continue;
            }

            int total_h = s1->h + s2->h + 4;
            int max_w = std::max(s1->w, s2->w);
            if (max_w <= inner_w && total_h <= inner_h) {
                // 居中放置（基于原始宽高）
                int y1 = (height - total_h) / 2;
                int y2 = y1 + s1->h + 4;
                SDL_Rect dst1 = { (width - s1->w) / 2, y1, s1->w, s1->h };
                SDL_Rect dst2 = { (width - s2->w) / 2, y2, s2->w, s2->h };
                SDL_BlitSurface(s1, nullptr, canvas, &dst1);
                SDL_BlitSurface(s2, nullptr, canvas, &dst2);
                SDL_DestroySurface(s1);
                SDL_DestroySurface(s2);
                return canvas;
            }
            SDL_DestroySurface(s1);
            SDL_DestroySurface(s2);
        }
        else {
            // 单行
            SDL_Surface* text_surf = TTF_RenderText_Blended(sized_font, text.c_str(), 0, color);
            if (!text_surf) continue;
            if (text_surf->w <= inner_w && text_surf->h <= inner_h) {
                SDL_Rect dst = { (width - text_surf->w) / 2,
                                 (height - text_surf->h) / 2,
                                 text_surf->w, text_surf->h };
                SDL_BlitSurface(text_surf, nullptr, canvas, &dst);
                SDL_DestroySurface(text_surf);
                return canvas;
            }
            SDL_DestroySurface(text_surf);
        }
    }

    SDL_DestroySurface(canvas);
    return nullptr;
}

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
    for (auto& [k, tex] : carry_cache) 
        SDL_DestroyTexture(tex);
    carry_cache.clear();

    font_cache.clear();

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

    // 1. 创建背景表面
    SDL_Surface* bg = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!bg) return nullptr;
    SDL_PixelFormat fmt = bg->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    SDL_Rect full_rect = { 0, 0, width, height };
    SDL_Color bg_color = get_resource_bg_color(type);
    Uint32 bg_pixel = SDL_MapRGBA(details, nullptr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_FillSurfaceRects(bg, &full_rect, 1, bg_pixel);

    // 2. 生成多行文字表面
    std::string text = get_resource_name(type);
    SDL_Color text_color = get_resource_text_color(type);
    SDL_Surface* text_surf = render_text_multiline(text, text_color, width, height);
    if (text_surf) {
        // 文字表面已是居中透明背景，直接 blit 到背景上
        SDL_Rect dst = { 0, 0, width, height };
        SDL_BlitSurface(text_surf, nullptr, bg, &dst);
        SDL_DestroySurface(text_surf);
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, bg);
    SDL_DestroySurface(bg);
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

    // 直接使用 render_text_multiline，因为它返回透明背景表面
    std::string text = get_unit_name(type);
    SDL_Surface* surf = render_text_multiline(text, color, width, height);
    if (!surf) return nullptr;

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

SDL_Color get_resource_color(ResourceType res_type) {
    // 返回资源对应的背景色（用于右下角填充）
    switch (res_type) {
    case ResourceType::Wood:  return to_sdl_color(Color::LeafGreen);   // 亮绿
    case ResourceType::Food:  return to_sdl_color(Color::Crimson);     // 红
    case ResourceType::Gold:  return to_sdl_color(Color::Gold);        // 金色
    case ResourceType::Stone: return to_sdl_color(Color::Silver);      // 亮灰
    default:                  return to_sdl_color(Color::None);
    }
}

SDL_Texture* TextureCache::create_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type) {
    if (!renderer || !font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    // 先创建普通透明文字纹理（不带任何背景色）
    SDL_Surface* surf = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return nullptr;

    SDL_PixelFormat fmt = surf->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    Uint32 transparent = SDL_MapRGBA(details, nullptr, 0, 0, 0, 0);
    SDL_Rect full = { 0,0,width,height };
    SDL_FillSurfaceRects(surf, &full, 1, transparent);

    // 如果有携带资源，绘制右下角四分之一区域填充资源色
    if (res_type != ResourceType::None) {
        SDL_Color mark_color = get_resource_color(res_type);
        SDL_PixelFormat fmt = surf->format;
        const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
        Uint32 pixel = SDL_MapRGBA(details, nullptr, mark_color.r, mark_color.g, mark_color.b, mark_color.a);

        // 右下角四分之一区域
        SDL_Rect mark_rect = {
            width - width / 4, height - height / 4,
            width / 4, height / 4
        };
        SDL_FillSurfaceRects(surf, &mark_rect, 1, pixel);
    }

    // 绘制文字（居中）
    std::string text = get_unit_name(type);
    SDL_Surface* text_surf = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (text_surf) {
        SDL_Rect dst = { (width - text_surf->w) / 2, (height - text_surf->h) / 2, text_surf->w, text_surf->h };
        SDL_BlitSurface(text_surf, nullptr, surf, &dst);
        SDL_DestroySurface(text_surf);
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
}

TTF_Font* TextureCache::get_font(int size)
{
    auto it = font_cache.find(size);
    if (it != font_cache.end() && it->second)
        return it->second;

    TTF_Font* new_font = TTF_OpenFont("font/SourceHanSansSC-Bold.otf", size);
    if (new_font) {
        font_cache[size] = new_font;
        return new_font;
    }
    return font;  // 回退到默认字体
}

std::string TextureCache::get_building_name(BuildingEntityType type) const
{
    switch (type) {
    case BuildingEntityType::TownCenter: return u8"城镇大厅";
    default: return "?";
    }
}

SDL_Texture* TextureCache::create_building_texture(BuildingEntityType type, SDL_Color color, int width, int height)
{
    if (!renderer || !font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    // 1. 创建背景表面，填充阵营暗色
    SDL_Surface* bg = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!bg) return nullptr;

    SDL_PixelFormat fmt = bg->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    Uint8 dr = (Uint8)(color.r * 0.4f);
    Uint8 dg = (Uint8)(color.g * 0.4f);
    Uint8 db = (Uint8)(color.b * 0.4f);
    Uint32 bg_pixel = SDL_MapRGBA(details, nullptr, dr, dg, db, 255);
    SDL_Rect full = { 0, 0, width, height };
    SDL_FillSurfaceRects(bg, &full, 1, bg_pixel);

    // 2. 生成多行文字表面（字体自适应，文字居中）
    std::string text = get_building_name(type);
    SDL_Surface* text_surf = render_text_multiline(text, color, width, height);
    if (text_surf) {
        // 文字表面是透明背景且居中，直接覆盖到背景上
        SDL_Rect dst = { 0, 0, width, height };
        SDL_BlitSurface(text_surf, nullptr, bg, &dst);
        SDL_DestroySurface(text_surf);
    }

    // 3. 转换为纹理
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, bg);
    SDL_DestroySurface(bg);
    return tex;
}

SDL_Texture* TextureCache::get_building_texture(BuildingEntityType type, SDL_Color color, int width, int height)
{
    // 缓存键：类型 + 颜色 + 尺寸
    BuildingKey key{ type, color, width, height };
    auto it = building_cache.find(key);
    if (it != building_cache.end()) return it->second;

    SDL_Texture* tex = create_building_texture(type, color, width, height);
    if (tex) building_cache[key] = tex;
    return tex;
}

SDL_Texture* TextureCache::get_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type) {
    CarryKey key{ type, color, width, height, res_type };
    auto it = carry_cache.find(key);
    if (it != carry_cache.end()) return it->second;
    SDL_Texture* tex = create_carrying_unit_texture(type, color, width, height, res_type);
    if (tex) carry_cache[key] = tex;
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
