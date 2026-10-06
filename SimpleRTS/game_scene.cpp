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
    ai_system.on_update(delta);          // AI 决策（下发移动/采集/生产/攻击命令）
    harvest_system.on_update(delta);
    attack_system.on_update(delta);
    production_system.on_update(delta);
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
    // 根据阵容配置确定人类玩家槽位（蓝方=槽位1，红方=槽位3）
    local_player_id = (match_config.human_team == 0) ? 1 : 3;

    // 清理上一局残留（重新开局时实体池/四叉树/动态障碍需归零）
    WorldEntityMgr::instance()->reset_world();
    game_map.clear_dynamic_obstacle_field();

    int win_w = 1280, win_h = 720;
    RVOAdapter::instance()->init(&game_map);
    // 20Hz 决策：0.1s 对 60px/s 的单位反应过迟钝
    RVOAdapter::instance()->set_fixed_timestep(0.05f);

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

    ResourcesMgr::instance()->init(5);
    ResourcesMgr::instance()->set_local_player_id(local_player_id);
    // 固定阵营：槽位 1/2 蓝队(0)，槽位 3/4 红队(1)；人类阵营决定自己落在哪个槽位
    ResourcesMgr::instance()->set_player_team(1, 0);
    ResourcesMgr::instance()->set_player_team(2, 0);
    ResourcesMgr::instance()->set_player_team(3, 1);
    ResourcesMgr::instance()->set_player_team(4, 1);

    SelectionMgr::instance()->set_local_player_id(local_player_id);
    TextureCache::instance()->init(renderer, font);
    factory.init(&game_map);
    move_system.set_map(&game_map);
    // 移动模式开关：Legacy=直线(基线) / FlowOnly=流场 / FlowRVO=流场+RVO
    // 出问题只需改这里一个枚举值即可降级回退
    move_system.set_move_mode(MoveModeKind::FlowRVO);
    attack_system.set_factory(&factory);
    production_system.set_factory(&factory);
    ai_system.set_factory(&factory);
    apply_ai_difficulty();
    ai_system.reset();
    UIMgr::instance()->on_placement_confirm = [this](BuildingEntityType type, int grid_x, int grid_y) {
        factory.set_player_id(local_player_id);
        factory.create_building_by_type(type, grid_x, grid_y, true);
        };

    // 获取窗口并初始化输入系统
    SDL_Window* win = SDL_GetRenderWindow(renderer);  // SDL3 通过渲染器获取窗口
    input_system.init(&camera, &game_map, &selection_box, &move_feedback_system,
        local_player_id, win);           // 传入窗口

    update_ui_layout();

    // 按阵容配置布场 + 注册 AI 玩家
    setup_match();

    // 相机对准人类基地
    center_camera_on_human();
}

void GameScene::apply_ai_difficulty()
{
    AISystem::AIConfig cfg;   // 默认即“普通”
    if (match_config.ai_difficulty == 0) {          // 简单：发育慢、进攻晚
        cfg.target_villagers = 8;
        cfg.wave_threshold = 10;
        cfg.first_wave_grace = 45.0f;
        cfg.attack_timeout = 60.0f;
    }
    else if (match_config.ai_difficulty == 2) {     // 困难：发育快、进攻猛
        cfg.target_villagers = 14;
        cfg.wave_threshold = 6;
        cfg.wave_growth = 1;
        cfg.first_wave_grace = 20.0f;
        cfg.attack_timeout = 40.0f;
        cfg.max_army = 30;
    }
    ai_system.set_config(cfg);
}

