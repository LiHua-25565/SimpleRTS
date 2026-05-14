#include "input_system.h"
#include "move_system.h"
#include "selection_mgr.h"
#include "world_entity_mgr.h"
#include "render_mgr.h"
#include <cmath>

void InputSystem::init(Camera* cam, GameMap* map, SelectionBox* selBox, MoveFeedbackSystem* feedback)
{
    this->camera = cam;
    this->map = map;
    this->selectionBox = selBox;
    this->feedbackSystem = feedback;
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
            leftBtnDown = true;
            if (is_point_in_minimap(mx, my))
            {
                leftMinimapDrag = true;
                leftMinimapDragStart = { mx, my };
                cameraStartPos = camera->get_position();
                move_camera_to_minimap(mx, my);
                return;
            }
            selectionBox->on_start(mx, my);
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            rightBtnDown = true;
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            middleBtnDown = true;
            middleDragStart = { mx, my };
            cameraStartPos = camera->get_position();
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        float mx = event.button.x;
        float my = event.button.y;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            leftBtnDown = false;

            if (leftMinimapDrag)
            {
                leftMinimapDrag = false;
                return;
            }

            bool hit_unit = selectionBox->on_end(*camera);

            if (!hit_unit && !is_point_in_minimap(mx, my))
            {
                if (SelectionMgr::instance()->get_current_mode() == SelectionMgr::SelectMode::Normal)
                    SelectionMgr::instance()->clear();
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            rightBtnDown = false;

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
                        if (movable)
                        {
                            movable->target = formation_targets[obj];
                            movable->flow_target = world_target;
                            feedbackSystem->add_line_for_unit(obj, formation_targets[obj], 1.0f);
                        }
                    }
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            middleBtnDown = false;
        }
        break;
    }

    case SDL_EVENT_MOUSE_MOTION:
    {
        float mx = event.motion.x;
        float my = event.motion.y;

        if (leftBtnDown && leftMinimapDrag)
        {
            move_camera_to_minimap(mx, my);
            return;
        }

        if (middleBtnDown)
        {
            float dx = mx - middleDragStart.x;
            float dy = my - middleDragStart.y;
            float scale = camera->get_scale();
            Vector2 new_pos = cameraStartPos;
            new_pos.x -= dx / scale;
            new_pos.y -= dy / scale;
            camera->set_position(new_pos);
            return;
        }

        if (leftBtnDown)
            selectionBox->on_update(mx, my);
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

void InputSystem::on_update(float delta)
{
    const bool* keyState = SDL_GetKeyboardState(nullptr);

    // 相机方向键移动
    camera_input(keyState);
    camera_controller.on_update(delta);   

    bool ctrl = keyState[SDL_SCANCODE_LCTRL] || keyState[SDL_SCANCODE_RCTRL];
    bool alt = keyState[SDL_SCANCODE_LALT] || keyState[SDL_SCANCODE_RALT];

    if (ctrl && !alt)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Add);
    else if (alt && !ctrl)
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Remove);
    else
        SelectionMgr::instance()->set_select_mode(SelectionMgr::SelectMode::Normal);
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