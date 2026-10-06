#include "game_scene.h"
#include "texture_cache.h"
#include "cursor_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "UI_mgr.h"
#include "rvo_adapter.h"
#include "scene_ui.h"

#include <chrono>
#include <set>
#include <algorithm>
#include <cstdlib>

void GameScene::on_input(const SDL_Event& event)
{
    // Esc：游戏未结束时开关暂停菜单；游戏结束后忽略（只能点结算按钮）
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (!game_over_)
            paused_ = !paused_;
        return;
    }

    // 暂停或已结束：只响应覆盖层按钮点击，不再把事件交给游戏输入系统
    if (paused_ || game_over_) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            handle_overlay_click(event.button.x, event.button.y);
        return;
    }

    input_system.handle_event(event);
}

void GameScene::on_update(float delta)
{
    // 暂停或已结束：冻结所有游戏逻辑（AI 决策/移动/战斗/生产等）
    if (paused_ || game_over_)
        return;

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

    // 每帧判定胜负（需在 WorldEntityMgr::on_update 之后，此时被毁建筑已被清除）
    check_win_lose();

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
    // 重置胜负/暂停状态（重新开局时）
    paused_ = false;
    game_over_ = false;
    won_ = false;

    // 根据阵容配置确定人类玩家槽位（缺省回退到槽位 0，即玩家 1）
    int hs = match_config.human_slot();
    local_player_id = (hs >= 0) ? (hs + 1) : 1;

    // 清理上一局残留（重新开局时实体池/四叉树/动态障碍需归零）
    WorldEntityMgr::instance()->reset_world();
    game_map.clear_dynamic_obstacle_field();
    map_baked = false;

    // 载入选定地图（地形 + 实体 + 起始位置）
    load_selected_map();

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

    int player_count = match_config.map_player_count;
    if (player_count < 2) player_count = 2;
    ResourcesMgr::instance()->init(player_count + 1);
    ResourcesMgr::instance()->set_local_player_id(local_player_id);
    // 按槽位设置队伍（阵营）
    for (size_t i = 0; i < match_config.slots.size(); ++i)
        ResourcesMgr::instance()->set_player_team((int)i + 1, match_config.slots[i].team);

    SelectionMgr::instance()->set_local_player_id(local_player_id);
    TextureCache::instance()->init(renderer, font);
    factory.init(&game_map);
    factory.clear_color_overrides();
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

    // 铺设地图实体（资源/中立建筑/中立单位）
    spawn_map_entities();
    // 按阵容配置布场 + 注册 AI 玩家
    setup_match();

    // 相机对准人类基地
    center_camera_on_human();
}

void GameScene::load_selected_map()
{
    map_entities_.clear();
    int pc = 2;
    std::vector<EntityRecord> loaded;
    bool ok = false;
    if (!match_config.map_name.empty())
        ok = load_map_resize("maps/" + match_config.map_name + ".srmap", game_map, loaded, pc);
    if (ok) {
        map_entities_ = std::move(loaded);
        match_config.map_player_count = pc;
    }
    else {
        // 载入失败：保持默认地图（300x200，含中心水域），空实体
        pc = 2;
        match_config.map_player_count = pc;
        map_entities_.clear();
    }

    // 若槽位数量与玩家上限不符（例如未经过选择界面直接进入对局），重建默认槽位
    if ((int)match_config.slots.size() != match_config.map_player_count || match_config.human_slot() < 0) {
        match_config.slots.assign(match_config.map_player_count, PlayerSlot{});
        for (int i = 0; i < match_config.map_player_count; ++i) {
            match_config.slots[i].color_id = i % PLAYER_COLOR_COUNT;
            match_config.slots[i].position = i;
        }
        match_config.slots[0].kind = SlotKind::Human;
    }

    // 计算起始位置
    match_config.spawn_points = compute_spawn_points(game_map, map_entities_, match_config.map_player_count);
}

