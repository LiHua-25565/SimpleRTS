#ifndef _MENU_SCENE_H_
#define _MENU_SCENE_H_

#include "scene.h"
#include <vector>
#include <string>
#include <functional>

class MenuScene : public Scene
{
public:
    MenuScene() = default;
    ~MenuScene() = default;

    void on_input(const SDL_Event& event) override;
    void on_update(float delta) override;
    void on_render() override;
    void on_enter() override;
    void on_exit() override;

private:
    struct Button {
        SDL_FRect rect;
        std::string label;
        std::function<void()> on_click;
    };

    std::vector<Button> buttons_;
    int hovered_ = -1;

    void layout_buttons();
    void do_start();
    void do_editor();
    void do_quit();
    bool point_in(const SDL_FRect& r, float x, float y) const;
};

#endif // !_MENU_SCENE_H_
