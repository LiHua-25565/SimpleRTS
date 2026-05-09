#ifndef _SELECTION_BOX_H_
#define _SELECTION_BOX_H_

#include "vector2.h"
#include "render_mgr.h"
#include <unordered_set>

class SelectionBox
{
public:
	SelectionBox() = default;
	~SelectionBox() = default;

	void on_start(float x, float y);

	void on_update(float x, float y);

	bool on_end(Camera& camera);

	void on_render();

private:
	bool is_dragging = false;
	Vector2 start_position;
	Vector2 current_position;

	const float MIN_DRAG = 20.0f;	// 任意边长小于该值的矩阵视为单点
};

#endif // !_SELECTION_BOX_H_
