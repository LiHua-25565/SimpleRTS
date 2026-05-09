#ifndef _RENDER_TEXTURE_H_
#define _RENDER_TEXTURE_H_

#include "collision_box.h"
#include <SDL3/SDL.h>

class RenderTexture
{
public:
    RenderTexture() = default;
    ~RenderTexture();

    // 创建离屏纹理
    bool create(int width, int height, SDL_Renderer* renderer);
    void destroy();

    // 绑定为绘制目标 / 解绑
    void begin(SDL_Renderer* renderer);
    void end(SDL_Renderer* renderer);

    // 绘制这张整张贴图到屏幕
    void render();

    SDL_Texture* get_texture() const
    {
        return tex;
    }

private:
    SDL_Texture* tex = nullptr;
    int tex_w = 0;
    int tex_h = 0;
};

#endif // !_RENDER_TEXTURE_H_
