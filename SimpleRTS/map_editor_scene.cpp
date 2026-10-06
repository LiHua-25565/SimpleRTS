#include "map_editor_scene.h"
#include "scene_mgr.h"
#include "scene_ui.h"
#include "world_entity_mgr.h"
#include "texture_cache.h"
#include "components.h"
#include "color.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

// 玩家外框颜色（0=中立灰，1..8=各玩家专属色，用于编辑器里单位/建筑的外框与选中高亮）
static SDL_Color player_frame_color(int pid) {
    switch (pid) {
    case 1:  return { 80, 130, 255, 255 };   // 蓝
    case 2:  return { 255, 90, 90, 255 };    // 红
    case 3:  return { 90, 200, 90, 255 };    // 绿
    case 4:  return { 240, 210, 60, 255 };   // 黄
    case 5:  return { 180, 110, 220, 255 };  // 紫
    case 6:  return { 255, 160, 60, 255 };   // 橙
    case 7:  return { 70, 210, 210, 255 };   // 青
    case 8:  return { 240, 110, 220, 255 };  // 品红
    default: return { 150, 150, 150, 255 };  // 中立灰
    }
}

// ==================== 工具辅助 ====================

bool MapEditorScene::is_terrain_tool(Tool t) const { return t == Tool::Land || t == Tool::Water; }
bool MapEditorScene::is_resource_tool(Tool t) const { return t >= Tool::Wood && t <= Tool::Berries; }
bool MapEditorScene::is_unit_tool(Tool t) const { return t >= Tool::Villager && t <= Tool::Crossbowman; }
bool MapEditorScene::is_building_tool(Tool t) const { return t >= Tool::TownCenter && t <= Tool::ArcheryRange; }

ResourceEntityType MapEditorScene::tool_to_resource(Tool t) const {
    switch (t) {
    case Tool::Wood:    return ResourceEntityType::Wood;
    case Tool::SGold:   return ResourceEntityType::SGold;
    case Tool::LGold:   return ResourceEntityType::LGold;
    case Tool::Stone:   return ResourceEntityType::Stone;
    case Tool::Berries: return ResourceEntityType::Berries;
    default: return ResourceEntityType::Wood;
    }
}

UnitEntityType MapEditorScene::tool_to_unit(Tool t) const {
    switch (t) {
    case Tool::Villager:     return UnitEntityType::Villager;
    case Tool::Archer:       return UnitEntityType::Archer;
    case Tool::Crossbowman:  return UnitEntityType::Crossbowman;
    default: return UnitEntityType::Villager;
    }
}

BuildingEntityType MapEditorScene::tool_to_building(Tool t) const {
    switch (t) {
    case Tool::TownCenter:   return BuildingEntityType::TownCenter;
    case Tool::ArcheryRange: return BuildingEntityType::ArcheryRange;
    default: return BuildingEntityType::TownCenter;
    }
}

// 与 factories.cpp 中的尺寸保持一致（资源/建筑占用格数）
int MapEditorScene::resource_size_cells(ResourceEntityType t) {
    switch (t) {
    case ResourceEntityType::Wood:    return 4;
    case ResourceEntityType::SGold:   return 10;
    case ResourceEntityType::LGold:   return 15;
    case ResourceEntityType::Stone:   return 10;
    case ResourceEntityType::Berries: return 5;
    }
    return 4;
}

int MapEditorScene::building_size_cells(BuildingEntityType) {
    return 20;
}

// ==================== 生命周期 ====================

void MapEditorScene::on_enter() {
    // 进入编辑器：清空世界实体，使用编辑器自己的地图
    WorldEntityMgr::instance()->reset_world();
    editor_map_.clear_dynamic_obstacle_field();   // 清掉上次残留的动态障碍标记
    WorldEntityMgr::instance()->init_world(&editor_map_);
    factory_.init(&editor_map_);

    if (!initialized_) {
        initialized_ = true;
        editor_map_.reset_terrain(TerrainType::Mud);   // 空地图起步
        entities_.clear();
    }

    // 相机：让整张地图尽量铺满画布
    float vw = LOGICAL_W - panel_w_;
    float vh = LOGICAL_H - topbar_h_;
    camera_.init(vw, vh,
        (float)(editor_map_.get_width() * editor_map_.get_cell_size()),
        (float)(editor_map_.get_height() * editor_map_.get_cell_size()));
    float fit = std::min(vw / camera_.get_map_w(), vh / camera_.get_map_h()) * 0.98f;
    camera_.set_scale(fit);
    camera_.set_position({ 0.0f, 0.0f });

    cur_tool_ = Tool::Select;
    selected_id_ = 0;
    hp_focused_ = false;
    hp_input_.clear();
    filename_focused_ = false;
    terrain_dirty_ = true;
    tool_scroll_ = 0.0f;
    map_scroll_ = 0.0f;
    status_msg_.clear();
    status_timer_ = 0.0f;
    layout();
    refresh_map_files();
    filename_ = next_default_name();   // 每次进入自动命名：未使用的最大编号
    map_sel_ = -1;

    spawn_from_records();

    SDL_Window* win = SDL_GetRenderWindow(renderer);
    if (win) SDL_StartTextInput(win);
}

void MapEditorScene::on_exit() {
    snapshot_entities();   // 内容沉淀到 entities_，下次进入还原
    WorldEntityMgr::instance()->reset_world();
    SDL_Window* win = SDL_GetRenderWindow(renderer);
    if (win) SDL_StopTextInput(win);
}

void MapEditorScene::on_update(float delta) {
    (void)delta;
    if (static_dirty_) {
        editor_map_.rebuild_static_obstacle_field();
        static_dirty_ = false;
    }
    if (status_timer_ > 0.0f) {
        status_timer_ -= delta;
        if (status_timer_ <= 0.0f) { status_timer_ = 0.0f; status_msg_.clear(); }
    }

    // 键盘平移
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        float speed = 420.0f / camera_.get_scale() * delta;
        Vector2 p = camera_.get_position();
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) p.x -= speed;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) p.x += speed;
        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) p.y -= speed;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) p.y += speed;
        camera_.set_position(p);
    }
}

// ==================== 布局 ====================