void GameScene::spawn_map_entities()
{
    int n = (int)match_config.slots.size();
    for (auto& r : map_entities_) {
        if (r.kind == EntityRecord::Kind::Resource) {
            factory.set_player_id(0);
            auto* obj = factory.create_resource_by_type((ResourceEntityType)r.type, r.gx, r.gy, false);
            if (obj) {
                auto* hp = obj->get_component<Health>();
                if (hp && r.health > 0) { hp->max_health = r.health; hp->current_health = r.health; }
            }
        }
        else if (r.kind == EntityRecord::Kind::Building) {
            if ((BuildingEntityType)r.type == BuildingEntityType::TownCenter) continue;   // 起始标记，由 setup_match 处理
            int owner = 0;
            Color c = Color::None;
            if (r.player >= 1 && r.player <= n && match_config.slots[r.player - 1].kind != SlotKind::Neutral) {
                owner = r.player;
                c = player_color_enum(match_config.slots[r.player - 1].color_id);
            }
            factory.set_player_id(owner);
            factory.set_player_color(owner, c);
            factory.create_building_by_type((BuildingEntityType)r.type, r.gx, r.gy, false);
        }
        else if (r.kind == EntityRecord::Kind::Unit) {
            int owner = 0;
            Color c = Color::None;
            if (r.player >= 1 && r.player <= n && match_config.slots[r.player - 1].kind != SlotKind::Neutral) {
                owner = r.player;
                c = player_color_enum(match_config.slots[r.player - 1].color_id);
            }
            factory.set_player_id(owner);
            factory.set_player_color(owner, c);
            float us = (float)(game_map.get_cell_size() * UNIT_SIZE_CELLS);
            factory.create_unit_by_type((UnitEntityType)r.type, { { r.wx, r.wy }, us, us }, false);
        }
    }
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

    int cs = game_map.get_cell_size();
    int n = (int)match_config.slots.size();
    int sp_count = (int)match_config.spawn_points.size();

    for (int i = 0; i < n; ++i) {
        const PlayerSlot& slot = match_config.slots[i];
        int pid = i + 1;

        if (slot.kind == SlotKind::Neutral) {
            // 中立玩家不生成城镇中心（"中立除外"）
            continue;
        }

        // 人类 / AI
        int pos = slot.position;
        if (pos < 0 || pos >= sp_count) pos = i;
        if (pos < 0 || pos >= sp_count) continue;
        const SpawnPoint& sp = match_config.spawn_points[pos];
        int tc_gx = sp.gx, tc_gy = sp.gy;

        factory.set_player_id(pid);
        factory.set_player_color(pid, player_color_enum(slot.color_id));
        give(pid, 150, 100);

        // 放置城镇中心；若因重叠/落水失败，螺旋向外偏移重试，保证每个玩家一定有基地
        {
            const int TC_CELLS = 20;
            if (!factory.create_town_center(tc_gx, tc_gy)) {
                int mw = game_map.get_width(), mh = game_map.get_height();
                int maxr = std::max(mw, mh);
                bool placed = false;
                for (int r = 1; r < maxr && !placed; ++r) {
                    for (int dy = -r; dy <= r && !placed; ++dy) {
                        for (int dx = -r; dx <= r && !placed; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                            int nx = tc_gx + dx, ny = tc_gy + dy;
                            if (nx < 0 || ny < 0 || nx + TC_CELLS > mw || ny + TC_CELLS > mh) continue;
                            if (factory.create_town_center(nx, ny)) {
                                tc_gx = nx; tc_gy = ny;
                                placed = true;
                            }
                        }
                    }
                }
            }
        }

        bool human = (slot.kind == SlotKind::Human);
        if (human) {
            // 靶场放在基地右侧
            factory.create_archery_range(tc_gx + 20, tc_gy);
        }

        // 资源：围绕基地铺一圈（创建失败自动跳过）
        struct ResDef { ResourceEntityType t; int ox, oy; };
        ResDef defs[] = {
            { ResourceEntityType::Wood, 22, -4 }, { ResourceEntityType::Wood, 30, -4 },
            { ResourceEntityType::SGold, 24, 6 }, { ResourceEntityType::Stone, 32, 6 },
            { ResourceEntityType::Berries, 22, -12 }, { ResourceEntityType::Wood, 38, 0 }
        };
        for (auto& d : defs)
            factory.create_resource_by_type(d.t, tc_gx + d.ox, tc_gy + d.oy, false);

        // 农民
        float us = (float)(cs * UNIT_SIZE_CELLS);
        Vector2 vbase{ (tc_gx + 22) * (float)cs, (tc_gy + 18) * (float)cs };
        for (int k = 0; k < 3; ++k)
            factory.create_unit_by_type(UnitEntityType::Villager,
                { { vbase.x + k * 60.0f, vbase.y }, us, us });

        // 人类额外弓兵
        if (human) {
            Vector2 abase{ (tc_gx + 22) * (float)cs, (tc_gy + 22) * (float)cs };
            for (int k = 0; k < 2; ++k)
                factory.create_archer({ { abase.x + k * 60.0f, abase.y }, us, us });
        }

        // 非人类槽位注册为 AI 玩家
        if (slot.kind == SlotKind::AI) {
            Vector2 rally{ (tc_gx + 10) * (float)cs, (tc_gy + 10) * (float)cs };
            ai_system.add_ai_player(pid, rally);
        }
    }
}

