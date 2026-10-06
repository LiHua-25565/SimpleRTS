#include "selector_scene.h"
#include "game_scene.h"
#include "scene_ui.h"
#include "texture_cache.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
// 横向等距矩形（色块）
std::vector<SDL_FRect> swatch_row(float x, float y, int count, float size, float gap) {
    std::vector<SDL_FRect> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i)
        v.push_back({ x + i * (size + gap), y, size, size });
    return v;
}

// 横向等距矩形（按钮）
std::vector<SDL_FRect> btn_row(float x, float y, int count, float w, float h, float gap) {
    std::vector<SDL_FRect> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i)
        v.push_back({ x + i * (w + gap), y, w, h });
    return v;
}

// 预览里资源的颜色
SDL_Color resource_preview_color(int type) {
    switch ((ResourceEntityType)type) {
    case ResourceEntityType::Wood:    return { 90, 160, 70, 255 };
    case ResourceEntityType::SGold:
    case ResourceEntityType::LGold:   return { 230, 200, 60, 255 };
    case ResourceEntityType::Stone:   return { 160, 160, 170, 255 };
    case ResourceEntityType::Berries: return { 220, 130, 60, 255 };
    default: return { 200, 200, 200, 255 };
    }
}
}

void SelectorScene::on_enter()
{
    config_ = MatchConfig{};
    preview_map_ = GameMap{};
    preview_entities_.clear();
    preview_dirty_ = true;
    map_scroll_ = 0.0f;
    refresh_map_files();
    if (map_files_.empty()) {
        // 无地图时仍给出一个空预览与默认槽位
        load_map_preview("");
    }
    layout();
}

void SelectorScene::on_exit()
{
}

void SelectorScene::on_update(float delta)
{
    (void)delta;
}

bool SelectorScene::point_in(const SDL_FRect& r, float x, float y) const
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

int SelectorScene::hit_index(const std::vector<SDL_FRect>& v, float x, float y) const
{
    for (size_t i = 0; i < v.size(); ++i)
        if (point_in(v[i], x, y)) return (int)i;
    return -1;
}

void SelectorScene::refresh_map_files()
{
    map_files_.clear();
    map_sel_ = -1;
    std::error_code ec;
    std::filesystem::path dir = "maps";
    if (!std::filesystem::exists(dir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".srmap") continue;
        map_files_.push_back(entry.path().stem().string());
    }
    std::sort(map_files_.begin(), map_files_.end());
    if (!map_files_.empty())
        apply_map_selection(0);
}

void SelectorScene::apply_map_selection(int idx)
{
    if (idx < 0 || idx >= (int)map_files_.size()) return;
    map_sel_ = idx;
    load_map_preview(map_files_[idx]);
}

void SelectorScene::reset_slots_default()
{
    int n = config_.map_player_count;
    config_.slots.assign(n, PlayerSlot{});
    if (n >= 1) {
        config_.slots[0].kind = SlotKind::Human;
        config_.slots[0].color_id = 0;
        config_.slots[0].team = 0;
        config_.slots[0].position = 0;
    }
    for (int i = 1; i < n; ++i) {
        config_.slots[i].kind = SlotKind::Neutral;
        config_.slots[i].color_id = i % PLAYER_COLOR_COUNT;
        config_.slots[i].team = 0;
        config_.slots[i].position = i;
    }
}

void SelectorScene::load_map_preview(const std::string& name)
{
    config_.map_name = name;
    preview_entities_.clear();
    int pc = 2;
    std::vector<EntityRecord> loaded;
    bool ok = false;
    if (!name.empty())
        ok = load_map_resize("maps/" + name + ".srmap", preview_map_, loaded, pc);
    if (!ok) {
        preview_map_.resize(300, 200);
        loaded.clear();
        pc = 2;
    }
    preview_entities_ = std::move(loaded);
    config_.map_player_count = pc;
    config_.spawn_points = compute_spawn_points(preview_map_, preview_entities_, pc);
    preview_dirty_ = true;
    reset_slots_default();
    layout();
}