void MapEditorScene::layout() {
    // 顶部工具条
    filename_rect_ = { 230.0f, 14.0f, 150.0f, 28.0f };
    btn_save_.rect  = { 392.0f, 12.0f, 72.0f, 32.0f };  btn_save_.label  = u8"保存";
    btn_load_.rect  = { 472.0f, 12.0f, 72.0f, 32.0f };  btn_load_.label  = u8"载入";
    btn_clear_.rect = { 552.0f, 12.0f, 72.0f, 32.0f };  btn_clear_.label = u8"清空";
    btn_back_.rect  = { 632.0f, 12.0f, 110.0f, 32.0f }; btn_back_.label  = u8"返回主菜单";

    // 地图尺寸
    size_label_y_ = 58.0f;
    size_small_ = { 8.0f, 78.0f, 56.0f, 22.0f };
    size_med_   = { 68.0f, 78.0f, 56.0f, 22.0f };
    size_large_ = { 128.0f, 78.0f, 56.0f, 22.0f };

    // 放置模式
    mode_label_y_ = 106.0f;
    mode_single_ = { 8.0f, 126.0f, 84.0f, 22.0f };
    mode_batch_  = { 100.0f, 126.0f, 84.0f, 22.0f };

    // 笔刷大小（地形工具）
    brush_label_y_ = 154.0f;
    brush_1_ = { 8.0f, 174.0f, 56.0f, 22.0f };
    brush_5_ = { 68.0f, 174.0f, 56.0f, 22.0f };
    brush_10_ = { 128.0f, 174.0f, 56.0f, 22.0f };

    // 工具列表可视区域
    tools_top_ = 202.0f;
    tools_bottom_ = 360.0f;

    // 玩家数量
    pc_label_y_ = 366.0f;
    pc_2_ = { 8.0f, 386.0f, 56.0f, 22.0f };
    pc_4_ = { 68.0f, 386.0f, 56.0f, 22.0f };
    pc_8_ = { 128.0f, 386.0f, 56.0f, 22.0f };

    // 归属玩家（4 列网格，最多 8 玩家 + 中立 = 9 项 = 3 行）
    owner_label_y_ = 414.0f;
    owner_grid_top_ = 434.0f;
    {
        const float col_w = 43.0f, row_h = 22.0f, gap_x = 4.0f, gap_y = 3.0f;
        owner_btns_.clear();
        owner_btns_.reserve((size_t)player_count_ + 1);
        for (int i = 0; i <= player_count_; ++i) {
            int col = i % 4;
            int row = i / 4;
            owner_btns_.push_back(SDL_FRect{
                8.0f + col * (col_w + gap_x),
                owner_grid_top_ + row * (row_h + gap_y),
                col_w, row_h });
        }
    }

    // 地图文件列表可视区域
    map_label_y_ = 512.0f;
    map_list_top_ = 532.0f;
    map_list_bottom_ = 584.0f;
    btn_delete_map_.rect = { 108.0f, 509.0f, 84.0f, 22.0f };  btn_delete_map_.label = u8"删除地图";

    // 选中信息面板
    sel_info_rect_ = { 8.0f, 590.0f, panel_w_ - 16.0f, 118.0f };
    hp_input_rect_ = { 16.0f, 636.0f, 168.0f, 28.0f };
    del_btn_       = { 16.0f, 672.0f, 168.0f, 30.0f };

    // 工具列表（含分区标题）
    sections_.clear();
    {
        ToolSection s; s.title = u8"地形";
        s.entries.push_back({ Tool::Land,  u8"陆地", SDL_FRect{} });
        s.entries.push_back({ Tool::Water, u8"河流", SDL_FRect{} });
        sections_.push_back(std::move(s));
    }
    {
        ToolSection s; s.title = u8"资源";
        s.entries.push_back({ Tool::Wood,    u8"树木",   SDL_FRect{} });
        s.entries.push_back({ Tool::SGold,   u8"小金矿", SDL_FRect{} });
        s.entries.push_back({ Tool::LGold,   u8"大金矿", SDL_FRect{} });
        s.entries.push_back({ Tool::Stone,   u8"石矿",   SDL_FRect{} });
        s.entries.push_back({ Tool::Berries, u8"浆果丛", SDL_FRect{} });
        sections_.push_back(std::move(s));
    }
    {
        ToolSection s; s.title = u8"兵种";
        s.entries.push_back({ Tool::Villager,    u8"农民", SDL_FRect{} });
        s.entries.push_back({ Tool::Archer,      u8"弓兵", SDL_FRect{} });
        s.entries.push_back({ Tool::Crossbowman, u8"弩手", SDL_FRect{} });
        sections_.push_back(std::move(s));
    }
    {
        ToolSection s; s.title = u8"建筑";
        s.entries.push_back({ Tool::TownCenter,   u8"城镇中心", SDL_FRect{} });
        s.entries.push_back({ Tool::ArcheryRange, u8"靶场",     SDL_FRect{} });
        sections_.push_back(std::move(s));
    }
    {
        ToolSection s; s.title = u8"操作";
        s.entries.push_back({ Tool::Select, u8"选择", SDL_FRect{} });
        sections_.push_back(std::move(s));
    }

    const float btn_h = 22.0f;
    const float header_h = 22.0f;   // 标题与首项间距，避免文字重叠
    const float x0 = 8.0f, x1 = panel_w_ - 8.0f;
    float y = tools_top_;
    for (auto& s : sections_) {
        s.title_y = y;
        y += header_h;
        for (auto& e : s.entries) {
            e.rect = { x0, y, x1 - x0, btn_h };
            y += btn_h + 2.0f;
        }
        y += 6.0f;   // 分区间隔
    }
    tools_content_h_ = y - tools_top_;   // 工具内容总高（用于滚动范围）
}

// ==================== 内容管理 ====================

void MapEditorScene::reset_to_empty() {
    editor_map_.reset_terrain(TerrainType::Mud);
    editor_map_.clear_dynamic_obstacle_field();
    WorldEntityMgr::instance()->reset_world();
    WorldEntityMgr::instance()->init_world(&editor_map_);
    factory_.init(&editor_map_);
    entities_.clear();
    selected_id_ = 0;
    terrain_dirty_ = true;
    static_dirty_ = false;
}

EntityRecord MapEditorScene::make_record(GameObject* obj) const {
    EntityRecord r;
    r.obj_id = obj->get_id();
    int cs = editor_map_.get_cell_size();
    const auto& box = obj->get_collision_box();
    auto* hb = obj->get_component<Harvestable>();
    auto* bt = obj->get_component<BuildingType>();
    auto* ut = obj->get_component<UnitType>();
    auto* own = obj->get_component<Ownership>();
    auto* hp = obj->get_component<Health>();

    if (hb) {
        r.kind = EntityRecord::Kind::Resource;
        r.type = (int)hb->entity_type;
        r.gx = (int)(box.position.x / cs);
        r.gy = (int)(box.position.y / cs);
        r.health = hp ? hp->current_health : 100;
    }
    else if (bt) {
        r.kind = EntityRecord::Kind::Building;
        r.type = (int)bt->type;
        r.gx = (int)(box.position.x / cs);
        r.gy = (int)(box.position.y / cs);
        r.player = own ? own->player_id : 0;
    }
    else if (ut) {
        r.kind = EntityRecord::Kind::Unit;
        r.type = (int)ut->type;
        r.wx = box.position.x;
        r.wy = box.position.y;
        r.player = own ? own->player_id : 0;
    }
    return r;
}

