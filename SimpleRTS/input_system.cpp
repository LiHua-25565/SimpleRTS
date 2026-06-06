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

// ========== 放置模式事件处理 ==========
bool InputSystem::handle_placement_event(const SDL_Event& event) {
    if (!UIMgr::instance()->is_placement_mode()) return false;

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        if (middle_btn_down) break;
        Vector2 world = camera->screen_to_world({ event.motion.x, event.motion.y });
        int cell_size = map->get_cell_size();
        int gx = (int)(world.x / cell_size);
        int gy = (int)(world.y / cell_size);
        auto* item = get_build_item(UIMgr::instance()->get_placement_building());
        if (item) {
            bool blocked = false;
            for (int y = 0; y < item->size_cells && !blocked; ++y) {
                for (int x = 0; x < item->size_cells && !blocked; ++x) {
                    int cx = gx + x, cy = gy + y;
                    // 边界检查
                    if (cx < 0 || cx >= map->get_width() || cy < 0 || cy >= map->get_height()) {
                        blocked = true;
                        break;
                    }
                    // 水域检查
                    if (map->get_grid()[cy][cx] == TerrainType::Water) {
                        blocked = true;
                        break;
                    }

                    // 精确碰撞检测：用当前格子的矩形去查询重叠实体
                    CollisionBox cell_box{
                        { (float)(cx * cell_size), (float)(cy * cell_size) },
                        (float)cell_size, (float)cell_size
                    };
                    std::vector<GameObject*> objs;
                    WorldEntityMgr::instance()->query_area(cell_box, objs);
                    for (auto* o : objs) {
                        if (!o->check_valid()) continue;
                        if (o->get_component<Projectile>()) continue; // 忽略投射物
                        if (o->get_collision_box().overlaps_strict(cell_box)) {
                            blocked = true;
                            break;
                        }
                    }
                }
            }
            UIMgr::instance()->update_placement_preview(gx, gy, blocked);
        }
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            Vector2 world = camera->screen_to_world({ event.button.x, event.button.y });
            int cell_size = map->get_cell_size();
            int gx = (int)(world.x / cell_size);
            int gy = (int)(world.y / cell_size);
            auto* item = get_build_item(UIMgr::instance()->get_placement_building());
            if (item) {
                bool blocked = false;
                for (int y = 0; y < item->size_cells && !blocked; ++y)
                    for (int x = 0; x < item->size_cells && !blocked; ++x) {
                        int cx = gx + x, cy = gy + y;
                        if (cx < 0 || cx >= map->get_width() || cy < 0 || cy >= map->get_height()) {
                            blocked = true; break;
                        }
                        if (map->get_grid()[cy][cx] == TerrainType::Water) {
                            blocked = true; break;
                        }
                        CollisionBox cell_box{
                            { (float)(cx * cell_size), (float)(cy * cell_size) },
                            (float)cell_size, (float)cell_size
                        };
                        std::vector<GameObject*> objs;
                        WorldEntityMgr::instance()->query_area(cell_box, objs);
                        for (auto* o : objs) {
                            if (!o->check_valid()) continue;
                            if (o->get_component<Projectile>()) continue;
                            if (o->get_collision_box().overlaps_strict(cell_box)) {
                                blocked = true; break;
                            }
                        }
                    }
                if (!blocked) {
                    UIMgr::instance()->confirm_placement(gx, gy);
                }
            }
            return true;
        }
        else if (event.button.button == SDL_BUTTON_RIGHT) {
            UIMgr::instance()->cancel_placement();
            return true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        break;

    default:
        break;
    }
    return false;
}

// ========== 鼠标按下 ==========
void InputSystem::handle_mouse_button_down(const SDL_Event& event) {
    float mx = event.button.x;
    float my = event.button.y;

    if (event.button.button == SDL_BUTTON_LEFT) {
        if (UIMgr::instance()->handle_mouse_down(mx, my)) {
            ui_captured_mouse = true;
            return;
        }
        left_btn_down = true;
        if (is_point_in_minimap(mx, my)) {
            is_left_minimap_dragging = true;
            left_minimap_drag_start = { mx, my };
            camera_start_pos = camera->get_position();
            move_camera_to_minimap(mx, my);
            return;
        }
        selection_box->on_start(mx, my);
    }
    else if (event.button.button == SDL_BUTTON_RIGHT) {
        right_btn_down = true;
    }
    else if (event.button.button == SDL_BUTTON_MIDDLE) {
        middle_btn_down = true;
        middle_drag_start = { mx, my };
        camera_start_pos = camera->get_position();
    }
}

