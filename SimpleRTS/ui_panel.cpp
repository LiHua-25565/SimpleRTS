#include "ui_panel.h"

ui_panel::ui_panel(SDL_Color bg_color)
    : bg_color_(bg_color)
{
}

void ui_panel::render(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, bg_color_.r, bg_color_.g, bg_color_.b, bg_color_.a);
    SDL_FRect r = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderFillRect(renderer, &r);
    UIComponent::render(renderer);
}