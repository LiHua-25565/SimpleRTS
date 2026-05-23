#ifndef _UI_PANEL_H_
#define _UI_PANEL_H_

#include "ui_component.h"

class ui_panel : public UIComponent {
public:
    ui_panel(SDL_Color bg_color);
    void render(SDL_Renderer* renderer) override;

private:
    SDL_Color bg_color_;
};

#endif // !_UI_PANEL_H_
