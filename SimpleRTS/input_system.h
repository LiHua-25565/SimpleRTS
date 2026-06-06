#ifndef _INPUT_SYSTEM_H_
#define _INPUT_SYSTEM_H_

#include <SDL3/SDL.h>
#include "vector2.h"
#include "camera.h"
#include "camera_controller.h"
#include "selection_box.h"
#include "game_map.h"
#include "move_feedback_system.h"

class InputSystem
{
public:
    InputSystem() = default;

    // 在 GameScene::on_enter 中调用，设置依赖
    void init(Camera* cam, GameMap* map, SelectionBox* sel_box, MoveFeedbackSystem* feedback,
        int player_id, SDL_Window* window);

    // 处理所有输入事件
    void handle_event(const SDL_Event& event);

    void on_update(float delta);

    void toggle_fullscreen();
    void exit_fullscreen();

private:
    bool ui_captured_mouse = false;
    bool left_btn_down = false;
    bool is_left_minimap_dragging = false;
    bool right_btn_down = false;
    bool middle_btn_down = false;
    bool is_key_ctrl_down = false;
    bool is_key_alt_down = false;

private:
    SDL_Window* window = nullptr;
    bool is_fullscreen = false;

private:
    int local_player_id = 0;
    int local_team_id = 0;

    Camera* camera = nullptr;
    CameraController camera_controller;
    GameMap* map = nullptr;
    SelectionBox* selection_box = nullptr;
    MoveFeedbackSystem* feedback_system = nullptr;
    
    Vector2 left_minimap_drag_start;
    Vector2 camera_start_pos;
    Vector2 middle_drag_start;

    bool handle_placement_event(const SDL_Event& event);  // 返回 true 表示事件已处理
    void handle_mouse_button_down(const SDL_Event& event);
    void handle_mouse_button_up(const SDL_Event& event);
    void handle_mouse_motion(const SDL_Event& event);
    void handle_key_down(const SDL_Event& event);
    void handle_key_up(const SDL_Event& event);

    // 小地图辅助
    bool is_point_in_minimap(float x, float y) const;
    Vector2 minimap_to_world(float x, float y) const;
    void move_camera_to_minimap(float x, float y);

    void camera_input(const bool* keyState);
};

#endif // !_INPUT_SYSTEM_H_
