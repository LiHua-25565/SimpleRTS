#include "input_system.h"
#include "move_system.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "render_mgr.h"
#include <cmath>
#include <algorithm>

void InputSystem::init(Camera* cam, GameMap* map, SelectionBox* selBox, MoveFeedbackSystem* feedback, int id)
{
    this->camera = cam;
    this->map = map;
    this->selection_box = selBox;
    this->feedback_system = feedback;
    this->local_player_id = id;
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
            right_btn_down = false;

            // 1. 检查是否点击了己方可提交资源建筑（精确碰撞）
            Vector2 world_click;
            if (is_point_in_minimap(mx, my))
                world_click = minimap_to_world(mx, my);
            else
                world_click = camera->screen_to_world({ mx, my });

            CollisionBox click_area{ world_click, 1.0f, 1.0f };
            std::vector<GameObject*> hit_objects;
            WorldEntityMgr::instance()->query_area(click_area, hit_objects);

            bool issued_submit = false;
            for (auto* obj : hit_objects)
            {
                if (!obj->check_valid()) continue;
                auto* dropoff = obj->get_component<ResourceDropoff>();
                if (!dropoff) continue;
                auto* owner = obj->get_component<Ownership>();
                if (!owner || owner->player_id != local_player_id) continue;

                // 精确碰撞：点击点必须在建筑碰撞盒内
                const auto& cb = obj->get_collision_box();
                if (world_click.x < cb.position.x || world_click.x > cb.position.x + cb.width ||
                    world_click.y < cb.position.y || world_click.y > cb.position.y + cb.height) continue;

                // 为每个携带资源的采集单位设置提交目标
                std::vector<GameObject*> submit_units;
                const auto& id_set = SelectionMgr::instance()->get_selected_object_id_set();
                std::vector<GameObject*> collectors;
                for (uint64_t id : id_set) {
                    GameObject* unit = WorldEntityMgr::instance()->get_object_by_id(id);
                    if (!unit) continue;
                    auto* gatherer = unit->get_component<Gatherer>();
                    if (!gatherer || gatherer->carried_amount <= 0) continue;
                    auto* ownership = unit->get_component<Ownership>();
                    if (!ownership || ownership->player_id != local_player_id) continue;
                    collectors.push_back(unit);
                }

                if (!collectors.empty()) {
                    Vector2 building_center = obj->get_collision_box().get_center_position();
                    // 2. 使用编队目标函数，以建筑中心为命令中心，为单位们分配环绕建筑的精确目标点
                    auto formation_targets = compute_formation_targets(collectors, building_center, map);

                    for (GameObject* unit : collectors) {
                        auto* gatherer = unit->get_component<Gatherer>();
                        gatherer->dropoff_target = obj;   // 记录提交目标建筑

                        auto* movable = unit->get_component<Movable>();
                        if (movable) {
                            movable->target = building_center;       // 个人精确点
                            movable->flow_target = building_center;          // 共享流场目标，所有单位共用同一流场
                            feedback_system->add_line_for_unit(unit, building_center, 0.5f);
                        }
                    }
                    issued_submit = true;  // 标记已处理，不再执行后续移动逻辑
                }
                if (issued_submit) break; // 只响应第一个有效建筑
            }

            // 2. 如果没有发生提交，则执行原有编队移动
            if (!issued_submit)
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
                        }
                    }
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
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
            }
        }
            break;
        case SDLK_A:
            if (is_key_ctrl_down)
                SelectionMgr::instance()->select_all_unit();
            break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            is_key_ctrl_down = true;
            if (!is_key_alt_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
            else
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
            break;
        case SDLK_LALT:
        case SDLK_RALT:
            is_key_alt_down = true;
            if (!is_key_ctrl_down)
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
            else
                SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
            break;
        }
    }
    break;
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