#include "texture_cache.h"
#include "color.h"
#include <algorithm>

// 辅助：UTF-8 字符计数
static int utf8_char_count(const std::string& str)
{
    int count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        if (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else i += 4;
        ++count;
    }
    return count;
}

// 资源背景色（静态）
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

// 资源文字色（静态）
static SDL_Color get_resource_text_color(ResourceEntityType type)
{
    switch (type) {
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:  return to_sdl_color(Color::Gold);
    case ResourceEntityType::Stone:  return to_sdl_color(Color::Silver);
    case ResourceEntityType::Wood:   return to_sdl_color(Color::LeafGreen);
    case ResourceEntityType::Berries:return to_sdl_color(Color::Crimson);
    default:                         return to_sdl_color(Color::White);
    }
}

// 携带资源标记色（静态）
static SDL_Color get_resource_carry_color(ResourceType res_type)
{
    switch (res_type) {
    case ResourceType::Wood:  return to_sdl_color(Color::LeafGreen);
    case ResourceType::Food:  return to_sdl_color(Color::Crimson);
    case ResourceType::Gold:  return to_sdl_color(Color::Gold);
    case ResourceType::Stone: return to_sdl_color(Color::Silver);
    default:                  return to_sdl_color(Color::None);
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
    base_font = f;
}

void TextureCache::shutdown()
{
    // 销毁所有纹理
    for (auto& [id, tex] : texture_map) {
        if (tex) SDL_DestroyTexture(tex);
    }
    texture_map.clear();

    // 清空所有缓存（只存 ID，不需要额外销毁）
    resource_cache.clear();
    unit_cache.clear();
    carry_cache.clear();
    building_cache.clear();
    text_cache.clear();

    // 释放所有字体缓存（避免关闭 base_font 两次）
    for (auto& [size, f] : font_cache) {
        if (f && f != base_font) TTF_CloseFont(f);
    }
    font_cache.clear();
    if (base_font) {
        TTF_CloseFont(base_font);
        base_font = nullptr;
    }
    renderer = nullptr;
}

// ---- 纹理 ID 管理 ----
uint32_t TextureCache::register_texture(SDL_Texture* tex)
{
    if (!tex) return 0;
    uint32_t id = next_texture_id++;
    texture_map[id] = tex;
    return id;
}

SDL_Texture* TextureCache::get_texture_by_id(uint32_t id) const
{
    auto it = texture_map.find(id);
    return (it != texture_map.end()) ? it->second : nullptr;
}

void TextureCache::release_texture(uint32_t id)
{
    auto it = texture_map.find(id);
    if (it != texture_map.end()) {
        if (it->second) SDL_DestroyTexture(it->second);
        texture_map.erase(it);
    }
    // 从所有缓存中删除对应的 ID
    auto erase_id = [id](auto& cache) {
        for (auto iter = cache.begin(); iter != cache.end(); ++iter) {
            if (iter->second == id) {
                cache.erase(iter);
                return;
            }
        }
        };
    erase_id(resource_cache);
    erase_id(unit_cache);
    erase_id(carry_cache);
    erase_id(building_cache);
    erase_id(text_cache);
}

// ---- 字体管理 ----
TTF_Font* TextureCache::get_font(int size)
{
    if (size <= 0) return base_font;
    auto it = font_cache.find(size);
    if (it != font_cache.end() && it->second)
        return it->second;

    TTF_Font* new_font = TTF_OpenFont("font/SourceHanSansSC-Bold.otf", size);
    if (new_font) {
        font_cache[size] = new_font;
        return new_font;
    }
    return base_font;
}

// ---- 多行文字渲染（内部使用） ----
SDL_Surface* TextureCache::render_text_multiline(const std::string& text, SDL_Color color, int width, int height)
{
    if (!base_font || width <= 0 || height <= 0) return nullptr;

    const float inner_scale = 0.85f;
    int inner_w = (int)(width * inner_scale);
    int inner_h = (int)(height * inner_scale);
    if (inner_w < 1) inner_w = 1;
    if (inner_h < 1) inner_h = 1;

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
            SDL_Surface* text_surf = TTF_RenderText_Blended(sized_font, text.c_str(), 0, color);
            if (!text_surf) continue;
            if (text_surf->w <= inner_w && text_surf->h <= inner_h) {
                SDL_Rect dst = { (width - text_surf->w) / 2, (height - text_surf->h) / 2, text_surf->w, text_surf->h };
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

// ---- 资源纹理 ----
uint32_t TextureCache::get_resource_texture(ResourceEntityType type, int width, int height)
{
    ResourceKey key{ type, width, height };
    auto it = resource_cache.find(key);
    if (it != resource_cache.end()) {
        if (get_texture_by_id(it->second)) return it->second;
        resource_cache.erase(it);
    }
    SDL_Texture* tex = create_resource_texture(type, width, height);
    uint32_t id = register_texture(tex);
    if (id) resource_cache[key] = id;
    return id;
}

SDL_Texture* TextureCache::create_resource_texture(ResourceEntityType type, int width, int height)
{
    if (!renderer || !base_font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    SDL_Surface* bg = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);
    if (!bg) return nullptr;
    SDL_PixelFormat fmt = bg->format;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
    SDL_Rect full_rect = { 0, 0, width, height };

    SDL_Color bg_color = get_resource_bg_color(type);
    Uint32 bg_pixel = SDL_MapRGBA(details, nullptr, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_FillSurfaceRects(bg, &full_rect, 1, bg_pixel);

    std::string text = get_resource_name(type);
    SDL_Color text_color = get_resource_text_color(type);
    SDL_Surface* text_surf = render_text_multiline(text, text_color, width, height);
    if (text_surf) {
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

// ---- 单位纹理 ----
uint32_t TextureCache::get_unit_texture(UnitEntityType type, SDL_Color color, int width, int height)
{
    UnitKey key{ type, color.r | (color.g << 8) | (color.b << 16) | (color.a << 24), width, height };
    auto it = unit_cache.find(key);
    if (it != unit_cache.end()) {
        if (get_texture_by_id(it->second)) return it->second;
        unit_cache.erase(it);
    }
    SDL_Texture* tex = create_unit_texture(type, color, width, height);
    uint32_t id = register_texture(tex);
    if (id) unit_cache[key] = id;
    return id;
}

SDL_Texture* TextureCache::create_unit_texture(UnitEntityType type, SDL_Color color, int width, int height)
{
    if (!renderer || !base_font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    std::string text = get_unit_name(type);
    SDL_Surface* surf = render_text_multiline(text, color, width, height);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
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

// ---- 携带资源单位纹理 ----
uint32_t TextureCache::get_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type)
{
    CarryKey key{ type, color, width, height, res_type };
    auto it = carry_cache.find(key);
    if (it != carry_cache.end()) {
        if (get_texture_by_id(it->second)) return it->second;
        carry_cache.erase(it);
    }
    SDL_Texture* tex = create_carrying_unit_texture(type, color, width, height, res_type);
    uint32_t id = register_texture(tex);
    if (id) carry_cache[key] = id;
    return id;
}

SDL_Texture* TextureCache::create_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type)
{
    if (!renderer || !base_font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

    SDL_Surface* surf = render_text_multiline(get_unit_name(type), color, width, height);
    if (!surf) return nullptr;

    if (res_type != ResourceType::None) {
        SDL_Color mark_color = get_resource_carry_color(res_type);
        SDL_PixelFormat fmt = surf->format;
        const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(fmt);
        Uint32 pixel = SDL_MapRGBA(details, nullptr, mark_color.r, mark_color.g, mark_color.b, mark_color.a);
        SDL_Rect mark_rect = {
            width - width / 4, height - height / 4,
            width / 4, height / 4
        };
        SDL_FillSurfaceRects(surf, &mark_rect, 1, pixel);
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
}

// ---- 建筑纹理 ----
uint32_t TextureCache::get_building_texture(BuildingEntityType type, SDL_Color color, int width, int height)
{
    BuildingKey key{ type, color, width, height };
    auto it = building_cache.find(key);
    if (it != building_cache.end()) {
        if (get_texture_by_id(it->second)) return it->second;
        building_cache.erase(it);
    }
    SDL_Texture* tex = create_building_texture(type, color, width, height);
    uint32_t id = register_texture(tex);
    if (id) building_cache[key] = id;
    return id;
}

SDL_Texture* TextureCache::create_building_texture(BuildingEntityType type, SDL_Color color, int width, int height)
{
    if (!renderer || !base_font) return nullptr;
    if (width <= 0 || height <= 0) return nullptr;

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

    std::string text = get_building_name(type);
    SDL_Surface* text_surf = render_text_multiline(text, color, width, height);
    if (text_surf) {
        SDL_Rect dst = { 0, 0, width, height };
        SDL_BlitSurface(text_surf, nullptr, bg, &dst);
        SDL_DestroySurface(text_surf);
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, bg);
    SDL_DestroySurface(bg);
    return tex;
}

std::string TextureCache::get_building_name(BuildingEntityType type) const
{
    switch (type) {
    case BuildingEntityType::TownCenter: return u8"城镇大厅";
    default: return "?";
    }
}

// ---- 文字纹理 ----
uint32_t TextureCache::get_text_texture(const std::string& text, SDL_Color color, int font_size)
{
    TextKey key{ text, color, font_size };
    auto it = text_cache.find(key);
    if (it != text_cache.end()) {
        if (get_texture_by_id(it->second)) return it->second;
        text_cache.erase(it);
    }
    SDL_Texture* tex = create_text_texture(text, color, font_size);
    uint32_t id = register_texture(tex);
    if (id) text_cache[key] = id;
    return id;
}

uint32_t TextureCache::register_external_texture(SDL_Texture* tex)
{
    if (!tex) return 0;
    uint32_t id = next_texture_id++;
    texture_map[id] = tex;
    return id;
}

SDL_Texture* TextureCache::create_text_texture(const std::string& text, SDL_Color color, int font_size)
{
    TTF_Font* sized_font = get_font(font_size);
    if (!sized_font) return nullptr;

    SDL_Surface* surf = TTF_RenderText_Blended(sized_font, text.c_str(), 0, color);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
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