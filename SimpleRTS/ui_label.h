#ifndef _UI_LABEL_H_
#define _UI_LABEL_H_

#include "ui_component.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <string>

class ui_label : public UIComponent {
public:
    ui_label(TTF_Font* font, const std::string& text, SDL_Color color);
    void set_text(const std::string& text);
    void render(SDL_Renderer* renderer) override;

private:
    void rebuild_texture(SDL_Renderer* renderer);

    TTF_Font* font;
    std::string text;
    SDL_Color color;
    SDL_Texture* texture = nullptr;
    int tex_w = 0, tex_h = 0;
    bool texture_dirty = true;
};

#endif // !_UI_LABEL_H_