void GameScene::setup_match()
{
    auto give = [](int player, int wood, int food) {
        ResourcesMgr::instance()->set_resource(player, ResourceType::Wood, wood);
        ResourcesMgr::instance()->set_resource(player, ResourceType::Food, food);
        };

    // 四个出生槽位：城镇中心/资源用网格坐标，单位/集结点用世界坐标
    struct Res { ResourceEntityType type; int gx, gy; };
    struct Slot {
        int player, team;
        int tc_gx, tc_gy;                       // 城镇中心
        int ar_gx, ar_gy;                       // 靶场（仅人类槽位使用）
        std::vector<Res> resources;
        Vector2 v_origin; float v_dx; int v_count;   // 农民
        Vector2 a_origin; float a_dx; int a_count;   // 弓兵（仅人类槽位使用）
        Vector2 rally;                          // AI 集结/基地点（世界坐标）
    };

    Slot slots[4] = {
        { 1, 0, 60, 60, 100, 60,
          { {ResourceEntityType::SGold,10,10}, {ResourceEntityType::LGold,30,10},
            {ResourceEntityType::Stone,50,10}, {ResourceEntityType::Wood,70,10},
            {ResourceEntityType::Wood,90,10}, {ResourceEntityType::Berries,120,10} },
          { 250.0f, 400.0f }, 60.0f, 3, { 450.0f, 520.0f }, 60.0f, 2, { 600.0f, 600.0f } },

        { 2, 0, 60, 120, 0, 0,
          { {ResourceEntityType::Wood,10,140}, {ResourceEntityType::SGold,30,140},
            {ResourceEntityType::Berries,50,140}, {ResourceEntityType::Wood,70,140},
            {ResourceEntityType::Wood,90,140} },
          { 250.0f, 850.0f }, 60.0f, 3, { 0.0f, 0.0f }, 0.0f, 0, { 600.0f, 1200.0f } },

        { 3, 1, 220, 60, 180, 60,
          { {ResourceEntityType::Wood,200,10}, {ResourceEntityType::SGold,220,10},
            {ResourceEntityType::Berries,240,10}, {ResourceEntityType::Wood,200,30},
            {ResourceEntityType::Wood,220,30} },
          { 1950.0f, 400.0f }, 60.0f, 3, { 1800.0f, 520.0f }, 60.0f, 2, { 2200.0f, 600.0f } },

        { 4, 1, 220, 120, 0, 0,
          { {ResourceEntityType::Wood,200,140}, {ResourceEntityType::LGold,220,140},
            {ResourceEntityType::Berries,240,140}, {ResourceEntityType::Wood,200,160},
            {ResourceEntityType::Wood,220,160} },
          { 1950.0f, 850.0f }, 60.0f, 3, { 0.0f, 0.0f }, 0.0f, 0, { 2200.0f, 1200.0f } },
    };

    // 决定启用哪些槽位
    int human_slot = local_player_id;
    std::vector<int> active;
    if (match_config.total_players <= 2) {
        // 1v1：人类 + 一个对手
        int enemy = (human_slot == 1) ? 3 : 1;
        active = { human_slot, enemy };
    }
    else {
        active = { 1, 2, 3, 4 };
    }

    for (int pid : active) {
        const Slot* s = nullptr;
        for (auto& sl : slots) if (sl.player == pid) { s = &sl; break; }
        if (!s) continue;

        factory.set_player_id(s->player);
        give(s->player, 150, 100);
        factory.create_town_center(s->tc_gx, s->tc_gy);

        if (s->player == human_slot && s->ar_gx != 0)
            factory.create_archery_range(s->ar_gx, s->ar_gy);

        for (auto& r : s->resources)
            factory.create_resource_by_type(r.type, r.gx, r.gy);

        for (int i = 0; i < s->v_count; ++i)
            factory.create_unit_by_type(UnitEntityType::Villager,
                { { s->v_origin.x + i * s->v_dx, s->v_origin.y }, 32.0f, 32.0f });

        if (s->player == human_slot && s->a_count > 0)
            for (int i = 0; i < s->a_count; ++i)
                factory.create_archer({ { s->a_origin.x + i * s->a_dx, s->a_origin.y }, 32.0f, 32.0f });

        // 非人类槽位注册为 AI 玩家
        if (s->player != human_slot)
            ai_system.add_ai_player(s->player, s->rally);
    }
}

void GameScene::center_camera_on_human()
{
    int cs = game_map.get_cell_size();
    // 与 setup_match 中的城镇中心网格保持一致（槽位1/3 在上排，槽位2/4 在下排）
    int tc_gx = (local_player_id == 1 || local_player_id == 2) ? 60 : 220;
    int tc_gy = (local_player_id == 1 || local_player_id == 3) ? 60 : 120;

    // 城镇中心 20x20 格，中心点
    float cx = (tc_gx + 10) * cs;
    float cy = (tc_gy + 10) * cs;

    // 让基地中心位于屏幕中心（scale=1，屏幕为 1280x720 逻辑尺寸）
    camera.set_position({ cx - camera.get_screen_w() * 0.5f,
                          cy - camera.get_screen_h() * 0.5f });
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