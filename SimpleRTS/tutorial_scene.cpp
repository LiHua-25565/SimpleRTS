#include "tutorial_scene.h"
#include "game_scene.h"
#include "scene_mgr.h"
#include "scene_ui.h"

#include <string>

TutorialScene::TutorialScene()
    : game_(std::make_unique<GameScene>())
{
}

TutorialScene::~TutorialScene() = default;

void TutorialScene::on_enter()
{
    game_->set_renderer(renderer);
    game_->set_font(font);
    game_->start_tutorial();
    game_->set_tutorial_external_ui(true);
    game_->on_enter();
}

void TutorialScene::on_exit()
{
    game_->on_exit();
}

bool TutorialScene::point_in(const SDL_FRect& r, float x, float y) const
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

bool TutorialScene::handle_click(float x, float y)
{
    if (game_->is_tutorial_finished()) {
        if (point_in(continue_btn_, x, y)) {
            game_->skip_tutorial();
            return true;
        }
        if (point_in(menu_btn_, x, y)) {
            SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
            return true;
        }
        return true;
    }

    if (point_in(skip_btn_, x, y)) {
        game_->skip_tutorial();
        return true;
    }
    return false;
}

void TutorialScene::on_input(const SDL_Event& event)
{
    if (game_->is_tutorial_finished()) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            handle_click(event.button.x, event.button.y);
        else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
        return;
    }

    if (game_->is_tutorial_mode()) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (handle_click(event.button.x, event.button.y)) return;
            if (point_in(panel_, event.button.x, event.button.y)) return;
        }
    }

    game_->on_input(event);
}

void TutorialScene::on_update(float delta)
{
    game_->on_update(delta);
}

void TutorialScene::on_render()
{
    game_->on_render();
}

void TutorialScene::on_render_overlay()
{
    render_tutorial();
    game_->on_render_overlay();
}

void TutorialScene::render_tutorial()
{
    if (!game_->is_tutorial_mode()) return;

    if (game_->is_tutorial_finished()) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_FRect full{ 0, 0, LOGICAL_W, LOGICAL_H };
        SDL_RenderFillRect(renderer, &full);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        const float pw = 600.0f, ph = 320.0f;
        SDL_FRect box{ (LOGICAL_W - pw) * 0.5f, (LOGICAL_H - ph) * 0.5f, pw, ph };
        ui_fill_rect(renderer, box, { 24, 30, 48, 255 }, { 255, 215, 0, 255 }, 2);
        ui_draw_text(renderer, u8"教程完成！", 40, { 255, 215, 0, 255 },
            box.x + pw * 0.5f, box.y + 44.0f, true);
        ui_draw_text(renderer, u8"你已经学会：选择、采集、建造、训练、研究、攻击。",
            20, { 220, 230, 248, 255 }, box.x + pw * 0.5f, box.y + 100.0f, true);
        ui_draw_text(renderer, u8"现在去 map2v2 打一场真正的 2v2 吧。",
            18, { 160, 180, 215, 255 }, box.x + pw * 0.5f, box.y + 132.0f, true);

        const float bw = 220.0f, bh = 52.0f;
        continue_btn_ = { box.x + 40.0f, box.y + 220.0f, bw, bh };
        menu_btn_ = { box.x + pw - 40.0f - bw, box.y + 220.0f, bw, bh };
        ui_fill_rect(renderer, continue_btn_, { 36, 48, 84, 255 }, { 90, 110, 160, 255 }, 2);
        ui_draw_text(renderer, u8"继续自由练习", 22, { 255, 255, 255, 255 },
            continue_btn_.x + bw * 0.5f, continue_btn_.y + bh * 0.5f, true);
        ui_fill_rect(renderer, menu_btn_, { 36, 48, 84, 255 }, { 255, 215, 0, 255 }, 2);
        ui_draw_text(renderer, u8"返回主菜单", 22, { 255, 255, 255, 255 },
            menu_btn_.x + bw * 0.5f, menu_btn_.y + bh * 0.5f, true);
        return;
    }

    panel_ = { 18.0f, 18.0f, 510.0f, 202.0f };
    ui_fill_rect(renderer, panel_, { 24, 30, 48, 245 }, { 255, 215, 0, 255 }, 2);

    const int step = game_->get_tutorial_step();
    const std::string step_text = u8"新手教程  " + std::to_string(step + 1) + " / 6";
    ui_draw_text(renderer, step_text, 16, { 150, 170, 205, 255 },
        panel_.x + 18.0f, panel_.y + 14.0f);
    ui_draw_text(renderer, game_->get_tutorial_title(), 26, { 255, 215, 0, 255 },
        panel_.x + 18.0f, panel_.y + 42.0f);
    ui_draw_text(renderer, game_->get_tutorial_detail1(), 17, { 232, 240, 252, 255 },
        panel_.x + 18.0f, panel_.y + 88.0f);
    ui_draw_text(renderer, game_->get_tutorial_detail2(), 15, { 150, 170, 200, 255 },
        panel_.x + 18.0f, panel_.y + 116.0f);

    for (int i = 0; i < 6; ++i) {
        SDL_FRect dot{ panel_.x + 18.0f + i * 22.0f, panel_.y + 154.0f, 12.0f, 8.0f };
        SDL_Color c = (i < step) ? SDL_Color{ 80, 210, 130, 255 }
            : (i == step ? SDL_Color{ 255, 215, 0, 255 }
                         : SDL_Color{ 70, 85, 115, 255 });
        ui_fill_rect(renderer, dot, c);
    }

    skip_btn_ = { panel_.x + panel_.w - 120.0f, panel_.y + panel_.h - 40.0f, 102.0f, 26.0f };
    ui_fill_rect(renderer, skip_btn_, { 36, 48, 84, 255 }, { 90, 110, 160, 255 }, 1);
    ui_draw_text(renderer, u8"跳过教程", 15, { 210, 220, 240, 255 },
        skip_btn_.x + skip_btn_.w * 0.5f, skip_btn_.y + skip_btn_.h * 0.5f, true);
    ui_draw_text(renderer, u8"完成目标后自动进入下一步", 13, { 120, 136, 165, 255 },
        panel_.x + 92.0f, panel_.y + 160.0f);
}
