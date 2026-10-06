#ifndef _SCENE_MGR_H_
#define _SCENE_MGR_H_

#include "scene.h"
#include <SDL3/SDL.h>

class GameScene;

class SceneMgr
{
public:
    enum class SceneType
    {
        Menu,
        Game,
        Selector,
        MapEditor
    };

public:
    static SceneMgr* instance();

    void set_current_scene(Scene* scene);
    void set_renderer(SDL_Renderer* renderer);

    void on_switch(SceneType type);
    void on_update(float delta);
    void on_render();
    void on_input(const SDL_Event& event);

    // 场景注册函数
    void set_menu_scene(Scene* scene) { menu_scene = scene; }
    void set_game_scene(Scene* scene) { game_scene = scene; }
    void set_selector_scene(Scene* scene) { selector_scene = scene; }
    void set_map_editor_scene(Scene* scene) { map_editor_scene = scene; }

    GameScene* get_game_scene() const;

    // 退出请求（菜单“退出游戏”按钮触发，主循环据此退出）
    void request_quit() { quit_requested = true; }
    bool should_quit() const { return quit_requested; }

private:
    SceneMgr();
    ~SceneMgr();

    Scene* current_scene = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool quit_requested = false;

    Scene* menu_scene = nullptr;
    Scene* game_scene = nullptr;
    Scene* selector_scene = nullptr;
    Scene* map_editor_scene = nullptr;
};


#endif // !_SCENE_MGR_H_

