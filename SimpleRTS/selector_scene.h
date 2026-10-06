#ifndef _SELECTOR_SCENE_H_
#define _SELECTOR_SCENE_H_

#include "scene_mgr.h"
#include "match_config.h"
#include "game_map.h"
#include "map_io.h"
#include "render_texture.h"
#include "player_palette.h"
#include <vector>
#include <string>

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
    // 单个 AI 槽位对应的 UI 行（在 layout() 中按当前 AI 槽位重建）
    struct AIRow {
        int slot = -1;
        float y = 0.0f;
        std::vector<SDL_FRect> color;   // 8 个颜色块
        std::vector<SDL_FRect> team;    // N 个阵营按钮（与玩家上限一致）
        std::vector<SDL_FRect> pos;     // N 个位置按钮
        SDL_FRect remove{};
    };

    MatchConfig config_;

    // 地图
    std::vector<std::string> map_files_;
    int map_sel_ = -1;
    float map_scroll_ = 0.0f;
    std::vector<SDL_FRect> map_entry_rects_;   // 未滚动时的基准矩形
    float map_list_top_ = 92.0f, map_list_bottom_ = 300.0f;

    // 预览
    GameMap preview_map_;
    std::vector<EntityRecord> preview_entities_;
    RenderTexture preview_bake_;
    bool preview_dirty_ = true;
    SDL_FRect preview_rect_{};

    // 人类设置 UI
    std::vector<SDL_FRect> human_color_;   // 8
    std::vector<SDL_FRect> human_team_;    // N
    std::vector<SDL_FRect> human_pos_;     // N

    // AI UI
    std::vector<AIRow> ai_rows_;
    SDL_FRect add_ai_btn_{};

    // 按钮
    SDL_FRect prev_btn_{}, next_btn_{};
    SDL_FRect start_btn_{}, back_btn_{};

    float mouse_x_ = 0.0f, mouse_y_ = 0.0f;

    void layout();
    void refresh_map_files();
    void apply_map_selection(int idx);
    void load_map_preview(const std::string& name);
    void reset_slots_default();
    void bake_preview();

    void add_ai();
    void remove_ai(int slot);
    int  next_free_color() const;
    bool color_taken(int color_id, int exclude_slot) const;   // 颜色是否已被其他玩家占用
    bool position_taken(int pos, int exclude_slot) const;     // 位置是否已被其他玩家占用
    int  next_free_position() const;                          // 找一个未被占用的位置

    void do_start();
    void do_back();
    bool point_in(const SDL_FRect& r, float x, float y) const;
    int  hit_index(const std::vector<SDL_FRect>& v, float x, float y) const;

    void draw_button(const SDL_FRect& r, const std::string& label, bool active, bool hover);
    void draw_swatch(const SDL_FRect& r, int color_id, bool selected, bool hover, bool disabled = false);
    void draw_pos_button(const SDL_FRect& r, int idx, bool active, bool hover, bool disabled = false);
};

#endif // !_SELECTOR_SCENE_H_
