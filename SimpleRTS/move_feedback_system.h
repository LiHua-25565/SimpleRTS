#ifndef _MOVE_FEEDBACK_SYSTEM_H_
#define _MOVE_FEEDBACK_SYSTEM_H_

#include "vector2.h"
#include "game_object.h"
#include <vector>


class Camera;

class MoveFeedbackSystem
{
public:
    // 添加一条从 unit 到 end 的线条，持续 duration 秒 (默认1秒)
    void add_line_for_unit(GameObject* unit, const Vector2& end, float duration = 0.5f);

    // 每帧更新，移除过期线条
    void on_update(float delta);

    // 绘制所有有效线条
    void on_render();

private:
    struct FeedbackLine
    {
        Vector2 end;                    // 终点（世界坐标）
        float time_left;                // 剩余时间（秒）
        GameObject* owner = nullptr;    // 关联的单位（可为空）
    };

private:
    std::vector<FeedbackLine> line_list;
};

#endif // !_MOVE_FEEDBACK_SYSTEM_H_
