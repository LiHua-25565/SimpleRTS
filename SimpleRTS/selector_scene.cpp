#include "selector_scene.h"
#include "game_scene.h"
#include "scene_ui.h"

void SelectorScene::on_enter()
{
    config_ = MatchConfig{};   // 默认：蓝方 / 2v2 / 普通
    layout();
}

void SelectorScene::on_exit()
{
}

void SelectorScene::on_update(float delta)
{
    (void)delta;
}

bool SelectorScene::point_in(const SDL_FRect& r, float x, float y) const
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

int SelectorScene::selected_value(int group) const
{
    if (group == 0) return config_.human_team;
    if (group == 1) return config_.total_players;
    if (group == 2) return config_.ai_difficulty;
    return -1;
}

void SelectorScene::apply_selection(int group, int value)
{
    if (group == 0) config_.human_team = value;
    else if (group == 1) config_.total_players = value;
    else if (group == 2) config_.ai_difficulty = value;
}

void SelectorScene::layout()
{
    groups_.clear();
    hovered_group_ = hovered_opt_ = hovered_btn_ = -1;

    const float opt_w = 180.0f;
    const float opt_h = 56.0f;
    const float gap = 22.0f;
    const float cx = LOGICAL_W * 0.5f;
    const float group_h = 128.0f;
    const float first_y = 170.0f;

    // 三个选择组
    {
        OptGroup g;
        g.title = u8"选择阵营";
        g.options.push_back({ u8"蓝方", 0, SDL_FRect{} });
        g.options.push_back({ u8"红方", 1, SDL_FRect{} });
        groups_.push_back(std::move(g));
    }
    {
        OptGroup g;
        g.title = u8"对局模式";
        g.options.push_back({ u8"1v1", 2, SDL_FRect{} });
        g.options.push_back({ u8"2v2", 4, SDL_FRect{} });
        groups_.push_back(std::move(g));
    }
    {
        OptGroup g;
        g.title = u8"AI 难度";
        g.options.push_back({ u8"简单", 0, SDL_FRect{} });
        g.options.push_back({ u8"普通", 1, SDL_FRect{} });
        g.options.push_back({ u8"困难", 2, SDL_FRect{} });
        groups_.push_back(std::move(g));
    }

    // 计算每组选项的屏幕矩形
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        auto& g = groups_[gi];
        g.title_y = first_y + gi * group_h;
        float total_w = opt_w * g.options.size() + gap * (g.options.size() - 1);
        float x0 = cx - total_w * 0.5f;
        float opt_y = g.title_y + 44.0f;
        for (size_t i = 0; i < g.options.size(); ++i) {
            g.options[i].rect = { x0 + i * (opt_w + gap), opt_y, opt_w, opt_h };
        }
    }

    const float btn_w = 220.0f;
    const float btn_h = 60.0f;
    const float btn_y = 570.0f;

    start_btn_.rect = { cx - btn_w - 30.0f, btn_y, btn_w, btn_h };
    start_btn_.label = u8"开始对局";
    start_btn_.on_click = [this]() { do_start(); };

    back_btn_.rect = { cx + 30.0f, btn_y, btn_w, btn_h };
    back_btn_.label = u8"返回";
    back_btn_.on_click = [this]() { do_back(); };
}

void SelectorScene::do_start()
{
    GameScene* game = SceneMgr::instance()->get_game_scene();
    if (game)
        game->set_match_config(config_);
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Game);
}

void SelectorScene::do_back()
{
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
}

void SelectorScene::on_input(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        float x = event.motion.x, y = event.motion.y;
        hovered_group_ = hovered_opt_ = hovered_btn_ = -1;
        for (size_t gi = 0; gi < groups_.size(); ++gi)
            for (size_t i = 0; i < groups_[gi].options.size(); ++i)
                if (point_in(groups_[gi].options[i].rect, x, y)) {
                    hovered_group_ = (int)gi;
                    hovered_opt_ = (int)i;
                }
        if (point_in(start_btn_.rect, x, y)) hovered_btn_ = 0;
        else if (point_in(back_btn_.rect, x, y)) hovered_btn_ = 1;
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        float x = event.button.x, y = event.button.y;
        for (size_t gi = 0; gi < groups_.size(); ++gi)
            for (size_t i = 0; i < groups_[gi].options.size(); ++i)
                if (point_in(groups_[gi].options[i].rect, x, y)) {
                    apply_selection((int)gi, groups_[gi].options[i].value);
                    return;
                }
        if (point_in(start_btn_.rect, x, y)) do_start();
        else if (point_in(back_btn_.rect, x, y)) do_back();
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
            do_start();
        else if (event.key.key == SDLK_ESCAPE)
            do_back();
    }
}

void SelectorScene::on_render()
{
    SDL_SetRenderDrawColor(renderer, 16, 22, 36, 255);
    SDL_RenderClear(renderer);

    ui_fill_rect(renderer, SDL_FRect{ 0, 0, LOGICAL_W, 8.0f }, { 255, 215, 0, 255 });

    ui_draw_text(renderer, u8"阵容选择", 56, { 255, 215, 0, 255 }, LOGICAL_W * 0.5f, 90.0f, true);

    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        auto& g = groups_[gi];
        int sel = selected_value((int)gi);
        float title_x = g.options.empty() ? LOGICAL_W * 0.5f : g.options[0].rect.x;
        ui_draw_text(renderer, g.title, 24, { 200, 210, 230, 255 }, title_x, g.title_y);

        for (size_t i = 0; i < g.options.size(); ++i) {
            auto& o = g.options[i];
            bool is_sel = (o.value == sel);
            bool hover = ((int)gi == hovered_group_ && (int)i == hovered_opt_);
            SDL_Color bg = is_sel ? SDL_Color{ 62, 122, 62, 255 }
                         : (hover ? SDL_Color{ 52, 70, 120, 255 } : SDL_Color{ 36, 48, 84, 255 });
            SDL_Color bd = (is_sel || hover) ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
            ui_fill_rect(renderer, o.rect, bg, bd, is_sel ? 3 : 2);
            ui_draw_text(renderer, o.label, 26, { 255, 255, 255, 255 },
                         o.rect.x + o.rect.w * 0.5f, o.rect.y + o.rect.h * 0.5f, true);
        }
    }

    // 底部按钮
    {
        bool hover = (hovered_btn_ == 0);
        SDL_Color bg = hover ? SDL_Color{ 82, 142, 82, 255 } : SDL_Color{ 62, 122, 62, 255 };
        ui_fill_rect(renderer, start_btn_.rect, bg, { 255, 215, 0, 255 }, 2);
        ui_draw_text(renderer, start_btn_.label, 28, { 255, 255, 255, 255 },
                     start_btn_.rect.x + start_btn_.rect.w * 0.5f,
                     start_btn_.rect.y + start_btn_.rect.h * 0.5f, true);
    }
    {
        bool hover = (hovered_btn_ == 1);
        SDL_Color bg = hover ? SDL_Color{ 52, 70, 120, 255 } : SDL_Color{ 36, 48, 84, 255 };
        ui_fill_rect(renderer, back_btn_.rect, bg, { 90, 110, 160, 255 }, 2);
        ui_draw_text(renderer, back_btn_.label, 28, { 255, 255, 255, 255 },
                     back_btn_.rect.x + back_btn_.rect.w * 0.5f,
                     back_btn_.rect.y + back_btn_.rect.h * 0.5f, true);
    }
}
