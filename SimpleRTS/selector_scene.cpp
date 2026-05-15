#include "selector_scene.h"
#include "game_scene.h"
#include "scene_mgr.h"
#include <iostream>

void SelectorScene::on_enter()
{
    std::cout << "Select player faction:" << std::endl;
    std::cout << "  1 - Player 1 (Blue)" << std::endl;
    std::cout << "  2 - Player 2 (Red)" << std::endl;
    std::cout << "Press 1 or 2 and Enter: ";

    int choice = 0;
    std::cin >> choice;

    if (true) {
        // 获取 GameScene 实例并设置本地玩家 ID
        GameScene* game = SceneMgr::instance()->get_game_scene();
        if (game)
            game->set_local_player_id(choice);
        else
            std::cerr << "GameScene not registered!" << std::endl;

        // 标记可以切换（下一帧 on_update 会执行切换）
        ready_to_switch = true;
    }
    else {
        std::cout << "Invalid choice, defaulting to player 1." << std::endl;
        GameScene* game = SceneMgr::instance()->get_game_scene();
        if (game) game->set_local_player_id(1);
        ready_to_switch = true;
    }
}

void SelectorScene::on_update(float delta)
{
    if (ready_to_switch) {
        SceneMgr::instance()->on_switch(SceneMgr::SceneType::Game);
    }
}