#include "menu_scene.h"
#include "scene_mgr.h"
#include "scene_ui.h"

void MenuScene::on_enter()
{
    layout_buttons();
}

void MenuScene::on_exit()
{
}

void MenuScene::on_update(float delta)
{
    (void)delta;
}

bool MenuScene::point_in(const SDL_FRect& r, float x, float y) const
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void MenuScene::layout_buttons()
{
    buttons_.clear();
    hovered_ = -1;

    const float btn_w = 300.0f;
    const float btn_h = 60.0f;
    const float gap = 24.0f;
    const float cx = LOGICAL_W * 0.5f;
    const float start_y = 360.0f;

    Button start;
    start.rect = { cx - btn_w * 0.5f, start_y, btn_w, btn_h };
    start.label = u8"开始游戏";
    start.on_click = [this]() { do_start(); };
    buttons_.push_back(start);

    Button editor;
    editor.rect = { cx - btn_w * 0.5f, start_y + btn_h + gap, btn_w, btn_h };
    editor.label = u8"地图编辑";
    editor.on_click = [this]() { do_editor(); };
    buttons_.push_back(editor);

    Button quit;
    quit.rect = { cx - btn_w * 0.5f, start_y + (btn_h + gap) * 2, btn_w, btn_h };
    quit.label = u8"退出游戏";
    quit.on_click = [this]() { do_quit(); };
    buttons_.push_back(quit);
}

void MenuScene::do_start()
{
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Selector);
}

void MenuScene::do_editor()
{
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::MapEditor);
}

void MenuScene::do_quit()
{
    SceneMgr::instance()->request_quit();
}

void MenuScene::on_input(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        float x = event.motion.x;
        float y = event.motion.y;
        hovered_ = -1;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (point_in(buttons_[i].rect, x, y)) { hovered_ = (int)i; break; }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        float x = event.button.x;
        float y = event.button.y;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (point_in(buttons_[i].rect, x, y)) { buttons_[i].on_click(); break; }
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
            do_start();
        else if (event.key.key == SDLK_ESCAPE)
            do_quit();
    }
}

void MenuScene::on_render()
{
    // 背景
    SDL_SetRenderDrawColor(renderer, 16, 22, 36, 255);
    SDL_RenderClear(renderer);

    // 顶部装饰条
    ui_fill_rect(renderer, SDL_FRect{ 0, 0, LOGICAL_W, 8.0f }, { 255, 215, 0, 255 });

    // 标题
    ui_draw_text(renderer, u8"MySTR", 88, { 255, 215, 0, 255 }, LOGICAL_W * 0.5f, 190.0f, true);
    ui_draw_text(renderer, u8"2D 即时战略", 36, { 200, 210, 230, 255 }, LOGICAL_W * 0.5f, 290.0f, true);

    // 按钮
    for (size_t i = 0; i < buttons_.size(); ++i) {
        bool hover = ((int)i == hovered_);
        SDL_Color bg = hover ? SDL_Color{ 52, 70, 120, 255 } : SDL_Color{ 36, 48, 84, 255 };
        SDL_Color border = hover ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
        ui_fill_rect(renderer, buttons_[i].rect, bg, border, 2);
        ui_draw_text(renderer, buttons_[i].label, 32, { 255, 255, 255, 255 },
                     buttons_[i].rect.x + buttons_[i].rect.w * 0.5f,
                     buttons_[i].rect.y + buttons_[i].rect.h * 0.5f, true);
    }
}
