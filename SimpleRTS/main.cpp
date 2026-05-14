#include "cursor_mgr.h"
#include "scene_mgr.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "UI_mgr.h"
#include "game_scene.h"
#include "menu_scene.h"
#include "selector_scene.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>

const int LOGICAL_W = 1280;
const int LOGICAL_H = 720;

void init()
{
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO);
    TTF_Init();
    MIX_Init();
}

void quit()
{
    UIMgr::instance()->shutdown();
    SDL_Quit();
    TTF_Quit();
    MIX_Quit();
}

int main(int argc, char* argv[]) 
{
    using namespace std::chrono;
    init();

    SDL_Window* window = SDL_CreateWindow(u8"MySTR",
        1280, 720, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    TTF_Font* font = TTF_OpenFont("font/SourceHanSansSC-Bold.otf", 18);
    if (!font)
    {
        SDL_Log("UIMgr: Fail to load font: %s", SDL_GetError());
    }
    UIMgr::instance()->init(renderer, font);
    game_scene->set_renderer(renderer);
    game_scene->set_font(font);

    bool is_fullscreen = false;

    RenderMgr::instance()->set_world_size(8000, 8000);
    RenderMgr::instance()->set_minimap_position(20, 20);
    RenderMgr::instance()->set_minimap_size(160, 160);

    // 设置逻辑分辨率
    SDL_SetRenderLogicalPresentation(
        renderer,
        1280, 720,
        SDL_LOGICAL_PRESENTATION_LETTERBOX // 等比例留黑边
    );

    // 创建场景

    SceneMgr::instance()->set_current_scene(game_scene);

    SDL_Event event;
    bool is_quit = false;

    const nanoseconds frame_duration(1000000000 / 144);
    steady_clock::time_point last_tick = steady_clock::now();

    while (!is_quit) {
        // 输入
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                is_quit = true;

            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                // 监听F11按键按下
                if (event.key.key == SDLK_F11)
                {
                    is_fullscreen = !is_fullscreen;
                    SDL_SetWindowFullscreen(window, is_fullscreen);
                }
                // ESC 键
                if (event.key.key == SDLK_ESCAPE)
                {
                    // 打开菜单
                }
            }

            SDL_ConvertEventToRenderCoordinates(renderer, &event);

            SceneMgr::instance()->on_input(event);
        }

        // 逻辑

        RenderMgr::instance()->begin_frame();

        steady_clock::time_point frame_start = steady_clock::now();
        duration<float> delta = duration<float>(frame_start - last_tick);

        SceneMgr::instance()->on_update(delta.count());

        // 渲染

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SceneMgr::instance()->on_render();
        RenderMgr::instance()->end_frame(renderer);
        SDL_RenderPresent(renderer);

        last_tick = frame_start;
        nanoseconds sleep_duration = frame_duration - (steady_clock::now() - frame_start);
        if (sleep_duration > nanoseconds(0))
            std::this_thread::sleep_for(sleep_duration);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    quit();

    return 0;
}