void SelectorScene::bake_preview()
{
    if (!renderer) return;
    int cs = preview_map_.get_cell_size();
    int w = preview_map_.get_width();
    int h = preview_map_.get_height();
    if (w <= 0 || h <= 0) return;
    if (!preview_bake_.create(w * cs, h * cs, renderer)) return;

    preview_bake_.begin(renderer);
    const auto& grid = preview_map_.get_grid();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            SDL_FRect rc{ (float)(x * cs), (float)(y * cs), (float)cs, (float)cs };
            if (grid[y][x] == TerrainType::Water)
                SDL_SetRenderDrawColor(renderer, 30, 80, 200, 255);
            else
                SDL_SetRenderDrawColor(renderer, 122, 92, 58, 255);
            SDL_RenderFillRect(renderer, &rc);
        }
    }
    preview_bake_.end(renderer);
    preview_dirty_ = false;
}

int SelectorScene::next_free_color() const
{
    bool used[PLAYER_COLOR_COUNT] = { false };
    for (auto& s : config_.slots)
        if (s.kind != SlotKind::Neutral)
            used[s.color_id % PLAYER_COLOR_COUNT] = true;
    for (int c = 0; c < PLAYER_COLOR_COUNT; ++c)
        if (!used[c]) return c;
    return 0;
}

bool SelectorScene::color_taken(int color_id, int exclude_slot) const
{
    int c = color_id % PLAYER_COLOR_COUNT;
    for (int i = 0; i < (int)config_.slots.size(); ++i) {
        if (i == exclude_slot) continue;
        if (config_.slots[i].kind == SlotKind::Neutral) continue;
        if ((config_.slots[i].color_id % PLAYER_COLOR_COUNT) == c) return true;
    }
    return false;
}

bool SelectorScene::position_taken(int pos, int exclude_slot) const
{
    for (int i = 0; i < (int)config_.slots.size(); ++i) {
        if (i == exclude_slot) continue;
        if (config_.slots[i].kind == SlotKind::Neutral) continue;
        if (config_.slots[i].position == pos) return true;
    }
    return false;
}

int SelectorScene::next_free_position() const
{
    int n = config_.map_player_count;
    for (int p = 0; p < n; ++p)
        if (!position_taken(p, -1)) return p;
    return 0;
}

void SelectorScene::add_ai()
{
    int n = config_.map_player_count;
    for (int i = 1; i < n; ++i) {
        if (config_.slots[i].kind == SlotKind::Neutral) {
            config_.slots[i].kind = SlotKind::AI;
            config_.slots[i].color_id = next_free_color();
            config_.slots[i].team = i;   // 每个玩家默认独立阵营（槽位下标即阵营编号，0..N-1）
            config_.slots[i].position = next_free_position();
            layout();
            return;
        }
    }
}

void SelectorScene::remove_ai(int slot)
{
    if (slot < 1 || slot >= (int)config_.slots.size()) return;
    config_.slots[slot].kind = SlotKind::Neutral;
    layout();
}

void SelectorScene::do_start()
{
    GameScene* game = SceneMgr::instance()->get_game_scene();
    if (game)
        game->set_match_config(config_);
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Game);
}

void SelectorScene::do_back()
{
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
}

