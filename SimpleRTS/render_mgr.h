#ifndef _RENDER_MGR_H_
#define _RENDER_MGR_H_

#include "render_def.h"
#include "camera.h"
#include <vector>

class RenderMgr
{
public:
    static RenderMgr* instance();

    void set_camera(Camera* camera)
    {
        this->camera = camera;
    }

    // 每一帧开始清空指令队列
    void begin_frame();

    // 提交到主地图和小地图
    void push_cmd(const RenderCmd& cmd);

    // 提交到主视图
    void push_main_cmd(const RenderCmd& cmd);

    // 提交到小地图
    void push_minimap_cmd(const RenderCmd& cmd);

    // 帧结束：排序、批量统一渲染
    void end_frame(SDL_Renderer* renderer);

    void set_world_size(float width, float height)
    {
        world_w = width;
        world_h = height;
    }

    float get_world_width() const
    {
        return world_w;
    }

    float get_world_height() const
    {
        return world_h;
    }

    void set_minimap_position(float x, float y)
    {
        minimap_pos.x = x; 
        minimap_pos.y = y;
    }

    void set_minimap_position(const Vector2& position)
    {
        minimap_pos = position;
    }

    void set_minimap_size(float w, float h)
    {
        minimap_w = w; 
        minimap_h = h;
    }

    float get_minimap_width() const
    {
        return minimap_w;
    }

    float get_minimap_height() const
    {
        return minimap_h;
    }

    void set_minimap_terrain(SDL_Texture* tex) 
    { 
        minimap_terrain = tex; 
    }

    const Vector2& get_minimap_position() const
    {
        return minimap_pos;
    }

private:
    RenderMgr() = default;
    ~RenderMgr() = default;

    // 按渲染层级排序
    void sort_cmds();
    
    void render_main(SDL_Renderer* renderer);

    void render_minimap(SDL_Renderer* renderer);

private:
    Camera* camera = nullptr;
    SDL_Texture* minimap_terrain = nullptr;
    std::vector<RenderCmd> main_cmd_list;     // 主世界
    std::vector<RenderCmd> minimap_cmd_list;  // 小地图

    Vector2 minimap_pos;
    float minimap_w = 0;
    float minimap_h = 0;

    // 世界地图大小（必须设置）
    float world_w = 8000;
    float world_h = 8000;
};

#endif // !_RENDER_MGR_H_
