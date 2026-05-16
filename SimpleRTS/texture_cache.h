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

    // 初始化（在 main 或 on_enter 中调用一次）
    void init(SDL_Renderer* renderer, TTF_Font* font);

    // 获取或创建纹理
    SDL_Texture* get_resource_texture(ResourceEntityType type, int width, int height);
    SDL_Texture* get_unit_texture(UnitEntityType type, SDL_Color color, int width, int height);
    // 带携带资源的单位纹理
    SDL_Texture* get_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type);
    SDL_Texture* get_building_texture(BuildingEntityType type, SDL_Color color, int width, int height);

    void shutdown();

private:
    // 缓存键：类型 + 玩家ID + 尺寸
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
            auto combine = [&seed](size_t val) {
                seed ^= val + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            return seed;
        }
    };
    std::unordered_map<ResourceKey, SDL_Texture*, ResourceKeyHash> resource_cache;

    std::string get_resource_name(ResourceEntityType type) const;
    SDL_Texture* create_resource_texture(ResourceEntityType type, int width, int height);

private:
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
            auto combine = [&seed](size_t val) {
                seed ^= val + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.color));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            return seed;
        }
    };
    std::unordered_map<UnitKey, SDL_Texture*, UnitKeyHash> unit_cache;

    std::string get_unit_name(UnitEntityType type) const;
    SDL_Texture* create_unit_texture(UnitEntityType type, SDL_Color color, int width, int height);

private:
    struct CarryKey {
        UnitEntityType type;
        SDL_Color color;
        int width, height;
        ResourceType res_type;
        bool operator==(const CarryKey& o) const {
            return type == o.type && color.r == o.color.r && color.g == o.color.g && color.b == o.color.b && color.a == o.color.a &&
                width == o.width && height == o.height && res_type == o.res_type;
        }
    };
    struct CarryKeyHash {
        size_t operator()(const CarryKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.color.r));
            combine(static_cast<size_t>(k.color.g));
            combine(static_cast<size_t>(k.color.b));
            combine(static_cast<size_t>(k.color.a));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            combine(static_cast<size_t>(k.res_type));
            return seed;
        }
    };
    std::unordered_map<CarryKey, SDL_Texture*, CarryKeyHash> carry_cache;

    SDL_Texture* create_carrying_unit_texture(UnitEntityType type, SDL_Color color, int width, int height, ResourceType res_type);

private:
    struct BuildingKey {
        BuildingEntityType type;
        SDL_Color color;
        int width, height;
        bool operator==(const BuildingKey& o) const {
            return type == o.type &&
                color.r == o.color.r && color.g == o.color.g && color.b == o.color.b && color.a == o.color.a &&
                width == o.width && height == o.height;
        }
    };
    struct BuildingKeyHash {
        size_t operator()(const BuildingKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t v) { seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2); };
            combine((size_t)k.type);
            combine(k.color.r); combine(k.color.g); combine(k.color.b); combine(k.color.a);
            combine(k.width); combine(k.height);
            return seed;
        }
    };
    std::unordered_map<BuildingKey, SDL_Texture*, BuildingKeyHash> building_cache;

    std::string get_building_name(BuildingEntityType type) const;
    SDL_Texture* create_building_texture(BuildingEntityType type, SDL_Color color, int width, int height);

private:
    TextureCache() = default;
    ~TextureCache() { shutdown(); }

    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;   
    std::unordered_map<int, TTF_Font*> font_cache;
    TTF_Font* get_font(int size);

    // 工具函数
    SDL_Color get_player_color(int playerId) const;
    SDL_Surface* render_text_multiline(const std::string& text,
        SDL_Color color,
        int width, int height);
};

#endif // !_TEXTURE_CACHE_H_
