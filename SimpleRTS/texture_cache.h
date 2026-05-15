#ifndef _TEXTURE_CACHE_H_
#define _TEXTURE_CACHE_H_

#include "resources_type.h"
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
    SDL_Texture* get_resource_texture(ResourceEntityType type, int playerId,
        int width, int height);

    void shutdown();

private:
    // 缓存键：类型 + 玩家ID + 尺寸
    struct CacheKey {
        ResourceEntityType type;
        int playerId;
        int width;
        int height;
        bool operator==(const CacheKey& o) const {
            return type == o.type && playerId == o.playerId
                && width == o.width && height == o.height;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const {
            size_t seed = 0;
            auto combine = [&seed](size_t val) {
                seed ^= val + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
            combine(static_cast<size_t>(k.type));
            combine(static_cast<size_t>(k.playerId));
            combine(static_cast<size_t>(k.width));
            combine(static_cast<size_t>(k.height));
            return seed;
        }
    };
    std::unordered_map<CacheKey, SDL_Texture*, CacheKeyHash> cache;

private:
    TextureCache() = default;
    ~TextureCache() { shutdown(); }

    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;   

    // 工具函数
    SDL_Texture* create_resource_texture(ResourceEntityType type, int playerId,
        int width, int height);
    std::string get_resource_name(ResourceEntityType type) const;
    SDL_Color get_player_color(int playerId) const;
};

#endif // !_TEXTURE_CACHE_H_
