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

    // 返回 ID，不再返回裸指针
    uint32_t get_texture_id() const { return tex_id; }   

private:
    uint32_t tex_id = 0;          // 替代 SDL_Texture* tex
    int tex_w = 0;
    int tex_h = 0;
};

#endif // !_RENDER_TEXTURE_H_
