#ifndef _SELECTOR_SCENE_H_
#define _SELECTOR_SCENE_H_

#include "scene_mgr.h"
#include "match_config.h"
#include <vector>
#include <string>
#include <functional>

class SelectorScene : public Scene {
public:
    SelectorScene() = default;
    ~SelectorScene() = default;

    void on_enter() override;
    void on_input(const SDL_Event& event) override;
    void on_update(float delta) override;
    void on_render() override;
    void on_exit() override;

private:
    struct Opt {
        std::string label;
        int value;
        SDL_FRect rect;
    };
    struct OptGroup {
        std::string title;
        float title_y = 0.0f;
        std::vector<Opt> options;
    };
    struct Button {
        SDL_FRect rect;
        std::string label;
        std::function<void()> on_click;
    };

    MatchConfig config_;

    std::vector<OptGroup> groups_;   // 0=阵营 1=模式 2=难度
    Button start_btn_;
    Button back_btn_;

    int hovered_group_ = -1;   // -1 表示未悬停在选项上
    int hovered_opt_   = -1;
    int hovered_btn_   = -1;   // 0=开始对局 1=返回

    void layout();
    int  selected_value(int group) const;
    void apply_selection(int group, int value);
    void do_start();
    void do_back();
    bool point_in(const SDL_FRect& r, float x, float y) const;
};

#endif // !_SELECTOR_SCENE_H_