void MapEditorScene::snapshot_entities() {
    entities_.clear();
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj || !obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        bool is_entity = obj->get_component<Harvestable>()
            || obj->get_component<BuildingType>()
            || obj->get_component<UnitType>();
        if (!is_entity) continue;
        entities_.push_back(make_record(obj));
    }
}

GameObject* MapEditorScene::spawn_record(const EntityRecord& r) {
    GameObject* obj = nullptr;
    if (r.kind == EntityRecord::Kind::Resource) {
        factory_.set_player_id(0);
        obj = factory_.create_resource_by_type((ResourceEntityType)r.type, r.gx, r.gy, false);
        if (obj) {
            auto* hp = obj->get_component<Health>();
            if (hp) { hp->max_health = r.health; hp->current_health = r.health; }
        }
    }
    else if (r.kind == EntityRecord::Kind::Building) {
        factory_.set_player_id(r.player);
        obj = factory_.create_building_by_type((BuildingEntityType)r.type, r.gx, r.gy, false);
    }
    else if (r.kind == EntityRecord::Kind::Unit) {
        factory_.set_player_id(r.player);
        obj = factory_.create_unit_by_type((UnitEntityType)r.type,
            { { r.wx, r.wy }, 32.0f, 32.0f }, false);
    }
    return obj;
}

void MapEditorScene::spawn_from_records() {
    for (auto& rec : entities_) {
        GameObject* obj = spawn_record(rec);
        if (obj) rec.obj_id = obj->get_id();
    }
}

void MapEditorScene::remove_record(uint64_t obj_id) {
    for (auto it = entities_.begin(); it != entities_.end(); ++it) {
        if (it->obj_id == obj_id) { entities_.erase(it); return; }
    }
}

bool MapEditorScene::record_on_water(const EntityRecord& r) const {
    int w = editor_map_.get_width();
    int h = editor_map_.get_height();
    int cs = editor_map_.get_cell_size();
    const auto& grid = editor_map_.get_grid();

    auto water_at = [&](int cx, int cy) -> bool {
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) return true;
        return grid[cy][cx] == TerrainType::Water;
        };

    if (r.kind == EntityRecord::Kind::Unit) {
        return water_at((int)(r.wx / cs), (int)(r.wy / cs));
    }

    int size = (r.kind == EntityRecord::Kind::Resource)
        ? resource_size_cells((ResourceEntityType)r.type)
        : building_size_cells((BuildingEntityType)r.type);
    for (int yy = 0; yy < size; ++yy)
        for (int xx = 0; xx < size; ++xx)
            if (water_at(r.gx + xx, r.gy + yy)) return true;
    return false;
}

void MapEditorScene::cleanup_invalid() {
    bool removed = false;
    for (auto it = entities_.begin(); it != entities_.end(); ) {
        if (record_on_water(*it)) {
            GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(it->obj_id);
            if (obj) WorldEntityMgr::instance()->destroy_object(obj);
            it = entities_.erase(it);
            removed = true;
        }
        else {
            ++it;
        }
    }
    if (removed) {
        if (selected_id_ && !WorldEntityMgr::instance()->get_object_by_id(selected_id_))
            selected_id_ = 0;
        WorldEntityMgr::instance()->on_update();   // 清理失效实体 + 重建动态障碍
    }
}

// ==================== 输入 ====================

bool MapEditorScene::inside_viewport(float x, float y) const {
    return x >= panel_w_ && y >= topbar_h_ && x < LOGICAL_W && y < LOGICAL_H;
}

bool MapEditorScene::inside_panel(float x, float y) const {
    return x >= 0 && x < panel_w_ && y >= topbar_h_ && y < LOGICAL_H;
}

bool MapEditorScene::point_in(const SDL_FRect& r, float x, float y) const {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

Vector2 MapEditorScene::viewport_to_world(float mx, float my) const {
    return camera_.screen_to_world({ mx - viewport_x_, my - viewport_y_ });
}

Vector2 MapEditorScene::world_to_viewport(const Vector2& w) const {
    Vector2 s = camera_.world_to_screen(w);
    return { s.x + viewport_x_, s.y + viewport_y_ };
}

SDL_FRect MapEditorScene::scrolled(const SDL_FRect& r) const {
    return { r.x, r.y - tool_scroll_, r.w, r.h };
}

void MapEditorScene::zoom_at(float mx, float my, float wheel) {
    float old = camera_.get_scale();
    float factor = (wheel > 0) ? 1.15f : (1.0f / 1.15f);
    float ns = std::clamp(old * factor, 0.08f, 4.0f);
    if (ns == old) return;
    Vector2 world = viewport_to_world(mx, my);
    camera_.set_scale(ns);
    Vector2 local{ mx - viewport_x_, my - viewport_y_ };
    camera_.set_position({ world.x - local.x / ns, world.y - local.y / ns });
}

void MapEditorScene::append_text(const char* txt) {
    if (!txt) return;
    for (const char* p = txt; *p; ++p) {
        char c = *p;
        if (filename_.size() >= 24) break;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (ok) filename_.push_back(c);
    }
}

void MapEditorScene::append_hp_text(const char* txt) {
    if (!txt) return;
    for (const char* p = txt; *p; ++p) {
        char c = *p;
        if (hp_input_.size() >= 9) break;
        if (c >= '0' && c <= '9') hp_input_.push_back(c);
    }
}

void MapEditorScene::set_status(const std::string& msg) {
    status_msg_ = msg;
    status_timer_ = 4.0f;
}

void MapEditorScene::select_at(float mx, float my) {
    Vector2 world = viewport_to_world(mx, my);
    uint64_t found = 0;
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj || !obj->check_valid()) continue;
        if (!obj->get_component<Renderable>()) continue;
        if (obj->get_collision_box().contains_point(world)) found = id;
    }
    selected_id_ = found;
}

bool MapEditorScene::cell_occupied(int cx, int cy) const {
    int cs = editor_map_.get_cell_size();
    CollisionBox cell{ { (float)(cx * cs), (float)(cy * cs) }, (float)cs, (float)cs };
    return factory_.check_overlap(cell) != nullptr;
}

void MapEditorScene::paint_terrain_at(float mx, float my) {
    Vector2 world = viewport_to_world(mx, my);
    int cs = editor_map_.get_cell_size();
    int gx = (int)std::floor(world.x / cs);
    int gy = (int)std::floor(world.y / cs);
    TerrainType t = (cur_tool_ == Tool::Water) ? TerrainType::Water : TerrainType::Mud;
    bool is_water = (t == TerrainType::Water);
    int half = (brush_size_ - 1) / 2;   // 左上角偏移，使 brush_size_×brush_size_ 方块大致以鼠标格为中心
    bool changed = false;
    for (int dy = 0; dy < brush_size_; ++dy) {
        for (int dx = 0; dx < brush_size_; ++dx) {
            int cx = gx - half + dx;
            int cy = gy - half + dy;
            if (cx < 0 || cy < 0 || cx >= editor_map_.get_width() || cy >= editor_map_.get_height()) continue;
            if (editor_map_.get_grid()[cy][cx] == t) continue;
            // 河流不能放到实体（资源/建筑/单位）脚下：被遮挡的格子跳过，其余格子正常生效
            if (is_water && cell_occupied(cx, cy)) continue;
            editor_map_.set_grid_by_pos(cx, cy, t);
            changed = true;
        }
    }
    if (changed) {
        terrain_dirty_ = true;
        static_dirty_ = true;
    }
}

