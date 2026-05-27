#include "game_scene.h"
#include "texture_cache.h"
#include "cursor_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "UI_mgr.h"
#include "rvo_adapter.h"

#include <chrono>

void GameScene::on_input(const SDL_Event& event)
{
    input_system.handle_event(event);
}

void GameScene::on_update(float delta)
{
    // === 计时代码开始 ===
    auto t0 = std::chrono::high_resolution_clock::now();
    input_system.on_update(delta);
    move_system.on_update(delta);
    harvest_system.on_update(delta);
    attack_system.on_update(delta);
    resource_submit_system.on_update(delta);
    render_system.on_update(delta);
    auto t1 = std::chrono::high_resolution_clock::now();

    move_feedback_system.on_update(delta);
    UIMgr::instance()->update_layout(screen_w_, screen_h_);
    UIMgr::instance()->update_content();
    auto t2 = std::chrono::high_resolution_clock::now();

    WorldEntityMgr::instance()->on_update();
    auto t3 = std::chrono::high_resolution_clock::now();
    // === 计时代码结束 ===

    // 计算耗时（毫秒）
    auto ms1 = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    auto ms2 = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
    auto ms3 = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;

    static int frame_counter = 0;
    //if (++frame_counter % 60 == 0) {  // 每60帧输出一次，避免刷屏
    //    SDL_Log("FrameTimings: Systems=%.3fms, Feedback&ui=%.3fms, WorldUpdate=%.3fms",
    //        ms1, ms2, ms3);
    //}
}

void GameScene::on_enter()
{
    int win_w = 1280, win_h = 720;
    RVOAdapter::instance()->init(&game_map);
    RVOAdapter::instance()->set_fixed_timestep(0.1f);

    camera.init((float)win_w, (float)win_h,
        (float)(game_map.get_width() * game_map.get_cell_size()),
        (float)(game_map.get_height() * game_map.get_cell_size()));

    camera_controller.set_camera(&camera);
    RenderMgr::instance()->set_camera(&camera);
    WorldEntityMgr::instance()->init_world(&game_map);
    RenderMgr::instance()->set_world_size(
        (float)(game_map.get_width() * game_map.get_cell_size()),
        (float)(game_map.get_height() * game_map.get_cell_size()));
    bake_terrain();
    RenderMgr::instance()->set_minimap_terrain(map_bake_tex.get_texture_id()); // 纹理 ID

    ResourcesMgr::instance()->init(3);
    ResourcesMgr::instance()->set_local_player_id(local_player_id);
    ResourcesMgr::instance()->set_player_team(0, 0);
    ResourcesMgr::instance()->set_player_team(1, 1);
    ResourcesMgr::instance()->set_player_team(2, 2);

    SelectionMgr::instance()->set_local_player_id(local_player_id);
    TextureCache::instance()->init(renderer, font);
    factory.init(&game_map);
    move_system.set_map(&game_map);
    attack_system.set_factory(&factory);

    // ★ 获取窗口并初始化输入系统
    SDL_Window* win = SDL_GetRenderWindow(renderer);  // SDL3 通过渲染器获取窗口
    input_system.init(&camera, &game_map, &selection_box, &move_feedback_system,
        local_player_id, win);           // 传入窗口

    update_ui_layout();

    factory.set_player_id(local_player_id);

    factory.create_town_center(60, 60);
    factory.create_resource_by_type(ResourceEntityType::SGold, 10, 10);
    factory.create_resource_by_type(ResourceEntityType::LGold, 30, 10);
    factory.create_resource_by_type(ResourceEntityType::Stone, 50, 10);
    factory.create_resource_by_type(ResourceEntityType::Wood, 70, 10);
    factory.create_resource_by_type(ResourceEntityType::Wood, 90, 10);
    factory.create_resource_by_type(ResourceEntityType::Berries, 120, 10);

    for (int i = 0; i < 1; i++) {
        float x = 250 + i / 10 * 50;
        float y = 100 + (i % 10) * 50;
        float w = 32, h = 32;
        CollisionBox collision_box{ {x, y}, w, h };
        factory.create_unit_by_type(UnitEntityType::Villager, collision_box);
    }

    for (int i = 0; i < 3; ++i) {
        CollisionBox box{ {500.0f, 400.0f + i * 50.0f}, 32, 32 };
        factory.create_archer(box);
    }

    factory.set_player_id(0);
    factory.create_unit_by_type(UnitEntityType::Villager, { {300.0f, 300.0f}, 32.0f, 32.0f });
    factory.create_unit_by_type(UnitEntityType::Villager, { {350.0f, 350.0f}, 32.0f, 32.0f });
    factory.create_town_center(60, 30);
}

