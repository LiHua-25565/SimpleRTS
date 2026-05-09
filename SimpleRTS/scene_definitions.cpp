#include "scene.h"
#include "game_scene.h"
#include "menu_scene.h"
#include "selector_scene.h"

Scene* menu_scene = new MenuScene();
Scene* game_scene = new GameScene();
Scene* selector_scene = new SelectorScene();