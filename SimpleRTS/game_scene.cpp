#include "game_scene.h"
#include "cursor_mgr.h"
#include "selection_mgr.h"

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::on_input(const SDL_Event& event)
{

    // 监听左键按下
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            float mx = event.button.x;
            float my = event.button.y;
            RenderMgr* rm = RenderMgr::instance();
            Vector2 mm_pos = rm->get_minimap_position();  // 需提供get接口
            float mm_w = rm->get_minimap_width();
            float mm_h = rm->get_minimap_height();

            if (mx >= mm_pos.x && mx <= mm_pos.x + mm_w &&
                my >= mm_pos.y && my <= mm_pos.y + mm_h)
            {
                // 转换到世界坐标
                float world_x = ((mx - mm_pos.x) / mm_w) * rm->get_world_width();
                float world_y = ((my - mm_pos.y) / mm_h) * rm->get_world_height();

                // 将相机中心对准该点（注意屏幕中心偏移）
                Vector2 new_cam_pos;
                new_cam_pos.x = world_x - camera.get_screen_w() / 2.0f / camera.get_scale();
                new_cam_pos.y = world_y - camera.get_screen_h() / 2.0f / camera.get_scale();
                camera.set_position(new_cam_pos);

                return; // 不触发框选
            }

            float mouse_x = event.button.x;
            float mouse_y = event.button.y;
            selection_box.on_start(mouse_x, mouse_y);
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            is_right_dragging = true;
            right_drag_start_position = { event.button.x, event.button.y };
            camera_start_position = camera.get_position();
        }
    }
    break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            bool hit_unit = selection_box.on_end(camera);

            // Normal 模式下，如果没点到单位，且有选中单位 → 移动
            if (!hit_unit &&
                SelectionMgr::instance()->get_current_mode() == SelectionMgr::SelectMode::Normal)
            {
                const auto& selected = SelectionMgr::instance()->get_selected_object_pool();
                if (!selected.empty())
                {
                    Vector2 raw_target = camera.screen_to_world({ event.button.x, event.button.y });
                    Vector2 cmd_center = game_map.find_nearest_passable(raw_target);

                    std::vector<GameObject*> selected_vec(selected.begin(), selected.end());
                    auto formation_targets = compute_formation_targets(selected_vec, cmd_center, &game_map);

                    for (GameObject* obj : selected) {
                        auto* movable = obj->get_component<Movable>();
                        if (movable) {
                            movable->target = formation_targets[obj];   // 个人精确点
                            movable->flow_target = cmd_center;               // 共享流场目标
                            SDL_Log("Set target: obj=%p, target=(%.1f,%.1f), flow_target=(%.1f,%.1f)",
                                obj, movable->target.x, movable->target.y,
                                movable->flow_target.x, movable->flow_target.y);
                            move_feedback_system.add_line_for_unit(obj, formation_targets[obj], 1.0f);
                        }
                    }
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (is_right_dragging)
            {
                is_right_dragging = false;
                // 如果鼠标移动距离很小（<5像素），视为“点击右键” → 清除选中
                float dx = event.button.x - right_drag_start_position.x;
                float dy = event.button.y - right_drag_start_position.y;
                if (fabsf(dx) < 5.0f && fabsf(dy) < 5.0f)
                {
                    SelectionMgr::instance()->clear();
                }
            }
        }
    }
    break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        if (is_right_dragging)
        {
            float dx = event.motion.x - right_drag_start_position.x;
            float dy = event.motion.y - right_drag_start_position.y;
            float scale = camera.get_scale();
            Vector2 new_pos = camera_start_position;
            new_pos.x -= dx / scale;
            new_pos.y -= dy / scale;
            camera.set_position(new_pos);
        }
        float mouse_x = event.motion.x;
        float mouse_y = event.motion.y;
        selection_box.on_update(mouse_x, mouse_y);
    }
    break;
    case SDL_EVENT_KEY_DOWN:
    {
        if (event.key.key == SDLK_S)
        {
            const auto& selected = SelectionMgr::instance()->get_selected_object_pool();
            for (GameObject* obj : selected)
            {
                auto* movable = obj->get_component<Movable>();
                if (movable)
                    movable->stop();       // 清空 target，停止移动
            }
        }
    }
    break;
    }
}

void GameScene::on_update(float delta) 
{
    const bool* keyState = SDL_GetKeyboardState(nullptr);

    // ctrl, alt
    is_key_ctrl_down = keyState[SDL_SCANCODE_LCTRL]
        || keyState[SDL_SCANCODE_RCTRL];
    is_key_alt_down = keyState[SDL_SCANCODE_LALT]
        || keyState[SDL_SCANCODE_RALT];

    // 相机方向键移动
    camera_input(keyState);

    if (is_key_ctrl_down)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
    if(is_key_alt_down)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
    if(!is_key_alt_down && !is_key_ctrl_down)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);

    camera_controller.on_update(delta);
    move_system.on_update(delta);
    move_feedback_system.on_update(delta);
    WorldEntityMgr::instance()->on_update();

}

void GameScene::on_enter()
{
    int win_w = 1280, win_h = 720;  
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
    RenderMgr::instance()->set_minimap_terrain(map_bake_tex.get_texture());

    move_system.set_map(&game_map);

    for (int i = 0;i < 10;i++)
    {
        float x = 250;
        float y = 100+i*50;
        float w = 32;
        float h = 32;
        CollisionBox collision_box{ {x,y},w,h };
        factory.create_unit(collision_box);
    }
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