#include "render_texture.h"
#include "render_mgr.h"

RenderTexture::~RenderTexture()
{
    destroy();
}

bool RenderTexture::create(int width, int height, SDL_Renderer* renderer)
{
    destroy();
    tex_w = width;
    tex_h = height;

    // 创建可作为渲染目标的纹理
    tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width, height
    );
    if (!tex) return false;

    // 透明背景
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return true;
}

void RenderTexture::destroy()
{
    if (tex)
    {
        SDL_DestroyTexture(tex);
        tex = nullptr;
    }
}

void RenderTexture::begin(SDL_Renderer* renderer)
{
    SDL_SetRenderTarget(renderer, tex);
}

void RenderTexture::end(SDL_Renderer* renderer)
{
    SDL_SetRenderTarget(renderer, nullptr);
}

void RenderTexture::render()
{
    RenderCmd cmd{};
    cmd.layer = RenderLayer::Map;       
    cmd.position = { 0, 0 };
    cmd.w = (float)tex_w;            
    cmd.h = (float)tex_h;            
    cmd.texture = tex;                   

    RenderMgr::instance()->push_main_cmd(cmd);
}