void GameScene::center_camera_on_human()
{
    int cs = game_map.get_cell_size();
    int hs = match_config.human_slot();
    int pos = 0;
    if (hs >= 0 && hs < (int)match_config.slots.size())
        pos = match_config.slots[hs].position;
    if (pos < 0 || pos >= (int)match_config.spawn_points.size()) pos = 0;

    const SpawnPoint& sp = match_config.spawn_points[pos];
    // 城镇中心 20x20 格，中心点
    float cx = (sp.gx + 10) * cs;
    float cy = (sp.gy + 10) * cs;

    camera.set_position({ cx - camera.get_screen_w() * 0.5f,
                          cy - camera.get_screen_h() * 0.5f });
}

void GameScene::on_exit()
{

}

void GameScene::check_win_lose()
{
    if (game_over_)
        return;

    // 收集参与对局的阵营集合（非中立玩家所属阵营）
    std::set<int> active_teams;
    for (auto& s : match_config.slots)
        if (s.kind != SlotKind::Neutral)
            active_teams.insert(s.team);

    // 仅一个阵营（或没有）不判定胜负（单阵营无对手，永不结束）
    if (active_teams.size() < 2)
        return;

    // 己方阵营
    int my_team = ResourcesMgr::instance()->get_team_id(local_player_id);

    // 统计当前仍有存活建筑的阵营
    std::set<int> alive_teams;
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj || !obj->check_valid()) continue;
        if (!obj->get_component<Structure>()) continue;   // 只统计建筑
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id <= 0) continue;        // 忽略中立建筑
        alive_teams.insert(own->team_id);
    }

    // 己方阵营所有建筑被毁 → 失败
    if (!alive_teams.count(my_team)) {
        game_over_ = true;
        won_ = false;
        return;
    }

    // 除己方外所有参与阵营均无存活建筑 → 胜利
    bool others_alive = false;
    for (int t : active_teams) {
        if (t == my_team) continue;
        if (alive_teams.count(t)) { others_alive = true; break; }
    }
    if (!others_alive) {
        game_over_ = true;
        won_ = true;
    }
}

void GameScene::handle_overlay_click(float x, float y)
{
    auto hit = [&](const SDL_FRect& r) {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    };

    if (paused_ && !game_over_) {
        // 暂停菜单：继续游戏 / 退出游戏
        if (hit(resume_btn_)) { paused_ = false; return; }
        if (hit(quit_btn_))  { paused_ = false; SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu); return; }
    }
    else if (game_over_) {
        // 结算：回到主菜单
        if (hit(quit_btn_)) { game_over_ = false; SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu); return; }
    }
}

