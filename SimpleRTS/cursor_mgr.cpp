#include "cursor_mgr.h"

CursorMgr* CursorMgr::instance()
{
	static CursorMgr mgr;
	return &mgr;
}

CursorMgr::CursorMgr() = default;
CursorMgr::~CursorMgr() = default;

void CursorMgr::on_input(const SDL_Event& event)
{
	switch (event.type)
	{
	case SDL_EVENT_MOUSE_MOTION:
		position.x = event.motion.x;
		position.y = event.motion.y;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
		{
			is_mouse_lbtn_down = true;
			left_down_position.x = event.motion.x;
			left_down_position.y = event.motion.y;
		}
		else if (event.button.button == SDL_BUTTON_RIGHT)
			is_mouse_rbtn_down = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event.button.button == SDL_BUTTON_LEFT)
			is_mouse_lbtn_down = false;
		else if (event.button.button == SDL_BUTTON_RIGHT)
			is_mouse_rbtn_down = false;
		break;
	}
}