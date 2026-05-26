#ifndef _RENDER_MGR_H_
#define _RENDER_MGR_H_

#include "render_def.h"
#include "camera.h"
#include <vector>

class RenderMgr
{
public:
    static RenderMgr* instance();

    void set_camera(Camera* camera);
    void begin_frame();
    void push_cmd(const RenderCmd& cmd);
    void push_main_cmd(const RenderCmd& cmd);
    void push_minimap_cmd(const RenderCmd& cmd);
    void end_frame(SDL_Renderer* renderer);

    void set_world_size(float width, float height);
    float get_world_width() const;
    float get_world_height() const;

    void set_minimap_position(float x, float y);
    void set_minimap_position(const Vector2& position);
    void set_minimap_size(float w, float h);
    float get_minimap_width() const;
    float get_minimap_height() const;
    void set_minimap_terrain(uint32_t tex_id);
    const Vector2& get_minimap_position() const;

    void set_minimap_content_rect(const SDL_FRect& rect);
    const SDL_FRect& get_minimap_content_rect() const;
    void set_sell_size(int sell_size);

private:
    RenderMgr() = default;
    ~RenderMgr() = default;

    void sort_cmds();
    void render_main(SDL_Renderer* renderer);
    void render_minimap(SDL_Renderer* renderer);
    void update_minimap_content_rect();

    Camera* camera = nullptr;
    uint32_t minimap_terrain_id = 0;
    SDL_FRect minimap_content_rect = { 0 };
    std::vector<RenderCmd> main_cmd_list;
    std::vector<RenderCmd> minimap_cmd_list;

    int cell_size = 10;

    Vector2 minimap_pos;
    float minimap_w = 0;
    float minimap_h = 0;

    float world_w = 8000;
    float world_h = 8000;
};

#endif // !_RENDER_MGR_H_