CollisionBox MapEditorScene::entity_placement_box(float mx, float my) const {
    Vector2 world = viewport_to_world(mx, my);
    int cs = editor_map_.get_cell_size();
    int gx = (int)std::floor(world.x / cs);
    int gy = (int)std::floor(world.y / cs);

    if (is_unit_tool(cur_tool_)) {
        // 单位 32×32：以鼠标所在格中心为锚点，让指针落在单位盒中心
        float cx = (gx + 0.5f) * cs;
        float cy = (gy + 0.5f) * cs;
        return CollisionBox{ { cx - 16.0f, cy - 16.0f }, 32.0f, 32.0f };
    }

    int n = 0;
    if (is_resource_tool(cur_tool_)) n = resource_size_cells(tool_to_resource(cur_tool_));
    else if (is_building_tool(cur_tool_)) n = building_size_cells(tool_to_building(cur_tool_));

    // 资源/建筑 n×n 格：以鼠标所在格为中心，左上角格 = gx - n/2（向下取整，偶数尺寸仍视觉居中）
    int ox = gx - n / 2;
    int oy = gy - n / 2;
    return CollisionBox{ { (float)(ox * cs), (float)(oy * cs) }, (float)(n * cs), (float)(n * cs) };
}

void MapEditorScene::place_at(float mx, float my) {
    if (is_terrain_tool(cur_tool_)) { paint_terrain_at(mx, my); return; }
    CollisionBox box = entity_placement_box(mx, my);
    int cs = editor_map_.get_cell_size();
    int gx = (int)std::floor(box.position.x / cs);
    int gy = (int)std::floor(box.position.y / cs);

    GameObject* obj = nullptr;
    if (is_resource_tool(cur_tool_)) {
        factory_.set_player_id(0);
        obj = factory_.create_resource_by_type(tool_to_resource(cur_tool_), gx, gy, false);
    }
    else if (is_building_tool(cur_tool_)) {
        factory_.set_player_id(place_player_);
        obj = factory_.create_building_by_type(tool_to_building(cur_tool_), gx, gy, false);
    }
    else if (is_unit_tool(cur_tool_)) {
        factory_.set_player_id(place_player_);
        obj = factory_.create_unit_by_type(tool_to_unit(cur_tool_), box, false);
    }
    if (obj) entities_.push_back(make_record(obj));
}

bool MapEditorScene::placement_valid(float mx, float my) const {
    if (cur_tool_ == Tool::Select) return false;
    Vector2 world = viewport_to_world(mx, my);
    int cs = editor_map_.get_cell_size();
    int gx = (int)std::floor(world.x / cs);
    int gy = (int)std::floor(world.y / cs);

    // 地形涂刷：只需笔刷范围全部在界内
    if (is_terrain_tool(cur_tool_)) {
        int half = (brush_size_ - 1) / 2;
        return (gx - half >= 0) && (gy - half >= 0)
            && (gx - half + brush_size_ <= editor_map_.get_width())
            && (gy - half + brush_size_ <= editor_map_.get_height());
    }

    CollisionBox box = entity_placement_box(mx, my);
    if (!editor_map_.is_box_passable(box)) return false;
    if (factory_.check_overlap(box)) return false;
    return true;
}

void MapEditorScene::delete_selected() {
    if (!selected_id_) return;
    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(selected_id_);
    if (obj) WorldEntityMgr::instance()->destroy_object(obj);
    remove_record(selected_id_);
    selected_id_ = 0;
    WorldEntityMgr::instance()->on_update();   // 清理失效实体 + 重建动态障碍
}

void MapEditorScene::apply_health_input() {
    GameObject* obj = WorldEntityMgr::instance()->get_object_by_id(selected_id_);
    if (!obj) return;
    if (!obj->get_component<Harvestable>()) return;
    auto* hp = obj->get_component<Health>();
    if (!hp) return;
    int v = 0;
    for (char c : hp_input_) {
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            if (v > 999999) { v = 999999; break; }
        }
    }
    v = std::clamp(v, 1, 999999);
    hp->current_health = v;
    hp->max_health = v;
    // 同步记录
    for (auto& r : entities_) if (r.obj_id == selected_id_) { r.health = v; break; }
}

// ==================== 操作 ====================

void MapEditorScene::do_save() {
    cleanup_invalid();
    snapshot_entities();   // 以世界实体为准生成记录
    try {
        std::filesystem::path dir = "maps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string path = (dir / (filename_ + ".srmap")).string();
        if (save_map(path, editor_map_, entities_, player_count_)) {
            set_status(u8"已保存: maps/" + filename_ + u8".srmap");
            refresh_map_files();
        }
        else
            set_status(u8"保存失败");
    }
    catch (...) { set_status(u8"保存失败"); }
}

void MapEditorScene::do_load() {
    std::string path = (std::filesystem::path("maps") / (filename_ + ".srmap")).string();
    std::vector<EntityRecord> loaded;
    int loaded_pc = 2;
    if (!load_map(path, editor_map_, loaded, loaded_pc)) {
        set_status(u8"载入失败: " + filename_ + u8".srmap 不存在或格式不符");
        return;
    }
    // 替换内容
    editor_map_.clear_dynamic_obstacle_field();
    WorldEntityMgr::instance()->reset_world();
    WorldEntityMgr::instance()->init_world(&editor_map_);
    factory_.init(&editor_map_);
    entities_ = std::move(loaded);
    selected_id_ = 0;
    player_count_ = loaded_pc;
    if (place_player_ > player_count_) place_player_ = 0;
    layout();   // 按载入的玩家数量重建归属按钮
    terrain_dirty_ = true;
    static_dirty_ = false;
    spawn_from_records();
    set_status(u8"已载入: " + filename_ + u8".srmap（实体 " + std::to_string(entities_.size()) + u8" 个，玩家 " + std::to_string(player_count_) + u8"）");
}

void MapEditorScene::do_clear() {
    reset_to_empty();
    set_status(u8"已清空地图");
}

