#include "ui_label.h"

ui_label::ui_label(TTF_Font* font, const std::string& text, SDL_Color color)
    : font(font), text(text), color(color)
{
}

void ui_label::set_text(const std::string& text)
{
    this->text = text;
    texture_dirty = true;
}

void ui_label::rebuild_texture(SDL_Renderer* renderer)
{
    if (texture)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (surf)
    {
        texture = SDL_CreateTextureFromSurface(renderer, surf);
        tex_w = surf->w;
        tex_h = surf->h;
        SDL_DestroySurface(surf);
    }
}

void ui_label::render(SDL_Renderer* renderer)
{
    if (texture_dirty)
    {
        rebuild_texture(renderer);
        texture_dirty = false;
    }

    if (texture)
    {
        SDL_FRect r = { rect.x, rect.y, (float)tex_w, (float)tex_h };
        SDL_RenderTexture(renderer, texture, nullptr, &r);
    }
}