// ========== 鼠标松开 ==========
void InputSystem::handle_mouse_button_up(const SDL_Event& event) {
    float mx = event.button.x;
    float my = event.button.y;

    if (event.button.button == SDL_BUTTON_LEFT) {
        if (ui_captured_mouse) {
            UIMgr::instance()->handle_mouse_up(mx, my);
            ui_captured_mouse = false;
            left_btn_down = false;
            if (is_left_minimap_dragging) is_left_minimap_dragging = false;
            return;
        }

        if (!left_btn_down) return;
        left_btn_down = false;

        if (is_left_minimap_dragging) {
            is_left_minimap_dragging = false;
            return;
        }

        bool hit_unit = selection_box->on_end(*camera);
        if (!hit_unit && !is_point_in_minimap(mx, my)) {
            if (SelectionMgr::instance()->get_current_mode() == SelectionMgr::SelectMode::Normal)
                SelectionMgr::instance()->clear();
        }
    }
    else if (event.button.button == SDL_BUTTON_RIGHT) {
        if (!right_btn_down) return;
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

        for (auto* obj : hit_objects) {
            if (!obj->check_valid()) continue;
            const auto& cb = obj->get_collision_box();
            if (world_click.x < cb.position.x || world_click.x > cb.position.x + cb.width ||
                world_click.y < cb.position.y || world_click.y > cb.position.y + cb.height)
                continue;

            // 1. 提交资源
            auto* dropoff = obj->get_component<ResourceDropoff>();
            auto* owner = obj->get_component<Ownership>();
            if (dropoff && owner && owner->player_id == local_player_id) {
                const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                for (uint64_t id : id_set) {
                    GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (!unit) continue;
                    auto* gatherer = unit->get_component<Gatherer>();
                    if (!gatherer || gatherer->carried_amount <= 0) continue;
                    auto* unit_owner = unit->get_component<Ownership>();
                    if (!unit_owner || unit_owner->player_id != local_player_id) continue;
                    gatherer->dropoff_target_id = obj->get_id();
                    auto* movable = unit->get_component<Movable>();
                    if (movable) {
                        movable->target = compute_outer_target(
                            unit->get_collision_box().get_center_position(),
                            obj->get_collision_box().get_center_position(),
                            unit->get_collision_box(), cb, 10.0f);
                        movable->flow_target = movable->target;
                        feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                    }
                    issued_command = true;
                    obj->start_flash();
                }
                if (issued_command) break;
            }

            // 2. 资源采集
            auto* harvestable = obj->get_component<Harvestable>();
            if (harvestable && !issued_command) {
                const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                for (uint64_t id : id_set) {
                    GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (!unit) continue;
                    auto* gatherer = unit->get_component<Gatherer>();
                    if (!gatherer) continue;
                    auto* unit_owner = unit->get_component<Ownership>();
                    if (!unit_owner || unit_owner->player_id != local_player_id) continue;
                    gatherer->target_resource_id = obj->get_id();
                    auto* movable = unit->get_component<Movable>();
                    if (movable) {
                        movable->target = compute_outer_target(
                            unit->get_collision_box().get_center_position(),
                            obj->get_collision_box().get_center_position(),
                            unit->get_collision_box(), cb, 10.0f);
                        movable->flow_target = movable->target;
                        feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                    }
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

            // 3. 攻击
            auto* health_comp = obj->get_component<Health>();
            if (health_comp && !issued_command) {
                if (owner && owner->team_id == local_team_id) continue;
                const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                for (uint64_t id : id_set) {
                    GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (!unit) continue;
                    auto* attack = unit->get_component<Attack>();
                    if (!attack) continue;
                    auto* u_own = unit->get_component<Ownership>();
                    if (!u_own || u_own->player_id != local_player_id) continue;
                    attack->target_id = obj->get_id();
                    attack->auto_attack = true;
                    auto* movable = unit->get_component<Movable>();
                    if (movable) {
                        Vector2 unit_center = unit->get_collision_box().get_center_position();
                        Vector2 target_center = obj->get_collision_box().get_center_position();
                        if (attack->is_ranged) {
                            float dist = rect_closest_distance(unit_center, obj->get_collision_box());
                            if (dist > attack->range) {
                                movable->target = compute_ranged_outer_target(
                                    unit_center, target_center, unit->get_collision_box(),
                                    obj->get_collision_box(), attack->range, 5.0f);
                                movable->flow_target = movable->target;
                                feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                            }
                            else {
                                movable->stop();
                            }
                        }
                        else {
                            movable->target = compute_outer_target(
                                unit_center, target_center, unit->get_collision_box(), cb, 5.0f);
                            movable->flow_target = movable->target;
                            feedback_system->add_line_for_unit(unit, movable->target, 0.5f);
                        }
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

        // 4. 编队移动
        if (!issued_command) {
            const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
            if (!id_set.empty()) {
                Vector2 world_target;
                if (is_point_in_minimap(mx, my))
                    world_target = minimap_to_world(mx, my);
                else
                    world_target = camera->screen_to_world({ mx, my });
                world_target = map->find_nearest_passable(world_target);

                std::vector<GameObject*> selected;
                selected.reserve(id_set.size());
                for (uint64_t id : id_set) {
                    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (obj) selected.push_back(obj);
                }
                if (!selected.empty()) {
                    auto targets = compute_formation_targets(selected, world_target, map);
                    for (GameObject* obj : selected) {
                        auto* movable = obj->get_component<Movable>();
                        auto* own = obj->get_component<Ownership>();
                        if (!movable || !own || own->player_id != local_player_id) continue;
                        movable->target = targets[obj];
                        movable->flow_target = world_target;
                        feedback_system->add_line_for_unit(obj, targets[obj], 0.5f);
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
    else if (event.button.button == SDL_BUTTON_MIDDLE) {
        if (!middle_btn_down) return;
        middle_btn_down = false;
    }
}

// ========== 鼠标移动 ==========
void InputSystem::handle_mouse_motion(const SDL_Event& event) {
    float mx = event.motion.x;
    float my = event.motion.y;

    if (left_btn_down && is_left_minimap_dragging) {
        move_camera_to_minimap(mx, my);
        return;
    }

    if (middle_btn_down) {
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
}

// ========== 键盘按下 ==========
void InputSystem::handle_key_down(const SDL_Event& event) {
    switch (event.key.key) {
    case SDLK_F11: toggle_fullscreen(); break;
    case SDLK_ESCAPE: exit_fullscreen(); break;
    case SDLK_RETURN: if (is_key_alt_down) toggle_fullscreen(); break;
    case SDLK_S: {
        const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
        for (uint64_t id : id_set) {
            GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
            if (!obj) continue;
            auto* movable = obj->get_component<Movable>();
            auto* own = obj->get_component<Ownership>();
            if (!movable || !own || own->player_id != local_player_id) continue;
            movable->stop();
            auto* attack = obj->get_component<Attack>();
            if (attack) { attack->target_id = 0; attack->auto_attack = false; }
        }
        break;
    }
    case SDLK_Q: {
        const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
        for (uint64_t id : id_set) {
            GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(id);
            if (!obj || !obj->check_valid()) continue;
            auto* own = obj->get_component<Ownership>();
            if (!own || own->player_id != local_player_id) continue;
            auto* anim = obj->get_component<ImpactAnimation>();
            if (!anim || anim->is_attacking) continue;
            anim->is_attacking = true;
        }
        break;
    }
    case SDLK_A: if (is_key_ctrl_down) SelectionMgr::instance()->select_all_unit(); break;
    case SDLK_LCTRL: case SDLK_RCTRL:
        is_key_ctrl_down = true;
        SelectionMgr::instance()->set_select_mode(is_key_alt_down ? SelectionMgr::SelectMode::Remove : SelectionMgr::SelectMode::Add);
        break;
    case SDLK_LALT: case SDLK_RALT:
        is_key_alt_down = true;
        SelectionMgr::instance()->set_select_mode(is_key_ctrl_down ? SelectionMgr::SelectMode::Add : SelectionMgr::SelectMode::Remove);
        break;
    }
}

// ========== 键盘松开 ==========
void InputSystem::handle_key_up(const SDL_Event& event) {
    switch (event.key.key) {
    case SDLK_LCTRL: case SDLK_RCTRL:
        is_key_ctrl_down = false;
        if (!is_key_alt_down) SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);
        break;
    case SDLK_LALT: case SDLK_RALT:
        is_key_alt_down = false;
        if (!is_key_ctrl_down) SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);
        break;
    }
}

// ========== 主事件入口（简化版） ==========
void InputSystem::handle_event(const SDL_Event& event) {
    if (handle_placement_event(event)) return;

    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: handle_mouse_button_down(event); break;
    case SDL_EVENT_MOUSE_BUTTON_UP:   handle_mouse_button_up(event);   break;
    case SDL_EVENT_MOUSE_MOTION:      handle_mouse_motion(event);      break;
    case SDL_EVENT_KEY_DOWN:          handle_key_down(event);          break;
    case SDL_EVENT_KEY_UP:            handle_key_up(event);            break;
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