void GameScene::on_exit()
{

}

void GameScene::on_render()
{
    map_bake_tex.render();
    render_system.on_render();
    move_feedback_system.on_render();
    selection_box.on_render();

    UIMgr::instance()->on_render();
}

void GameScene::camera_input(const bool* keyState)
{
    float angle = -1.0f; // 默认静止

    // 方向键控制 360° 角度
    bool left = keyState[SDL_SCANCODE_LEFT];
    bool right = keyState[SDL_SCANCODE_RIGHT];
    bool up = keyState[SDL_SCANCODE_UP];
    bool down = keyState[SDL_SCANCODE_DOWN];

    if (up && right)      angle = 315.0f;
    else if (up && left)  angle = 225.0f;
    else if (down && right) angle = 45.0f;
    else if (down && left)  angle = 135.0f;
    else if (up)          angle = 270.0f;
    else if (down)        angle = 90.0f;
    else if (left)        angle = 180.0f;
    else if (right)       angle = 0.0f;
    else                 angle = -1.0f;

    // 设置角度 → 相机自动移动
    camera_controller.set_angle(angle);
}

void GameScene::bake_terrain()
{
    if (map_baked) return;

    if (!renderer) {
        SDL_Log("bake_terrain: renderer is null!");
        return;
    }
        
    // SDL3 方式：检查最大纹理尺寸（非必须，但如果纹理过大可以提前发现）
    SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
    int max_tex_size = (int)SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    //SDL_Log("Renderer max texture size: %d", max_tex_size);

    int cell_size = game_map.get_cell_size();
    int total_w = game_map.get_width() * cell_size;
    int total_h = game_map.get_height() * cell_size;

    /*if (max_tex_size > 0 && (total_w > max_tex_size || total_h > max_tex_size)) {
        SDL_Log("bake_terrain: texture too large! (%dx%d > %d)", total_w, total_h, max_tex_size);
        return;
    }

    SDL_Log("baking terrain %dx%d", total_w, total_h);*/

    if (!map_bake_tex.create(total_w, total_h, renderer)) {
        //SDL_Log("map_bake_tex.create failed: %s", SDL_GetError());
        return;
    }

    // 绑定离屏纹理
    map_bake_tex.begin(renderer);
    //{
    //    SDL_Texture* current_target = SDL_GetRenderTarget(renderer);
    //    if (current_target != map_bake_tex.get_texture()) {
    //        SDL_Log("Failed to set render target! current=%p, expected=%p, error: %s",
    //            current_target, map_bake_tex.get_texture(), SDL_GetError());
    //    }
    //}

    // 清屏
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    /*if (SDL_RenderClear(renderer) != 0)
        SDL_Log("RenderClear failed: %s", SDL_GetError());*/

    // 绘制地形格子
    for (int y = 0; y < game_map.get_height(); ++y) {
        for (int x = 0; x < game_map.get_width(); ++x) {
            TerrainType t = game_map.get_grid()[y][x];
            SDL_FRect rect{
                (float)x * cell_size,
                (float)y * cell_size,
                (float)cell_size,
                (float)cell_size
            };
            if(t == TerrainType::Water)
                SDL_SetRenderDrawColor(renderer, 30, 80, 200, 255);
            else
                SDL_SetRenderDrawColor(renderer, 120, 80, 40, 255);     // 其余地形底部是棕色的Mud
            SDL_RenderFillRect(renderer, &rect);
        }
    }

    // 解绑
    map_bake_tex.end(renderer);
    map_baked = true;
    //SDL_Log("bake_terrain done, tex=%p (%dx%d)", map_bake_tex.get_texture(), total_w, total_h);
}

void GameScene::update_ui_layout()
{
    float mm_w = screen_w_ * minimap_width_percent;
    float mm_h = screen_h_ * minimap_height_percent;
    float margin = screen_w_ * minimap_margin_percent;

    // 让小地图区域为正方形（取宽和高中较小的一边）
    float mm_size = std::min(mm_w, mm_h);
    float mm_x = screen_w_ - mm_size - margin;
    float mm_y = screen_h_ - mm_size - margin;

    RenderMgr::instance()->set_minimap_size(mm_size, mm_size);
    RenderMgr::instance()->set_minimap_position(mm_x, mm_y);
    UIMgr::instance()->update_layout(screen_w_, screen_h_);
}