void SelectorScene::layout()
{
    human_color_.clear(); human_team_.clear(); human_pos_.clear();
    ai_rows_.clear(); map_entry_rects_.clear();

    const float left_x = 20.0f, left_w = 420.0f;
    const float right_x = 470.0f, right_w = 790.0f;

    map_list_top_ = 92.0f; map_list_bottom_ = 300.0f;
    const float entry_h = 30.0f;
    for (size_t i = 0; i < map_files_.size(); ++i)
        map_entry_rects_.push_back({ left_x, map_list_top_ + (float)i * entry_h, left_w, entry_h - 2.0f });

    prev_btn_ = { left_x, 310.0f, 200.0f, 36.0f };
    next_btn_ = { left_x + 220.0f, 310.0f, 200.0f, 36.0f };
    preview_rect_ = { left_x, 384.0f, left_w, 280.0f };

    // 人类设置
    human_color_ = swatch_row(right_x + 80.0f, 76.0f, PLAYER_COLOR_COUNT, 34.0f, 8.0f);
    human_team_  = btn_row(right_x + 80.0f, 128.0f, config_.map_player_count, 44.0f, 30.0f, 6.0f);
    human_pos_   = btn_row(right_x + 80.0f, 180.0f, config_.map_player_count, 40.0f, 30.0f, 6.0f);

    // AI 区
    add_ai_btn_ = { right_x + right_w - 140.0f, 226.0f, 140.0f, 30.0f };

    float ay = 272.0f;
    for (int i = 1; i < config_.map_player_count; ++i) {
        if (config_.slots[i].kind != SlotKind::AI) continue;
        AIRow row;
        row.slot = i;
        row.y = ay;
        row.color = swatch_row(right_x + 52.0f, ay + 2.0f, PLAYER_COLOR_COUNT, 18.0f, 3.0f);
        float cx = right_x + 52.0f + PLAYER_COLOR_COUNT * (18.0f + 3.0f) + 6.0f;
        row.team = btn_row(cx, ay + 1.0f, config_.map_player_count, 24.0f, 22.0f, 3.0f);
        cx += config_.map_player_count * (24.0f + 3.0f) + 8.0f;
        row.pos = btn_row(cx, ay + 1.0f, config_.map_player_count, 24.0f, 22.0f, 3.0f);
        row.remove = { right_x + right_w - 64.0f, ay + 1.0f, 56.0f, 22.0f };
        ai_rows_.push_back(row);
        ay += 34.0f;
    }

    start_btn_ = { right_x, 648.0f, 280.0f, 56.0f };
    back_btn_  = { right_x + 310.0f, 648.0f, 200.0f, 56.0f };
}

void SelectorScene::draw_button(const SDL_FRect& r, const std::string& label, bool active, bool hover)
{
    SDL_Color bg = active ? SDL_Color{ 62, 122, 62, 255 }
        : (hover ? SDL_Color{ 52, 70, 120, 255 } : SDL_Color{ 36, 48, 84, 255 });
    SDL_Color bd = (active || hover) ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
    ui_fill_rect(renderer, r, bg, bd, active ? 3 : 2);
    ui_draw_text(renderer, label, 16, { 255, 255, 255, 255 },
                 r.x + r.w * 0.5f, r.y + r.h * 0.5f, true);
}

void SelectorScene::draw_swatch(const SDL_FRect& r, int color_id, bool selected, bool hover, bool disabled)
{
    SDL_Color c = player_color_sdl(color_id);
    if (disabled) {   // 已被其他玩家占用：压暗，表示不可选
        c.r = c.r / 4; c.g = c.g / 4; c.b = c.b / 4;
    }
    SDL_Color bd = selected ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 120, 130, 150, 255 };
    ui_fill_rect(renderer, r, c, bd, selected ? 3 : (hover ? 2 : 1));
}

void SelectorScene::draw_pos_button(const SDL_FRect& r, int idx, bool active, bool hover, bool disabled)
{
    SDL_Color pc = player_color_sdl(idx % PLAYER_COLOR_COUNT);
    SDL_Color bg = active ? pc : SDL_Color{
        (Uint8)(pc.r / 4 + 18), (Uint8)(pc.g / 4 + 18), (Uint8)(pc.b / 4 + 18), 255 };
    if (disabled && !active) {   // 已被其他玩家占用：压暗，表示不可选
        bg = SDL_Color{ (Uint8)(bg.r / 3), (Uint8)(bg.g / 3), (Uint8)(bg.b / 3), 255 };
    }
    SDL_Color bd = (active || hover) ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
    ui_fill_rect(renderer, r, bg, bd, active ? 3 : 2);
    ui_draw_text(renderer, std::to_string(idx + 1), 16, { 255, 255, 255, 255 },
                 r.x + r.w * 0.5f, r.y + r.h * 0.5f, true);
}

