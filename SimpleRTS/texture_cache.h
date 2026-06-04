#ifndef _TEXTURE_CACHE_H_
#define _TEXTURE_CACHE_H_

#include "unit_type.h"
#include "resources_type.h"
#include "building_type.h"
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

class TextureCache
{
public:
    static TextureCache* instance();

    void init(SDL_Renderer* renderer, TTF_Font* base_font);
    void shutdown();

    // ---- 实体纹理（返回纹理 ID） ----
    uint32_t get_resource_texture(ResourceEntityType type, int width, int height);
    uint32_t get_unit_texture(UnitEntityType type, SDL_Color color, int width, int height);
    uint32_t get_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type);
    uint32_t get_building_texture(BuildingEntityType type, SDL_Color color, int width, int height);
    uint32_t get_research_texture(const std::string& name, int width, int height);

    // ---- 文字纹理（返回纹理 ID） ----
    uint32_t get_text_texture(const std::string& text, SDL_Color color, int font_size, int width = 0, int height = 0);
    bool get_texture_size(uint32_t id, float& w, float& h) const;

    // ---- 纹理 ID 操作 ----
    SDL_Texture* get_texture_by_id(uint32_t id) const;   // 通过 ID 获取裸指针（仅渲染时使用）
    void release_texture(uint32_t id);                    // 释放纹理，同时从缓存中移除

    // ---- 字体管理（暂保留裸指针，内部使用） ----
    TTF_Font* get_font(int font_size);

    // 注册外部创建的纹理，返回纹理 ID（不负责创建，仅接管生命周期）
    uint32_t register_external_texture(SDL_Texture* tex);

    // 辅助函数
    std::string get_resource_name(ResourceEntityType type) const;
    std::string get_unit_name(UnitEntityType type) const;
    std::string get_building_name(BuildingEntityType type) const;

private:
    TextureCache() = default;
    ~TextureCache() { shutdown(); }

    // 纹理注册与 ID 分配
    uint32_t register_texture(SDL_Texture* tex);

    SDL_Renderer* renderer = nullptr;
    TTF_Font* base_font = nullptr;

    // 纹理 ID 映射表
    std::unordered_map<uint32_t, SDL_Texture*> texture_map;
    uint32_t next_texture_id = 1;

    // 字体缓存（字号 → 字体指针）
    std::unordered_map<int, TTF_Font*> font_cache;

    // ---- 纹理缓存键（value 类型改为 uint32_t） ----
    struct ResourceKey {
        ResourceEntityType type;
        int width, height;
        bool operator==(const ResourceKey& o) const {
            return type == o.type && width == o.width && height == o.height;
        }
    };
    struct ResourceKeyHash {
        size_t operator()(const ResourceKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            return seed;
        }
    };
    std::unordered_map<ResourceKey, uint32_t, ResourceKeyHash> resource_cache;

    struct UnitKey {
        UnitEntityType type;
        Uint32 color;
        int width;
        int height;
        bool operator==(const UnitKey& o) const {
            return type == o.type && color == o.color && width == o.width && height == o.height;
        }
    };
    struct UnitKeyHash {
        size_t operator()(const UnitKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.color));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            return seed;
        }
    };
    std::unordered_map<UnitKey, uint32_t, UnitKeyHash> unit_cache;

    struct CarryKey {
        UnitEntityType type;
        SDL_Color color;
        int width, height;
        ResourceType res_type;
        bool operator==(const CarryKey& o) const {
            return type == o.type && color.r == o.color.r && color.g == o.color.g &&
                color.b == o.color.b && color.a == o.color.a &&
                width == o.width && height == o.height && res_type == o.res_type;
        }
    };
    struct CarryKeyHash {
        size_t operator()(const CarryKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine(static_cast<size_t>(k.type));
            combine(k.color.r); combine(k.color.g); combine(k.color.b); combine(k.color.a);
            combine(k.width); combine(k.height); combine(static_cast<size_t>(k.res_type));
            return seed;
        }
    };
    std::unordered_map<CarryKey, uint32_t, CarryKeyHash> carry_cache;

    struct BuildingKey {
        BuildingEntityType type;
        SDL_Color color;
        int width, height;
        bool operator==(const BuildingKey& o) const {
            return type == o.type &&
                color.r == o.color.r && color.g == o.color.g && color.b == o.color.b &&
                color.a == o.color.a && width == o.width && height == o.height;
        }
    };
    struct BuildingKeyHash {
        size_t operator()(const BuildingKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine(static_cast<size_t>(k.type));
            combine(k.color.r); combine(k.color.g); combine(k.color.b); combine(k.color.a);
            combine(k.width); combine(k.height);
            return seed;
        }
    };
    std::unordered_map<BuildingKey, uint32_t, BuildingKeyHash> building_cache;

    struct TextKey {
        std::string text;
        SDL_Color color;
        int font_size;
        int width = 0;   // 0 表示单行（自动忽略）
        int height = 0;
        bool operator==(const TextKey& o) const {
            return text == o.text && color.r == o.color.r && color.g == o.color.g &&
                color.b == o.color.b && color.a == o.color.a &&
                font_size == o.font_size && width == o.width && height == o.height;
        }
    };
    struct TextKeyHash {
        size_t operator()(const TextKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            for (char c : k.text) combine((size_t)c);
            combine(k.color.r); combine(k.color.g); combine(k.color.b); combine(k.color.a);
            combine(k.font_size);
            combine(k.width);
            combine(k.height);
            return seed;
        }
    };
    std::unordered_map<TextKey, uint32_t, TextKeyHash> text_cache;

    // ---- 内部创建函数（仍返回 SDL_Texture*，由 get 函数注册为 ID） ----
    SDL_Texture* create_resource_texture(ResourceEntityType type, int width, int height);
    SDL_Texture* create_unit_texture(UnitEntityType type, SDL_Color color, int width, int height);
    SDL_Texture* create_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type);
    SDL_Texture* create_building_texture(BuildingEntityType type, SDL_Color color, int width, int height);
    SDL_Texture* create_text_texture(const std::string& text, SDL_Color color, int font_size);

    // ---- 辅助函数 ----
    SDL_Color get_player_color(int playerId) const;
    SDL_Surface* render_text_multiline(const std::string& text, SDL_Color color, int width, int height);
};

#endif // !_TEXTURE_CACHE_H_