void MapEditorScene::do_delete_map() {
    if (map_sel_ < 0 || map_sel_ >= (int)map_files_.size()) {
        confirm_delete_ = false;
        set_status(u8"请先在下方列表中选择要删除的地图");
        return;
    }
    std::string name = map_files_[map_sel_];
    if (!confirm_delete_ || confirm_delete_name_ != name) {
        confirm_delete_ = true;
        confirm_delete_name_ = name;
        set_status(u8"再次点击「删除地图」确认删除: " + name);
        return;
    }
    // 二次确认通过，执行删除
    confirm_delete_ = false;
    confirm_delete_name_.clear();
    std::error_code ec;
    std::filesystem::path path = std::filesystem::path("maps") / (name + ".srmap");
    std::filesystem::remove(path, ec);
    if (ec) {
        set_status(u8"删除失败");
        return;
    }
    refresh_map_files();
    if (filename_ == name)
        filename_ = next_default_name();
    set_status(u8"已删除地图: " + name);
}

void MapEditorScene::do_back() {
    SceneMgr::instance()->on_switch(SceneMgr::SceneType::Menu);
}

void MapEditorScene::apply_size(MapSize s) {
    if (s == cur_size_) return;
    cur_size_ = s;

    int w, h;
    switch (s) {
    case MapSize::Small:  w = 150; h = 100; break;
    case MapSize::Medium: w = 300; h = 200; break;
    case MapSize::Large:  w = 450; h = 300; break;
    default: w = 300; h = 200; break;
    }

    editor_map_.resize(w, h);
    WorldEntityMgr::instance()->reset_world();
    WorldEntityMgr::instance()->init_world(&editor_map_);
    factory_.init(&editor_map_);
    entities_.clear();
    selected_id_ = 0;
    terrain_dirty_ = true;
    static_dirty_ = false;

    // 重新适配相机，让新尺寸整图铺满画布
    float vw = LOGICAL_W - panel_w_;
    float vh = LOGICAL_H - topbar_h_;
    camera_.init(vw, vh,
        (float)(w * editor_map_.get_cell_size()),
        (float)(h * editor_map_.get_cell_size()));
    float fit = std::min(vw / camera_.get_map_w(), vh / camera_.get_map_h()) * 0.98f;
    camera_.set_scale(fit);
    camera_.set_position({ 0.0f, 0.0f });

    set_status(u8"已切换地图尺寸: " + std::to_string(w) + u8"x" + std::to_string(h));
}

void MapEditorScene::apply_player_count(int n) {
    if (player_count_ == n) return;
    player_count_ = n;
    if (place_player_ > n) place_player_ = n;   // 当前选中玩家超出范围时收缩
    layout();   // 重建归属按钮网格
    set_status(u8"玩家数量: " + std::to_string(n));
}

void MapEditorScene::refresh_map_files() {
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
    // 若当前文件名在列表中，则高亮对应项
    for (size_t i = 0; i < map_files_.size(); ++i)
        if (map_files_[i] == filename_) { map_sel_ = (int)i; break; }
}

std::string MapEditorScene::next_default_name() const {
    // 从已有地图文件名里提取所有连续数字段，取最大值作为"已用最大编号"
    int max_num = 0;
    for (const auto& name : map_files_) {
        int num = 0;
        for (char c : name) {
            if (c >= '0' && c <= '9') {
                num = num * 10 + (c - '0');
            }
            else if (num > 0) {
                max_num = std::max(max_num, num);
                num = 0;
            }
        }
        if (num > 0) max_num = std::max(max_num, num);
    }
    int next = max_num + 1;
    std::string ns = std::to_string(next);
    if (ns.size() < 2) ns = "0" + ns;   // 与默认 map01 保持一致，至少两位
    return "map" + ns;
}

void MapEditorScene::handle_key(int key) {
    if (hp_focused_) {
        if (key == SDLK_BACKSPACE && !hp_input_.empty()) {
            hp_input_.pop_back();
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            apply_health_input();
            hp_focused_ = false;
            return;
        }
        if (key == SDLK_ESCAPE) {
            hp_focused_ = false;   // 取消，不应用
            return;
        }
        return;   // 血量输入框聚焦时，其余按键不触发编辑器快捷键
    }

    if (filename_focused_) {
        if (key == SDLK_BACKSPACE && !filename_.empty()) {
            filename_.pop_back();
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            filename_focused_ = false;
            return;
        }
        return;   // 输入框聚焦时，其余按键不触发编辑器快捷键
    }

    switch (key) {
    case SDLK_ESCAPE:
        do_back();
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        delete_selected();
        break;
    default:
        break;
    }
}

