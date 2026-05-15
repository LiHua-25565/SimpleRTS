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
        Selector
    };

public:
    static SceneMgr* instance();

    void set_current_scene(Scene* scene);
    void set_renderer(SDL_Renderer* renderer);

    void on_switch(SceneType type);
    void on_update(float delta);
    void on_render();
    void on_input(const SDL_Event& event);

    // ³¡¾°×¢²áº¯Êý
    void set_menu_scene(Scene* scene) { menu_scene = scene; }
    void set_game_scene(Scene* scene) { game_scene = scene; }
    void set_selector_scene(Scene* scene) { selector_scene = scene; }

    GameScene* get_game_scene() const;

private:
    SceneMgr();
    ~SceneMgr();

    Scene* current_scene = nullptr;
    SDL_Renderer* renderer = nullptr;

    Scene* menu_scene = nullptr;
    Scene* game_scene = nullptr;
    Scene* selector_scene = nullptr;
};


#endif // !_SCENE_MGR_H_

