#include "input_system.h"
#include "move_system.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "world_entity_mgr.h"
#include "render_mgr.h"
#include "UI_mgr.h"
#include "util.h"
#include <cmath>
#include <algorithm>

void InputSystem::init(Camera* cam, GameMap* map, SelectionBox* sel_box,
    MoveFeedbackSystem* feedback, int player_id, SDL_Window* win)
{
    camera = cam;
    this->map = map;
    selection_box = sel_box;
    feedback_system = feedback;
    local_player_id = player_id;
    window = win; 
    local_team_id = ResourcesMgr::instance()->get_team_id(local_player_id);
    camera_controller.set_camera(camera);
}

void InputSystem::handle_event(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        float mx = event.button.x;
        float my = event.button.y;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (UIMgr::instance()->handle_mouse_down(mx, my))
            {
                ui_captured_mouse = true;
                return;
            }

            left_btn_down = true;
            if (is_point_in_minimap(mx, my))
            {
                is_left_minimap_dragging = true;
                left_minimap_drag_start = { mx, my };
                camera_start_pos = camera->get_position();
                move_camera_to_minimap(mx, my);
                return;
            }
            selection_box->on_start(mx, my);
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            right_btn_down = true;
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            middle_btn_down = true;
            middle_drag_start = { mx, my };
            camera_start_pos = camera->get_position();
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        float mx = event.button.x;
        float my = event.button.y;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (ui_captured_mouse) {
                UIMgr::instance()->handle_mouse_up(mx, my);
                ui_captured_mouse = false;
                // 需要手动重置左键状态，避免影响后续逻辑
                left_btn_down = false;
                if (is_left_minimap_dragging) is_left_minimap_dragging = false;
                return;
            }

            if (left_btn_down == false)
                return;

            left_btn_down = false;

            if (is_left_minimap_dragging)
            {
                is_left_minimap_dragging = false;
                return;
            }

            bool hit_unit = selection_box->on_end(*camera);

            if (!hit_unit && !is_point_in_minimap(mx, my))
            {
                if (SelectionMgr::instance()->get_current_mode() == SelectionMgr::SelectMode::Normal)
                    SelectionMgr::instance()->clear();
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (right_btn_down == false)
                return;

            right_btn_down = false;

            Vector2 world_click;
            if (is_point_in_minimap(mx, my))
                world_click = minimap_to_world(mx, my);
            else
                world_click = camera->screen_to_world({ mx, my });

            CollisionBox click_area{ world_click, 1.0f, 1.0f };
            std::vector<GameObject*> hit_objects;
            WorldEntityMgr::instance()->query_area(click_area, hit_objects);

            bool issued_command = false;
            const int local_team_id = ResourcesMgr::instance()->get_team_id(local_player_id);

            // 一次遍历，按优先级：提交建筑 > 资源采集 > 攻击目标
            for (auto* obj : hit_objects)
            {
                if (!obj->check_valid()) continue;
                const auto& cb = obj->get_collision_box();

                // 精确碰撞
                if (world_click.x < cb.position.x || world_click.x > cb.position.x + cb.width ||
                    world_click.y < cb.position.y || world_click.y > cb.position.y + cb.height)
                    continue;

                // ---- 1. 己方可提交建筑 ----
                auto* dropoff = obj->get_component<ResourceDropoff>();
                auto* owner = obj->get_component<Ownership>();
                if (dropoff && owner && owner->player_id == local_player_id)
                {
                    const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                    for (uint64_t id : id_set)
                    {
                        GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                        if (!unit) continue;
                        auto* gatherer = unit->get_component<Gatherer>();
                        if (!gatherer || gatherer->carried_amount <= 0) continue;
                        auto* unit_owner = unit->get_component<Ownership>();
                        if (!unit_owner || unit_owner->player_id != local_player_id) continue;

                        gatherer->dropoff_target_id = obj->get_id();

                        auto* movable = unit->get_component<Movable>();
                        if (movable)
                        {
                            movable->target = compute_outer_target(
                                unit->get_collision_box().get_center_position(),
                                obj->get_collision_box().get_center_position(),
                                unit->get_collision_box(),
                                cb,
                                10.0f
                            );
                            movable->flow_target = movable->target;
                            feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                        }
                        issued_command = true;
                        obj->start_flash();
                    }
                    if (issued_command) break;
                }

                // ---- 2. 资源采集 ----
                auto* harvestable = obj->get_component<Harvestable>();
                if (harvestable && !issued_command)
                {
                    const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                    for (uint64_t id : id_set)
                    {
                        GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                        if (!unit) continue;
                        auto* gatherer = unit->get_component<Gatherer>();
                        if (!gatherer) continue;
                        auto* unit_owner = unit->get_component<Ownership>();
                        if (!unit_owner || unit_owner->player_id != local_player_id) continue;

                        gatherer->target_resource_id = obj->get_id();

                        auto* movable = unit->get_component<Movable>();
                        if (movable)
                        {
                            movable->target = compute_outer_target(
                                unit->get_collision_box().get_center_position(),
                                obj->get_collision_box().get_center_position(),
                                unit->get_collision_box(),
                                cb,
                                10.0f
                            );
                            movable->flow_target = movable->target;
                            feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                        }

                        // 清除攻击状态
                        auto* attack = unit->get_component<Attack>();
                        if (attack) {
                            attack->target_id = 0;
                            attack->auto_attack = false;
                        }
                    }

                    issued_command = true;
                    obj->start_flash();

                    if (issued_command) break;
                }

                // ---- 3. 攻击目标（非己方且有血量） ----
                auto* health_comp = obj->get_component<Health>();
                if (health_comp && !issued_command)
                {
                    if (owner && owner->team_id == local_team_id)
                        continue;

                    const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                    for (uint64_t id : id_set)
                    {
                        GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                        if (!unit) continue;
                        auto* attack = unit->get_component<Attack>();
                        if (!attack) continue;
                        auto* u_own = unit->get_component<Ownership>();
                        if (!u_own || u_own->player_id != local_player_id) continue;

                        attack->target_id = obj->get_id();
                        attack->auto_attack = true;

                        auto* movable = unit->get_component<Movable>();
                        if (movable)
                        {
                            Vector2 unit_center = unit->get_collision_box().get_center_position();
                            Vector2 target_center = obj->get_collision_box().get_center_position();

                            if (attack->is_ranged)
                            {
                                float dist_to_target = rect_closest_distance(unit_center, obj->get_collision_box());
                                if (dist_to_target > attack->range)
                                {
                                    movable->target = compute_ranged_outer_target(
                                        unit_center, target_center, unit->get_collision_box(), obj->get_collision_box(), attack->range, 5.0f);
                                    movable->flow_target = movable->target;
                                    feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                                }
                                else
                                {
                                    movable->stop();
                                }
                            }
                            else
                            {
                                movable->target = compute_outer_target(
                                    unit_center,
                                    target_center,
                                    unit->get_collision_box(),
                                    cb,
                                    5.0f
                                );
                                movable->flow_target = movable->target;
                                feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                            }

                            // 清除采集状态
                            auto* gatherer = unit->get_component<Gatherer>();
                            if (gatherer) {
                                gatherer->target_resource_id = 0;
                                gatherer->dropoff_target_id = 0;
                            }
                        }

                        issued_command = true;
                        obj->start_flash();
                    }
                    if (issued_command) break;
                }
            }

            // 4. 编队移动（未触发任何命令）
            if (!issued_command)
            {
                const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                if (!id_set.empty())
                {
                    Vector2 world_target;
                    if (is_point_in_minimap(mx, my))
                        world_target = minimap_to_world(mx, my);
                    else
                        world_target = camera->screen_to_world({ mx, my });
                    world_target = map->find_nearest_passable(world_target);

                    std::vector<GameObject*> selected_objects;
                    selected_objects.reserve(id_set.size());
                    for (uint64_t id : id_set)
                    {
                        GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                        if (obj) selected_objects.push_back(obj);
                    }

                    if (!selected_objects.empty())
                    {
                        auto formation_targets = compute_formation_targets(selected_objects, world_target, map);
                        for (GameObject* obj : selected_objects)
                        {
                            auto* movable = obj->get_component<Movable>();
                            auto* ownership = obj->get_component<Ownership>();
                            if (!movable || !ownership || ownership->player_id != local_player_id) continue;
                            movable->target = formation_targets[obj];
                            movable->flow_target = world_target;
                            feedback_system->add_line_for_unit(obj, formation_targets[obj], 0.5f);

                            // 清除采集/攻击状态
                            auto* gatherer = obj->get_component<Gatherer>();
                            if (gatherer) {
                                gatherer->target_resource_id = 0;
                                gatherer->dropoff_target_id = 0;
                            }
                            auto* attack = obj->get_component<Attack>();
                            if (attack) {
                                attack->target_id = 0;
                                attack->auto_attack = false;
                            }
                        }
                    }
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            if (middle_btn_down == false)
                return;

            middle_btn_down = false;
        }
        break;
    }

    case SDL_EVENT_MOUSE_MOTION:
    {
        float mx = event.motion.x;
        float my = event.motion.y;

        if (left_btn_down && is_left_minimap_dragging)
        {
            move_camera_to_minimap(mx, my);
            return;
        }

        if (middle_btn_down)
        {
            float dx = mx - middle_drag_start.x;
            float dy = my - middle_drag_start.y;
            float scale = camera->get_scale();
            Vector2 new_pos = camera_start_pos;
            new_pos.x -= dx / scale;
            new_pos.y -= dy / scale;
            camera->set_position(new_pos);
            return;
        }

        if (left_btn_down)
            selection_box->on_update(mx, my);
        break;
    }

    case SDL_EVENT_KEY_DOWN:
    {
        switch (event.key.key)
        {
            // ---------- 全屏切换 ----------
        case SDLK_F11:
        {
            toggle_fullscreen();
            break;
        }

        case SDLK_ESCAPE:
        {
            exit_fullscreen();
            break;
        }

        case SDLK_RETURN:   // Enter
            // 利用维护的 is_key_alt_down 状态来判断组合键
        {
            if (is_key_alt_down)
                toggle_fullscreen();
            break;
        }

            // ---------- 游戏控制 ----------
        case SDLK_S:
        {
            const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
            for (uint64_t id : id_set)
            {
                GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                if (!obj) continue;
                auto* movable = obj->get_component<Movable>();
                auto* ownership = obj->get_component<Ownership>();
                if (!movable || !ownership || ownership->player_id != local_player_id) continue;
                movable->stop();

                auto* attack = obj->get_component<Attack>();
                if (attack) {
                    attack->target_id = 0;
                    attack->auto_attack = false;
                }
            }
            break;
        }
        case SDLK_Q:
        {
            const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
            for (uint64_t id : id_set)
            {
                GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                if (!obj || !obj->check_valid()) continue;

                auto* ownership = obj->get_component<Ownership>();
                if (!ownership || ownership->player_id != local_player_id) continue;

                auto* anim = obj->get_component<ImpactAnimation>();
                if (!anim || anim->is_attacking) continue;

                anim->is_attacking = true;
            }
            break;
        }
        case SDLK_A:
        {
            if (is_key_ctrl_down)
                SelectionMgr::instance()->select_all_unit();
            break;
        }

        case SDLK_LCTRL:
        case SDLK_RCTRL:
        {
            is_key_ctrl_down = true;
            if (!is_key_alt_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
            else
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
            break;
        }

        case SDLK_LALT:
        case SDLK_RALT:
        {
            is_key_alt_down = true;
            if (!is_key_ctrl_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
            else
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
        }
        break;
        }
        break;
    }

    case SDL_EVENT_KEY_UP:
    {
        switch (event.key.key)
        {
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            is_key_ctrl_down = false;
            if (!is_key_alt_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);
            break;

        case SDLK_LALT:
        case SDLK_RALT:
            is_key_alt_down = false;
            if (!is_key_ctrl_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);
            break;
        }
        break;
    }
    }
}

void InputSystem::on_update(float delta)
{
    const bool* keyState = SDL_GetKeyboardState(nullptr);

    // 相机方向键移动
    camera_input(keyState);
    camera_controller.on_update(delta);   
}

void InputSystem::camera_input(const bool* keyState)
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

bool InputSystem::is_point_in_minimap(float x, float y) const
{
    const SDL_FRect& content = RenderMgr::instance()->get_minimap_content_rect();
    return (x >= content.x && x <= content.x + content.w &&
        y >= content.y && y <= content.y + content.h);
}

Vector2 InputSystem::minimap_to_world(float x, float y) const
{
    const SDL_FRect& content = RenderMgr::instance()->get_minimap_content_rect();
    float ratio_x = (x - content.x) / content.w;
    float ratio_y = (y - content.y) / content.h;
    return { ratio_x * RenderMgr::instance()->get_world_width(),
             ratio_y * RenderMgr::instance()->get_world_height() };
}

void InputSystem::move_camera_to_minimap(float x, float y)
{
    Vector2 world = minimap_to_world(x, y);
    Vector2 new_cam_pos;
    new_cam_pos.x = world.x - camera->get_screen_w() / 2.0f / camera->get_scale();
    new_cam_pos.y = world.y - camera->get_screen_h() / 2.0f / camera->get_scale();
    camera->set_position(new_cam_pos);
}

void InputSystem::toggle_fullscreen()
{
    is_fullscreen = !is_fullscreen;
    if (window)
        SDL_SetWindowFullscreen(window, is_fullscreen);
}

void InputSystem::exit_fullscreen()
{
    if (is_fullscreen)
    {
        is_fullscreen = false;
        SDL_SetWindowFullscreen(window, false);
    }
}