#ifndef _TUTORIAL_SCENE_H_
#define _TUTORIAL_SCENE_H_

#include "scene.h"

#include <memory>

class GameScene;

// 独立的新手教程关卡：拥有自己的 GameScene 和教程 UI，
// 不把教程提示塞进普通对局界面。
class TutorialScene : public Scene
{
public:
    TutorialScene();
    ~TutorialScene();

    void on_input(const SDL_Event& event) override;
    void on_update(float delta) override;
    void on_render() override;
    void on_render_overlay() override;
    void on_enter() override;
    void on_exit() override;

private:
    std::unique_ptr<GameScene> game_;

    SDL_FRect panel_{};
    SDL_FRect skip_btn_{};
    SDL_FRect continue_btn_{};
    SDL_FRect menu_btn_{};

    void render_tutorial();
    bool handle_click(float x, float y);
    bool point_in(const SDL_FRect& r, float x, float y) const;
};

#endif // !_TUTORIAL_SCENE_H_
