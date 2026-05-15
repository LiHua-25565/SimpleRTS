#ifndef _SELECTOR_SCENE_H_
#define _SELECTOR_SCENE_H_

#include "scene_mgr.h"

class SelectorScene : public Scene {
public:
    SelectorScene() = default;
    ~SelectorScene() = default;

    void on_enter() override;
    void on_input(const SDL_Event& event) override {};
    void on_update(float delta) override;
    void on_render() override {}
    void on_exit() override {}

private:
    bool ready_to_switch = false;
};

#endif // !_SELECTOR_SCENE_H_
