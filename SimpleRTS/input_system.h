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
    void init(Camera* cam, GameMap* map, SelectionBox* selBox, MoveFeedbackSystem* feedback);

    // 处理所有输入事件
    void handle_event(const SDL_Event& event);

    void on_update(float delta);

private:
    Camera* camera = nullptr;
    CameraController camera_controller;
    GameMap* map = nullptr;
    SelectionBox* selectionBox = nullptr;
    MoveFeedbackSystem* feedbackSystem = nullptr;

    bool leftBtnDown = false;
    bool leftMinimapDrag = false;
    Vector2 leftMinimapDragStart;
    Vector2 cameraStartPos;

    bool rightBtnDown = false;

    bool middleBtnDown = false;
    Vector2 middleDragStart;

    // 小地图辅助
    bool is_point_in_minimap(float x, float y) const;
    Vector2 minimap_to_world(float x, float y) const;
    void move_camera_to_minimap(float x, float y);

    void camera_input(const bool* keyState);
};

#endif // !_INPUT_SYSTEM_H_
