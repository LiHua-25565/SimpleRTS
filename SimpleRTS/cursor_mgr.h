#ifndef _CURSOR_MGR_H_
#define _CURSOR_MGR_H_

#include "vector2.h"
#include "SDL3/SDL.h"

class CursorMgr
{
public:
	static CursorMgr* instance();

	void on_input(const SDL_Event& event);

	Vector2 get_position() const { return position; }
	bool is_left_down() const { return is_mouse_lbtn_down; }
	bool is_right_down() const { return is_mouse_rbtn_down; }

	// 框选需要：按下时的起点
	Vector2 get_left_down_position() const { return left_down_position; }

private:
	CursorMgr();
	~CursorMgr();

private:
	Vector2 position;
	Vector2 left_down_position;

	bool is_mouse_lbtn_down = false;
	bool is_mouse_rbtn_down = false;
	
};

#endif // !_CURSOR_MGR_H_
