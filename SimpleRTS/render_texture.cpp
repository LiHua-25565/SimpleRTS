#include "render_texture.h"
#include "render_mgr.h"
#include "texture_cache.h"

RenderTexture::~RenderTexture()
{
    destroy();
}

bool RenderTexture::create(int width, int height, SDL_Renderer* renderer)
{
    destroy();
    tex_w = width;
    tex_h = height;

    SDL_Texture* raw_tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width, height
    );
    if (!raw_tex) return false;

    // 注册到全局纹理缓存
    tex_id = TextureCache::instance()->register_external_texture(raw_tex);

    // 设置为混合模式，以便透明背景生效
    SDL_SetTextureBlendMode(raw_tex, SDL_BLENDMODE_BLEND);
    return true;
}

void RenderTexture::destroy()
{
    if (tex_id != 0) {
        TextureCache::instance()->release_texture(tex_id);  // 销毁纹理并从缓存移除
        tex_id = 0;
    }
}

void RenderTexture::begin(SDL_Renderer* renderer)
{
    SDL_Texture* raw = TextureCache::instance()->get_texture_by_id(tex_id);
    if (raw) SDL_SetRenderTarget(renderer, raw);
}

void RenderTexture::end(SDL_Renderer* renderer)
{
    SDL_SetRenderTarget(renderer, nullptr);
}

void RenderTexture::render()
{
    if (tex_id == 0) return;
    // 通过 ID 创建 RenderCmd，这里改为使用纹理 ID
    RenderCmd cmd{};
    cmd.layer = RenderLayer::Map;
    cmd.position = { 0, 0 };
    cmd.w = (float)tex_w;
    cmd.h = (float)tex_h;
    cmd.texture_id = tex_id;      // 关键：设置纹理 ID
    RenderMgr::instance()->push_main_cmd(cmd);
}