void GameScene::render_overlay()
{
    const float cw = (float)screen_w_;
    const float ch = (float)screen_h_;

    // 半透明遮罩
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_FRect full{ 0, 0, cw, ch };
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // 中央面板
    const float panel_w = 420.0f;
    const float panel_h = 300.0f;
    SDL_FRect panel{ (cw - panel_w) * 0.5f, (ch - panel_h) * 0.5f, panel_w, panel_h };
    ui_fill_rect(renderer, panel, { 24, 30, 48, 255 }, { 255, 215, 0, 255 }, 2);

    // 标题与按钮布局（按钮矩形同时供 handle_overlay_click 命中）
    const float btn_w = 280.0f, btn_h = 56.0f;
    float ty = panel.y + 40.0f;

    if (paused_ && !game_over_) {
        ui_draw_text(renderer, u8"游戏暂停", 40, { 255, 215, 0, 255 }, panel.x + panel_w * 0.5f, ty, true);

        resume_btn_ = { panel.x + (panel_w - btn_w) * 0.5f, panel.y + 120.0f, btn_w, btn_h };
        quit_btn_   = { panel.x + (panel_w - btn_w) * 0.5f, panel.y + 196.0f, btn_w, btn_h };

        ui_fill_rect(renderer, resume_btn_, { 36, 48, 84, 255 }, { 90, 110, 160, 255 }, 2);
        ui_draw_text(renderer, u8"继续游戏", 28, { 255, 255, 255, 255 },
                     resume_btn_.x + resume_btn_.w * 0.5f, resume_btn_.y + resume_btn_.h * 0.5f, true);

        ui_fill_rect(renderer, quit_btn_, { 36, 48, 84, 255 }, { 90, 110, 160, 255 }, 2);
        ui_draw_text(renderer, u8"退出游戏", 28, { 255, 255, 255, 255 },
                     quit_btn_.x + quit_btn_.w * 0.5f, quit_btn_.y + quit_btn_.h * 0.5f, true);
    }
    else if (game_over_) {
        if (won_) {
            ui_draw_text(renderer, u8"胜利！", 56, { 255, 215, 0, 255 }, panel.x + panel_w * 0.5f, ty + 10.0f, true);
            ui_draw_text(renderer, u8"敌方阵营已被全灭", 22, { 200, 210, 230, 255 },
                         panel.x + panel_w * 0.5f, ty + 70.0f, true);
        }
        else {
            ui_draw_text(renderer, u8"失败", 56, { 230, 90, 90, 255 }, panel.x + panel_w * 0.5f, ty + 10.0f, true);
            ui_draw_text(renderer, u8"你的所有建筑已被摧毁", 22, { 220, 200, 200, 255 },
                         panel.x + panel_w * 0.5f, ty + 70.0f, true);
        }

        quit_btn_ = { panel.x + (panel_w - btn_w) * 0.5f, panel.y + 190.0f, btn_w, btn_h };
        ui_fill_rect(renderer, quit_btn_, { 36, 48, 84, 255 }, { 90, 110, 160, 255 }, 2);
        ui_draw_text(renderer, u8"回到主菜单", 26, { 255, 255, 255, 255 },
                     quit_btn_.x + quit_btn_.w * 0.5f, quit_btn_.y + quit_btn_.h * 0.5f, true);
    }
}

void GameScene::on_render()
{
    map_bake_tex.render();
    render_system.on_render();
    move_feedback_system.on_render();
    selection_box.on_render();

    UIMgr::instance()->on_render();
}

void GameScene::on_render_overlay()
{
    // 覆盖层（暂停菜单 / 胜负结算）绘制在最上层
    if (paused_ || game_over_)
        render_overlay();
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
    //    //    if (current_target != map_bake_tex.get_texture()) {
    //    //        SDL_Log("Failed to set render target! current=%p, expected=%p, error: %s",
    //    //            current_target, map_bake_tex.get_texture(), SDL_GetError());
    //    //    }
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