void SelectorScene::on_input(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        float x = event.button.x, y = event.button.y;

        // 底部按钮
        if (point_in(start_btn_, x, y)) { do_start(); return; }
        if (point_in(back_btn_, x, y)) { do_back(); return; }

        // 地图：上一张 / 下一张
        if (point_in(prev_btn_, x, y)) {
            if (!map_files_.empty()) apply_map_selection((map_sel_ - 1 + (int)map_files_.size()) % (int)map_files_.size());
            return;
        }
        if (point_in(next_btn_, x, y)) {
            if (!map_files_.empty()) apply_map_selection((map_sel_ + 1) % (int)map_files_.size());
            return;
        }

        // 地图列表
        if (y >= map_list_top_ && y <= map_list_bottom_) {
            float content_y = y + map_scroll_;
            for (size_t i = 0; i < map_entry_rects_.size(); ++i) {
                if (point_in(map_entry_rects_[i], x, content_y)) {
                    apply_map_selection((int)i);
                    return;
                }
            }
        }

        // 人类：颜色（已被其他玩家占用的颜色不可选）
        int ci = hit_index(human_color_, x, y);
        if (ci >= 0) {
            if (!color_taken(ci, 0)) config_.slots[0].color_id = ci;
            return;
        }
        // 人类：阵营
        int ti = hit_index(human_team_, x, y);
        if (ti >= 0) { config_.slots[0].team = ti; return; }
        // 人类：位置（已被其他玩家占用的位置不可选）
        int pi = hit_index(human_pos_, x, y);
        if (pi >= 0) {
            if (!position_taken(pi, 0)) config_.slots[0].position = pi;
            return;
        }

        // 添加 AI
        if (point_in(add_ai_btn_, x, y)) { add_ai(); return; }

        // AI 行
        for (auto& row : ai_rows_) {
            int slot = row.slot;
            int aci = hit_index(row.color, x, y);
            if (aci >= 0) {
                if (!color_taken(aci, slot)) config_.slots[slot].color_id = aci;
                return;
            }
            int ati = hit_index(row.team, x, y);
            if (ati >= 0) { config_.slots[slot].team = ati; return; }
            int api = hit_index(row.pos, x, y);
            if (api >= 0) {
                if (!position_taken(api, slot)) config_.slots[slot].position = api;
                return;
            }
            if (point_in(row.remove, x, y)) { remove_ai(slot); return; }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        float x = event.wheel.mouse_x, y = event.wheel.mouse_y;
        if (x >= 20.0f && x <= 440.0f && y >= map_list_top_ && y <= map_list_bottom_) {
            float content = (float)map_files_.size() * 30.0f;
            float max_scroll = std::max(0.0f, content - (map_list_bottom_ - map_list_top_));
            map_scroll_ = std::clamp(map_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
            do_start();
        else if (event.key.key == SDLK_ESCAPE)
            do_back();
    }
}

void SelectorScene::on_render()
{
    SDL_SetRenderDrawColor(renderer, 16, 22, 36, 255);
    SDL_RenderClear(renderer);

    ui_fill_rect(renderer, SDL_FRect{ 0, 0, LOGICAL_W, 8.0f }, { 255, 215, 0, 255 });
    ui_draw_text(renderer, u8"对局设置", 44, { 255, 215, 0, 255 }, LOGICAL_W * 0.5f, 44.0f, true);

    // ============ 左：地图选择 + 预览 ============
    ui_draw_text(renderer, u8"地图选择", 22, { 200, 210, 230, 255 }, 20.0f, 68.0f, false);

    {
        SDL_Rect clip{ 20, (int)map_list_top_, 420, (int)(map_list_bottom_ - map_list_top_) };
        SDL_SetRenderClipRect(renderer, &clip);
        if (map_files_.empty())
            ui_draw_text(renderer, u8"（暂无地图，请先到地图编辑创建）", 15, { 150, 160, 180, 255 }, 24.0f, map_list_top_ + 6.0f, false);
        for (size_t i = 0; i < map_entry_rects_.size(); ++i) {
            SDL_FRect r = map_entry_rects_[i];
            r.y -= map_scroll_;
            bool sel = ((int)i == map_sel_);
            draw_button(r, map_files_[i], sel, false);
        }
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    draw_button(prev_btn_, u8"< 上一张", false, point_in(prev_btn_, mouse_x_, mouse_y_));
    draw_button(next_btn_, u8"下一张 >", false, point_in(next_btn_, mouse_x_, mouse_y_));

    ui_draw_text(renderer, u8"地图预览", 22, { 200, 210, 230, 255 }, 20.0f, 358.0f, false);
    ui_fill_rect(renderer, preview_rect_, { 24, 30, 48, 255 }, { 90, 110, 160, 255 }, 2);

    // 预览内容
    if (preview_dirty_) bake_preview();
    {
        int cs = preview_map_.get_cell_size();
        int mw = preview_map_.get_width() * cs;
        int mh = preview_map_.get_height() * cs;
        if (mw > 0 && mh > 0) {
            float scale = std::min(preview_rect_.w / (float)mw, preview_rect_.h / (float)mh);
            float ox = preview_rect_.x + (preview_rect_.w - mw * scale) * 0.5f;
            float oy = preview_rect_.y + (preview_rect_.h - mh * scale) * 0.5f;

            SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(preview_bake_.get_texture_id());
            if (tex) {
                SDL_FRect dst{ ox, oy, mw * scale, mh * scale };
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
            }

            // 资源/建筑/单位小方块
            for (auto& e : preview_entities_) {
                float px = 0, py = 0, pw = 0, ph = 0;
                SDL_Color c = { 200, 200, 200, 255 };
                if (e.kind == EntityRecord::Kind::Resource) {
                    px = e.gx * cs; py = e.gy * cs;
                    pw = ph = 8.0f * cs;
                    c = resource_preview_color(e.type);
                }
                else if (e.kind == EntityRecord::Kind::Building) {
                    if (e.type == (int)BuildingEntityType::TownCenter) continue;   // 起始标记
                    px = e.gx * cs; py = e.gy * cs;
                    pw = ph = 20.0f * cs;
                    c = { 120, 120, 130, 255 };
                }
                else if (e.kind == EntityRecord::Kind::Unit) {
                    px = e.wx; py = e.wy; pw = (float)(UNIT_SIZE_CELLS * cs); ph = pw;
                    c = { 220, 220, 220, 255 };
                }
                if (pw <= 0) continue;
                SDL_FRect dr{ ox + px * scale, oy + py * scale, pw * scale, ph * scale };
                if (dr.w < 2.0f) dr.w = 2.0f;
                if (dr.h < 2.0f) dr.h = 2.0f;
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(renderer, &dr);
            }

            // 起始位置标记：彩色方块 + 白色数字编号（1..N）
            for (size_t i = 0; i < config_.spawn_points.size(); ++i) {
                const SpawnPoint& sp = config_.spawn_points[i];
                SDL_Color mc = player_color_sdl((int)i % PLAYER_COLOR_COUNT);
                float sx = ox + (sp.gx + 10) * cs * scale;
                float sy = oy + (sp.gy + 10) * cs * scale;
                float sr = std::max(5.0f, 10.0f * cs * scale * 0.5f);
                // 深色底框，保证数字在各种地形上都清晰
                SDL_FRect shadow{ sx - sr - 1.0f, sy - sr - 1.0f, sr * 2 + 2.0f, sr * 2 + 2.0f };
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
                SDL_RenderFillRect(renderer, &shadow);
                // 彩色方块（位置颜色）
                SDL_FRect mr{ sx - sr, sy - sr, sr * 2, sr * 2 };
                SDL_SetRenderDrawColor(renderer, mc.r, mc.g, mc.b, 255);
                SDL_RenderFillRect(renderer, &mr);
                // 白色数字编号
                ui_draw_text(renderer, std::to_string(i + 1), 11, { 255, 255, 255, 255 }, sx, sy, true);
            }
        }
    }

    // ============ 右：玩家设置 ============
    const float right_x = 470.0f;

    // 你的设置
    ui_draw_text(renderer, u8"你的设置", 24, { 255, 215, 0, 255 }, right_x, 78.0f, false);
    ui_draw_text(renderer, u8"颜色", 18, { 200, 210, 230, 255 }, right_x + 20.0f, 90.0f, false);
    for (int i = 0; i < (int)human_color_.size(); ++i)
        draw_swatch(human_color_[i], i, (config_.slots[0].color_id == i),
                    point_in(human_color_[i], mouse_x_, mouse_y_),
                    color_taken(i, 0));

    ui_draw_text(renderer, u8"阵营", 18, { 200, 210, 230, 255 }, right_x + 20.0f, 142.0f, false);
    for (int i = 0; i < (int)human_team_.size(); ++i)
        draw_button(human_team_[i], std::to_string(i + 1), (config_.slots[0].team == i),
                    point_in(human_team_[i], mouse_x_, mouse_y_));

    ui_draw_text(renderer, u8"位置", 18, { 200, 210, 230, 255 }, right_x + 20.0f, 194.0f, false);
    for (int i = 0; i < (int)human_pos_.size(); ++i)
        draw_pos_button(human_pos_[i], i, (config_.slots[0].position == i),
                        point_in(human_pos_[i], mouse_x_, mouse_y_),
                        position_taken(i, 0));

    // AI 玩家区
    ui_draw_text(renderer, u8"AI 玩家", 24, { 255, 215, 0, 255 }, right_x, 240.0f, false);
    bool ai_full = (config_.ai_count() >= config_.map_player_count - 1);
    draw_button(add_ai_btn_, ai_full ? u8"已满" : u8"+ 添加 AI", ai_full,
                point_in(add_ai_btn_, mouse_x_, mouse_y_));
    ui_draw_text(renderer, u8"空槽位开局转为中立阵营", 14, { 150, 160, 180, 255 }, right_x + 180.0f, 252.0f, false);

    if (ai_rows_.empty())
        ui_draw_text(renderer, u8"（点击 + 添加 AI 加入电脑玩家）", 15, { 150, 160, 180, 255 }, right_x, 278.0f, false);

    for (auto& row : ai_rows_) {
        int slot = row.slot;
        ui_draw_text(renderer, u8"AI " + std::to_string(slot), 16, { 220, 224, 235, 255 }, right_x, row.y + 10.0f, false);
        for (int i = 0; i < (int)row.color.size(); ++i)
            draw_swatch(row.color[i], i, (config_.slots[slot].color_id == i),
                        point_in(row.color[i], mouse_x_, mouse_y_),
                        color_taken(i, slot));
        for (int i = 0; i < (int)row.team.size(); ++i)
            draw_button(row.team[i], std::to_string(i + 1), (config_.slots[slot].team == i),
                        point_in(row.team[i], mouse_x_, mouse_y_));
        for (int i = 0; i < (int)row.pos.size(); ++i)
            draw_pos_button(row.pos[i], i, (config_.slots[slot].position == i),
                            point_in(row.pos[i], mouse_x_, mouse_y_),
                            position_taken(i, slot));
        draw_button(row.remove, u8"移除", false, point_in(row.remove, mouse_x_, mouse_y_));
    }

    // 底部按钮
    draw_button(start_btn_, u8"开始对局", false, point_in(start_btn_, mouse_x_, mouse_y_));
    draw_button(back_btn_, u8"返回", false, point_in(back_btn_, mouse_x_, mouse_y_));
}