void MapEditorScene::on_input(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        float x = event.motion.x, y = event.motion.y;
        mouse_x_ = x;
        mouse_y_ = y;
        if (panning_) {
            float dx = (x - last_pan_x_) / camera_.get_scale();
            float dy = (y - last_pan_y_) / camera_.get_scale();
            Vector2 p = camera_.get_position();
            camera_.set_position({ p.x - dx, p.y - dy });
            last_pan_x_ = x;
            last_pan_y_ = y;
        }
        else if (painting_) {
            if (inside_viewport(x, y)) paint_terrain_at(x, y);
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float x = event.button.x, y = event.button.y;
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (handle_ui_click(x, y)) return;
            if (inside_viewport(x, y)) {
                if (is_terrain_tool(cur_tool_)) {
                    if (place_mode_ == PlaceMode::Single) {
                        paint_terrain_at(x, y);
                        cur_tool_ = Tool::Select;   // 单个放置：放置后回到选择
                    }
                    else {
                        painting_ = true;
                        paint_terrain_at(x, y);
                    }
                }
                else if (cur_tool_ == Tool::Select) select_at(x, y);
                else {
                    place_at(x, y);
                    if (place_mode_ == PlaceMode::Single) cur_tool_ = Tool::Select;
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT) {
            // 右键：取消选中（同时退出血量输入）
            selected_id_ = 0;
            hp_focused_ = false;
            // 若处于放置工具（绿框预览）阶段，一并取消工具选择回到「选择」工具
            if (cur_tool_ != Tool::Select) {
                cur_tool_ = Tool::Select;
                painting_ = false;
            }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE) {
            if (inside_viewport(x, y)) { panning_ = true; last_pan_x_ = x; last_pan_y_ = y; }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (painting_) { painting_ = false; cleanup_invalid(); }
        }
        else if (event.button.button == SDL_BUTTON_MIDDLE) {
            panning_ = false;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        float x = event.wheel.mouse_x, y = event.wheel.mouse_y;
        if (inside_viewport(x, y)) zoom_at(x, y, event.wheel.y);
        else if (inside_panel(x, y)) {
            if (y >= tools_top_ && y <= tools_bottom_) {
                float max_scroll = std::max(0.0f, tools_content_h_ - (tools_bottom_ - tools_top_));
                tool_scroll_ = std::clamp(tool_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
            }
            else if (y >= map_list_top_ && y <= map_list_bottom_) {
                float content = (float)map_files_.size() * 26.0f;
                float max_scroll = std::max(0.0f, content - (map_list_bottom_ - map_list_top_));
                map_scroll_ = std::clamp(map_scroll_ - event.wheel.y * 40.0f, 0.0f, max_scroll);
            }
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        handle_key((int)event.key.key);
    }
    else if (event.type == SDL_EVENT_TEXT_INPUT) {
        if (hp_focused_) append_hp_text(event.text.text);
        else if (filename_focused_) append_text(event.text.text);
    }
}

bool MapEditorScene::handle_ui_click(float x, float y) {
    // 点击血量输入框以外的任何位置时，退出血量编辑
    if (hp_focused_ && !point_in(hp_input_rect_, x, y)) hp_focused_ = false;

    // 顶部工具条
    if (point_in(filename_rect_, x, y)) { filename_focused_ = true; return true; }
    if (point_in(btn_save_.rect, x, y))  { filename_focused_ = false; do_save();  return true; }
    if (point_in(btn_load_.rect, x, y))  { filename_focused_ = false; do_load();  return true; }
    if (point_in(btn_clear_.rect, x, y)) { filename_focused_ = false; do_clear(); return true; }
    if (point_in(btn_back_.rect, x, y))  { filename_focused_ = false; do_back();  return true; }

    // 地图尺寸
    if (point_in(size_small_, x, y)) { filename_focused_ = false; apply_size(MapSize::Small);  return true; }
    if (point_in(size_med_, x, y))   { filename_focused_ = false; apply_size(MapSize::Medium); return true; }
    if (point_in(size_large_, x, y)) { filename_focused_ = false; apply_size(MapSize::Large);  return true; }

    // 放置模式
    if (point_in(mode_single_, x, y)) {
        filename_focused_ = false; place_mode_ = PlaceMode::Single;
        set_status(u8"单个放置：放置后自动回到选择"); return true;
    }
    if (point_in(mode_batch_, x, y)) {
        filename_focused_ = false; place_mode_ = PlaceMode::Batch;
        set_status(u8"批量放置：可连续放置"); return true;
    }

    // 笔刷大小（地形工具）
    if (point_in(brush_1_, x, y)) { brush_size_ = 1; return true; }
    if (point_in(brush_5_, x, y)) { brush_size_ = 5; return true; }
    if (point_in(brush_10_, x, y)) { brush_size_ = 10; return true; }

    // 玩家数量
    if (point_in(pc_2_, x, y)) { apply_player_count(2); return true; }
    if (point_in(pc_4_, x, y)) { apply_player_count(4); return true; }
    if (point_in(pc_8_, x, y)) { apply_player_count(8); return true; }

    // 归属玩家（index 0=中立, 1..N=玩家）
    for (size_t i = 0; i < owner_btns_.size(); ++i) {
        if (point_in(owner_btns_[i], x, y)) {
            place_player_ = (int)i;
            return true;
        }
    }

    // 选中信息面板按钮
    if (selected_id_) {
        GameObject* sel = WorldEntityMgr::instance()->get_object_by_id(selected_id_);
        if (sel && sel->get_component<Harvestable>()) {
            if (point_in(hp_input_rect_, x, y)) {
                hp_focused_ = true;
                filename_focused_ = false;
                auto* h = sel->get_component<Health>();
                hp_input_ = h ? std::to_string(h->current_health) : "";
                return true;
            }
        }
        if (point_in(del_btn_, x, y)) { delete_selected(); return true; }
    }

    // 地图文件列表（选择保存/载入目标 / 删除）
    if (point_in(btn_delete_map_.rect, x, y)) { filename_focused_ = false; do_delete_map(); return true; }

    if (y >= map_list_top_ && y <= map_list_bottom_) {
        float content_y = y + map_scroll_;
        const float entry_h = 26.0f;
        for (size_t i = 0; i < map_files_.size(); ++i) {
            SDL_FRect r{ 8.0f, map_list_top_ + (float)i * entry_h, panel_w_ - 16.0f, entry_h - 2.0f };
            if (point_in(r, x, content_y)) {
                filename_ = map_files_[i];
                map_sel_ = (int)i;
                filename_focused_ = false;
                confirm_delete_ = false;   // 切换选中后，取消之前的删除确认
                set_status(u8"已选择地图: " + filename_ + u8"（点“载入”打开 / 点“保存”覆盖）");
                return true;
            }
        }
        return false;   // 列表空白处不穿透
    }

    // 工具按钮（仅在工具列表可视区域内响应）
    if (y >= tools_top_ && y <= tools_bottom_) {
        float content_y = y + tool_scroll_;
        for (auto& s : sections_) {
            for (auto& e : s.entries) {
                if (point_in(e.rect, x, content_y)) {
                    cur_tool_ = e.tool;
                    return true;
                }
            }
        }
    }
    return false;
}

// ==================== 渲染 ====================

void MapEditorScene::bake_terrain() {
    if (!renderer) return;
    int cs = editor_map_.get_cell_size();
    int w = editor_map_.get_width();
    int h = editor_map_.get_height();
    if (!terrain_bake_.create(w * cs, h * cs, renderer)) return;

    terrain_bake_.begin(renderer);
    const auto& grid = editor_map_.get_grid();
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
    terrain_bake_.end(renderer);
    terrain_dirty_ = false;
}

void MapEditorScene::render_terrain() {
    if (terrain_dirty_) bake_terrain();
    uint32_t id = terrain_bake_.get_texture_id();
    SDL_Texture* tex = TextureCache::instance()->get_texture_by_id(id);
    if (!tex) return;
    float mw = camera_.get_map_w() * camera_.get_scale();
    float mh = camera_.get_map_h() * camera_.get_scale();
    Vector2 tl = world_to_viewport({ 0.0f, 0.0f });
    SDL_FRect dst{ tl.x, tl.y, mw, mh };
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
}

void MapEditorScene::render_grid_overlay() {
    if (camera_.get_scale() < 0.7f) return;
    int cs = editor_map_.get_cell_size();
    Vector2 topLeft = viewport_to_world(panel_w_, topbar_h_);
    Vector2 botRight = viewport_to_world(LOGICAL_W, LOGICAL_H);
    int x0 = std::max(0, (int)std::floor(topLeft.x / cs));
    int x1 = std::min(editor_map_.get_width(), (int)std::ceil(botRight.x / cs) + 1);
    int y0 = std::max(0, (int)std::floor(topLeft.y / cs));
    int y1 = std::min(editor_map_.get_height(), (int)std::ceil(botRight.y / cs) + 1);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 36);
    for (int x = x0; x <= x1; ++x) {
        Vector2 a = world_to_viewport({ (float)(x * cs), topLeft.y });
        Vector2 b = world_to_viewport({ (float)(x * cs), botRight.y });
        SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
    }
    for (int y = y0; y <= y1; ++y) {
        Vector2 a = world_to_viewport({ topLeft.x, (float)(y * cs) });
        Vector2 b = world_to_viewport({ botRight.x, (float)(y * cs) });
        SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
    }
}

void MapEditorScene::render_entities() {
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool) {
        if (!obj || !obj->check_valid()) continue;
        auto* renderable = obj->get_component<Renderable>();
        if (!renderable) continue;
        const auto& box = obj->get_collision_box();
        Vector2 tl = world_to_viewport(box.position);
        float w = box.width * camera_.get_scale();
        float h = box.height * camera_.get_scale();
        SDL_FRect dst{ tl.x, tl.y, w, h };

        SDL_Texture* tex = renderable->texture_id
            ? TextureCache::instance()->get_texture_by_id(renderable->texture_id) : nullptr;
        if (tex) {
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
        else {
            SDL_Color c = to_sdl_color(renderable->color);
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_RenderFillRect(renderer, &dst);
        }

        // 单位保留外框：按归属玩家着色（0=中立灰，1..N=专属色），让 32x32 脚印可见
        if (obj->get_component<UnitType>()) {
            auto* own = obj->get_component<Ownership>();
            int pid = own ? own->player_id : 0;
            SDL_Color bc = player_frame_color(pid);
            SDL_SetRenderDrawColor(renderer, bc.r, bc.g, bc.b, 255);
            SDL_RenderRect(renderer, &dst);
        }

        if (id == selected_id_) {
            SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
            SDL_RenderRect(renderer, &dst);
        }
    }
}

void MapEditorScene::draw_button(const SDL_FRect& r, const std::string& label, bool active, bool hover) {
    SDL_Color bg = active ? SDL_Color{ 62, 122, 62, 255 }
        : (hover ? SDL_Color{ 52, 70, 120, 255 } : SDL_Color{ 36, 48, 84, 255 });
    SDL_Color bd = (active || hover) ? SDL_Color{ 255, 215, 0, 255 } : SDL_Color{ 90, 110, 160, 255 };
    ui_fill_rect(renderer, r, bg, bd, active ? 3 : 2);
    ui_draw_text(renderer, label, 18, { 255, 255, 255, 255 },
        r.x + r.w * 0.5f, r.y + r.h * 0.5f, true);
}

void MapEditorScene::render_ui() {
    // 顶部工具条背景
    ui_fill_rect(renderer, SDL_FRect{ 0, 0, LOGICAL_W, topbar_h_ }, { 24, 30, 48, 255 });
    ui_fill_rect(renderer, SDL_FRect{ 0, topbar_h_ - 3, LOGICAL_W, 3 }, { 255, 215, 0, 255 });
    ui_draw_text(renderer, u8"地图编辑", 26, { 255, 215, 0, 255 }, 64.0f, topbar_h_ * 0.5f, true);

    ui_draw_text(renderer, u8"地图:", 20, { 200, 210, 230, 255 }, 176.0f, topbar_h_ * 0.5f, true);

    // 文件名输入框
    {
        SDL_Color bg = filename_focused_ ? SDL_Color{ 46, 60, 96, 255 } : SDL_Color{ 34, 44, 74, 255 };
        ui_fill_rect(renderer, filename_rect_, bg, { 120, 140, 190, 255 }, 2);
        std::string shown = filename_ + (filename_focused_ ? "_" : "");
        ui_draw_text_vcenter(renderer, shown, 18, { 255, 255, 255, 255 },
            filename_rect_.x + 8.0f, filename_rect_.y + filename_rect_.h * 0.5f);
    }

    draw_button(btn_save_.rect, btn_save_.label, false, false);
    draw_button(btn_load_.rect, btn_load_.label, false, false);
    draw_button(btn_clear_.rect, btn_clear_.label, false, false);
    draw_button(btn_back_.rect, btn_back_.label, false, false);

    // 状态消息
    if (!status_msg_.empty())
        ui_draw_text(renderer, status_msg_, 18, { 150, 220, 150, 255 }, 780.0f, topbar_h_ * 0.5f, false);

    // 左侧面板背景
    ui_fill_rect(renderer, SDL_FRect{ 0, topbar_h_, panel_w_, LOGICAL_H - topbar_h_ }, { 18, 24, 40, 255 });

    // 地图尺寸
    ui_draw_text(renderer, u8"地图尺寸", 16, { 200, 210, 230, 255 }, 8.0f, size_label_y_, false);
    {
        bool small = (cur_size_ == MapSize::Small), med = (cur_size_ == MapSize::Medium), large = (cur_size_ == MapSize::Large);
        draw_button(size_small_, u8"小", small, false);
        draw_button(size_med_,   u8"中", med,   false);
        draw_button(size_large_, u8"大", large, false);
    }

    // 放置模式
    ui_draw_text(renderer, u8"放置模式", 16, { 200, 210, 230, 255 }, 8.0f, mode_label_y_, false);
    {
        bool single = (place_mode_ == PlaceMode::Single), batch = (place_mode_ == PlaceMode::Batch);
        draw_button(mode_single_, u8"单个", single, false);
        draw_button(mode_batch_,  u8"批量", batch,  false);
    }

    // 笔刷大小
    ui_draw_text(renderer, u8"笔刷大小", 16, { 200, 210, 230, 255 }, 8.0f, brush_label_y_, false);
    {
        draw_button(brush_1_,  u8"1x1",   (brush_size_ == 1),  false);
        draw_button(brush_5_,  u8"5x5",   (brush_size_ == 5),  false);
        draw_button(brush_10_, u8"10x10", (brush_size_ == 10), false);
    }

    // 工具列表（带滚动）
    {
        SDL_Rect clip{ 0, (int)tools_top_, (int)panel_w_, (int)(tools_bottom_ - tools_top_) };
        SDL_SetRenderClipRect(renderer, &clip);
        for (auto& s : sections_) {
            ui_draw_text(renderer, s.title, 16, { 255, 215, 0, 255 }, 8.0f, s.title_y - tool_scroll_, false);
            for (auto& e : s.entries) {
                SDL_FRect r = scrolled(e.rect);
                draw_button(r, e.label, (cur_tool_ == e.tool), false);
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    // 玩家数量
    ui_draw_text(renderer, u8"玩家数量", 16, { 200, 210, 230, 255 }, 8.0f, pc_label_y_, false);
    {
        draw_button(pc_2_, u8"2", (player_count_ == 2), false);
        draw_button(pc_4_, u8"4", (player_count_ == 4), false);
        draw_button(pc_8_, u8"8", (player_count_ == 8), false);
    }

    // 归属玩家（0=中立, 1..N=玩家）
    ui_draw_text(renderer, u8"归属玩家", 16, { 200, 210, 230, 255 }, 8.0f, owner_label_y_, false);
    for (size_t i = 0; i < owner_btns_.size(); ++i) {
        std::string lbl = (i == 0) ? u8"中立" : std::to_string(i);
        draw_button(owner_btns_[i], lbl, ((int)i == place_player_), false);
    }

    // 地图文件列表（选择保存/载入目标）+ 删除
    ui_draw_text(renderer, u8"地图文件", 16, { 200, 210, 230, 255 }, 8.0f, map_label_y_, false);
    draw_button(btn_delete_map_.rect, btn_delete_map_.label, confirm_delete_, false);
    {
        SDL_Rect clip{ 0, (int)map_list_top_, (int)panel_w_, (int)(map_list_bottom_ - map_list_top_) };
        SDL_SetRenderClipRect(renderer, &clip);
        const float entry_h = 26.0f;
        if (map_files_.empty()) {
            ui_draw_text(renderer, u8"（暂无地图）", 15, { 150, 160, 180, 255 }, 8.0f, map_list_top_ + 6.0f, false);
        }
        for (size_t i = 0; i < map_files_.size(); ++i) {
            SDL_FRect r{ 8.0f, map_list_top_ + (float)i * entry_h - map_scroll_, panel_w_ - 16.0f, entry_h - 2.0f };
            draw_button(r, map_files_[i], ((int)i == map_sel_), false);
        }
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    // 选中信息面板
    ui_fill_rect(renderer, sel_info_rect_, { 30, 40, 66, 255 }, { 90, 110, 160, 255 }, 2);
    GameObject* sel = selected_id_ ? WorldEntityMgr::instance()->get_object_by_id(selected_id_) : nullptr;
    if (!sel) {
        ui_draw_text(renderer, u8"未选中", 18, { 160, 170, 190, 255 },
            sel_info_rect_.x + sel_info_rect_.w * 0.5f, sel_info_rect_.y + 40.0f, true);
        return;
    }

    // 类型名
    std::string type_name = u8"对象";
    if (auto* hb = sel->get_component<Harvestable>()) {
        type_name = TextureCache::instance()->get_resource_name(hb->entity_type);
        if (type_name == u8"金矿") type_name = (hb->entity_type == ResourceEntityType::LGold) ? u8"大金矿" : u8"小金矿";
    }
    else if (auto* bt = sel->get_component<BuildingType>()) type_name = TextureCache::instance()->get_building_name(bt->type);
    else if (auto* ut = sel->get_component<UnitType>()) type_name = TextureCache::instance()->get_unit_name(ut->type);

    ui_draw_text(renderer, type_name, 20, { 255, 255, 255, 255 },
        sel_info_rect_.x + sel_info_rect_.w * 0.5f, sel_info_rect_.y + 22.0f, true);

    auto* hp = sel->get_component<Health>();
    if (sel->get_component<Harvestable>() && hp) {
        SDL_Color hbg = hp_focused_ ? SDL_Color{ 46, 60, 96, 255 } : SDL_Color{ 34, 44, 74, 255 };
        ui_fill_rect(renderer, hp_input_rect_, hbg, { 120, 140, 190, 255 }, 2);
        std::string htext = u8"血量: ";
        if (hp_focused_) htext += hp_input_ + "_";
        else             htext += std::to_string(hp->current_health);
        ui_draw_text_vcenter(renderer, htext, 16, { 255, 255, 255, 255 },
            hp_input_rect_.x + 8.0f, hp_input_rect_.y + hp_input_rect_.h * 0.5f);
    }

    draw_button(del_btn_, u8"删除", false, false);
}

void MapEditorScene::on_render() {
    SDL_SetRenderDrawColor(renderer, 10, 13, 22, 255);
    SDL_RenderClear(renderer);

    // 地图画布（裁剪到视口）
    SDL_Rect vp{ (int)panel_w_, (int)topbar_h_, (int)(LOGICAL_W - panel_w_), (int)(LOGICAL_H - topbar_h_) };
    SDL_SetRenderClipRect(renderer, &vp);
    render_terrain();
    render_grid_overlay();
    render_entities();

    // 放置预览
    if (cur_tool_ != Tool::Select && inside_viewport(mouse_x_, mouse_y_)) {
        Vector2 world = viewport_to_world(mouse_x_, mouse_y_);
        int cs = editor_map_.get_cell_size();
        int gx = (int)std::floor(world.x / cs);
        int gy = (int)std::floor(world.y / cs);
        float pw = 0, ph = 0;
        float ox = (float)(gx * cs), oy = (float)(gy * cs);
        if (is_terrain_tool(cur_tool_)) {
            int half = (brush_size_ - 1) / 2;
            pw = ph = (float)(cs * brush_size_);
            ox = (float)((gx - half) * cs);
            oy = (float)((gy - half) * cs);
        }
        else {
            CollisionBox box = entity_placement_box(mouse_x_, mouse_y_);
            ox = box.position.x;
            oy = box.position.y;
            pw = box.width;
            ph = box.height;
        }
        if (pw > 0) {
            Vector2 tl = world_to_viewport({ ox, oy });
            SDL_FRect ghost{ tl.x, tl.y, pw * camera_.get_scale(), ph * camera_.get_scale() };
            bool valid = placement_valid(mouse_x_, mouse_y_);
            if (valid) {
                SDL_SetRenderDrawColor(renderer, 120, 220, 120, 90);
                SDL_RenderFillRect(renderer, &ghost);
                SDL_SetRenderDrawColor(renderer, 120, 220, 120, 255);
            }
            else {
                SDL_SetRenderDrawColor(renderer, 220, 80, 80, 90);
                SDL_RenderFillRect(renderer, &ghost);
                SDL_SetRenderDrawColor(renderer, 220, 80, 80, 255);
            }
            SDL_RenderRect(renderer, &ghost);

            // 河流预览：标记被实体（资源/建筑/单位）遮挡、涂不进去的格子
            if (cur_tool_ == Tool::Water) {
                int half = (brush_size_ - 1) / 2;
                for (int dy = 0; dy < brush_size_; ++dy) {
                    for (int dx = 0; dx < brush_size_; ++dx) {
                        int cx = gx - half + dx;
                        int cy = gy - half + dy;
                        if (cx < 0 || cy < 0 || cx >= editor_map_.get_width() || cy >= editor_map_.get_height()) continue;
                        if (editor_map_.get_grid()[cy][cx] == TerrainType::Water) continue;   // 已是水域
                        if (!cell_occupied(cx, cy)) continue;
                        Vector2 occ_tl = world_to_viewport({ (float)(cx * cs), (float)(cy * cs) });
                        SDL_FRect occ{ occ_tl.x, occ_tl.y, (float)cs * camera_.get_scale(), (float)cs * camera_.get_scale() };
                        SDL_SetRenderDrawColor(renderer, 220, 80, 80, 170);
                        SDL_RenderFillRect(renderer, &occ);
                    }
                }
            }
        }
    }
    SDL_SetRenderClipRect(renderer, nullptr);

    render_ui();
}
