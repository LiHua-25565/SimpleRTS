#include "game_scene.h"
#include "cursor_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "UI_mgr.h"

#include <chrono>

// 辅助函数
// 判断点是否在小地图区域内
bool GameScene::is_point_in_minimap(float x, float y) const
{
    RenderMgr* rm = RenderMgr::instance();
    Vector2 mm_pos = rm->get_minimap_position();
    float mm_w = rm->get_minimap_width();
    float mm_h = rm->get_minimap_height();
    return (x >= mm_pos.x && x <= mm_pos.x + mm_w &&
        y >= mm_pos.y && y <= mm_pos.y + mm_h);
}

// 将小地图坐标转换为世界坐标
Vector2 GameScene::minimap_to_world(float x, float y) const
{
    RenderMgr* rm = RenderMgr::instance();
    const SDL_FRect& content = rm->get_minimap_content_rect();
    // 将小地图像素坐标映射到内容矩形内的相对比例，再转为世界坐标
    float ratio_x = (x - content.x) / content.w;
    float ratio_y = (y - content.y) / content.h;
    float world_x = ratio_x * rm->get_world_width();
    float world_y = ratio_y * rm->get_world_height();
    return { world_x, world_y };
}

// 移动相机到小地图对应位置
void GameScene::move_camera_to_minimap(float x, float y)
{
    Vector2 world = minimap_to_world(x, y);
    Vector2 new_cam_pos;
    new_cam_pos.x = world.x - camera.get_screen_w() / 2.0f / camera.get_scale();
    new_cam_pos.y = world.y - camera.get_screen_h() / 2.0f / camera.get_scale();
    camera.set_position(new_cam_pos);
}

GameScene::GameScene() = default;
GameScene::~GameScene() = default;

void GameScene::on_input(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        float mx = event.button.x;
        float my = event.button.y;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            is_left_btn_down = true;
            float mx = event.button.x, my = event.button.y;
            if (is_point_in_minimap(mx, my))
            {
                // 开始小地图拖拽
                is_left_btn_minimap_dragging = true;
                left_minimap_drag_start = { mx, my };
                camera_start_position = camera.get_position();
                // 同时立即移动一次（点击效果）
                move_camera_to_minimap(mx, my);
                return;
            }
            // 否则正常框选
            selection_box.on_start(mx, my);
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            is_right_btn_down = true;   // 标记右键按下，抬起时处理移动
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            is_middle_btn_down = true;
            middle_drag_start_position = { mx, my };
            camera_start_position = camera.get_position();
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        float mx = event.button.x;
        float my = event.button.y;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            is_left_btn_down = false;

            if (is_left_btn_minimap_dragging)
            {
                is_left_btn_minimap_dragging = false;
                return;   // 直接返回，不执行 selection_box.on_end 和后续清空逻辑
            }

            bool hit_unit = selection_box.on_end(camera);

            // 如果未点中任何单位，且不在小地图上，才清空选择
            if (!hit_unit && !is_point_in_minimap(mx, my))
            {
                if (SelectionMgr::instance()->get_current_mode() == SelectionMgr::SelectMode::Normal)
                    SelectionMgr::instance()->clear();
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            is_right_btn_down = false;

            // 右键点击 → 移动选中单位
            const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
            if (!id_set.empty())
            {
                // 如果点在小地图上，使用小地图坐标；否则使用世界坐标
                Vector2 world_target;
                if (is_point_in_minimap(mx, my))
                {
                    world_target = minimap_to_world(mx, my);
                }
                else
                {
                    world_target = camera.screen_to_world({ mx, my });
                }
                world_target = game_map.find_nearest_passable(world_target);

                // 获取有效对象并计算编队目标
                std::vector<GameObject*> selected_objects;
                selected_objects.reserve(id_set.size());
                for (uint64_t id : id_set)
                {
                    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (obj) selected_objects.push_back(obj);
                }

                if (!selected_objects.empty())
                {
                    auto formation_targets = compute_formation_targets(selected_objects, world_target, &game_map);
                    for (GameObject* obj : selected_objects)
                    {
                        auto* movable = obj->get_component<Movable>();
                        if (movable)
                        {
                            movable->target = formation_targets[obj];
                            movable->flow_target = world_target;
                            move_feedback_system.add_line_for_unit(obj, formation_targets[obj], 1.0f);
                        }
                    }
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            is_middle_btn_down = false;
        }
        break;
    }

    case SDL_EVENT_MOUSE_MOTION:
    {
        float mx = event.motion.x;
        float my = event.motion.y;

        // 左键在小地图上拖拽
        if (is_left_btn_down && is_left_btn_minimap_dragging)
        {
            move_camera_to_minimap(mx, my);
            return;
        }
        // 中键拖拽地图
        if (is_middle_btn_down)
        {
            float dx = mx - middle_drag_start_position.x;
            float dy = my - middle_drag_start_position.y;
            float scale = camera.get_scale();
            Vector2 new_pos = camera_start_position;
            new_pos.x -= dx / scale;
            new_pos.y -= dy / scale;
            camera.set_position(new_pos);
            return; // 拖拽时不处理其他
        }

        // 左键拖动时更新选择框
        selection_box.on_update(mx, my);
        break;
    }

    case SDL_EVENT_KEY_DOWN:
    {
        if (event.key.key == SDLK_S)
        {
            const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
            for (uint64_t id : id_set)
            {
                GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                if (!obj) continue;
                auto* movable = obj->get_component<Movable>();
                if (movable) movable->stop();
            }
        }
        break;
    }
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
    if (is_key_alt_down)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
    if (!is_key_alt_down && !is_key_ctrl_down)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);

    camera_controller.on_update(delta);

    // === 计时代码开始 ===
    auto t0 = std::chrono::high_resolution_clock::now();
    move_system.on_update(delta);
    auto t1 = std::chrono::high_resolution_clock::now();

    move_feedback_system.on_update(delta);
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
    if (++frame_counter % 60 == 0) {  // 每60帧输出一次，避免刷屏
        SDL_Log("FrameTimings: MoveSys=%.3fms, Feedback&ui=%.3fms, WorldUpdate=%.3fms",
            ms1, ms2, ms3);
    }
}

void GameScene::on_enter()
{
    int win_w, win_h;
    SDL_GetRenderLogicalPresentation(renderer, &win_w, &win_h, nullptr);

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

    ResourcesMgr::instance()->init(1);
    ResourcesMgr::instance()->set_local_player_id(0);
    move_system.set_map(&game_map);
    update_ui_layout();

    for (int i = 0;i < 30;i++)
    {
        float x = 250 + i / 10 * 50;
        float y = 100 + (i % 10) * 50;
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