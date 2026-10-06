#ifndef _MAP_EDITOR_SCENE_H_
#define _MAP_EDITOR_SCENE_H_

#include "scene.h"
#include "game_map.h"
#include "factories.h"
#include "camera.h"
#include "render_texture.h"
#include "map_io.h"

#include <vector>
#include <string>

class MapEditorScene : public Scene {
public:
    MapEditorScene() = default;
    ~MapEditorScene() = default;

    void on_input(const SDL_Event& event) override;
    void on_update(float delta) override;
    void on_render() override;
    void on_enter() override;
    void on_exit() override;

private:
    enum class Tool : int {
        Select = 0,
        // 地形
        Land, Water,
        // 资源
        Wood, SGold, LGold, Stone, Berries,
        // 兵种
        Villager, Archer, Crossbowman,
        // 建筑
        TownCenter, ArcheryRange,
        // 操作
        Eraser
    };

    enum class MapSize : int { Small, Medium, Large };

    enum class PlaceMode : int { Single, Batch };

    struct ToolEntry { Tool tool; std::string label; SDL_FRect rect{}; };
    struct ToolSection { std::string title; float title_y = 0.0f; std::vector<ToolEntry> entries; };
    struct Btn { SDL_FRect rect{}; std::string label; };

    // ---- 状态 ----
    GameMap editor_map_;               // 编辑器地图（地形权威数据）
    ObjectFactory factory_;
    Camera camera_;
    RenderTexture terrain_bake_;
    bool terrain_dirty_ = true;        // 地形纹理需要重烘焙
    bool static_dirty_ = false;        // 静态障碍场需要重建
    bool initialized_ = false;

    std::vector<ToolSection> sections_;
    Tool cur_tool_ = Tool::Select;
    int place_player_ = 1;             // 当前归属玩家 id：0=中立, 1..N=玩家
    int player_count_ = 2;             // 玩家数量（2/4/8）

    MapSize cur_size_ = MapSize::Medium;   // 地图尺寸
    PlaceMode place_mode_ = PlaceMode::Single;  // 放置模式：单个/批量（默认单个）

    std::vector<std::string> map_files_;   // maps/ 目录下已有的 .srmap 文件名（不含扩展名）
    int map_sel_ = -1;                     // 列表当前选中项
    float map_scroll_ = 0.0f;              // 地图列表滚动偏移

    std::vector<EntityRecord> entities_;   // 编辑器内容（实体权威数据）
    uint64_t selected_id_ = 0;

    std::string filename_ = "map01";
    bool filename_focused_ = false;
    bool hp_focused_ = false;          // 血量输入框聚焦（键盘输入）
    std::string hp_input_;             // 血量输入缓冲
    std::string status_msg_;
    float status_timer_ = 0.0f;

    // ---- 交互状态 ----
    bool panning_ = false;
    float last_pan_x_ = 0.0f, last_pan_y_ = 0.0f;
    bool painting_ = false;                    // 地形涂刷按住中
    bool placing_ = false;                     // 资源批量放置按住中（长按连续放置）
    bool erasing_ = false;                     // 橡皮擦按住中（拖动连续擦除）
    int last_place_gx_ = -1, last_place_gy_ = -1;   // 上次连续放置的格子原点（避免同格重复放置）
    float mouse_x_ = 0.0f, mouse_y_ = 0.0f;   // 最近一次鼠标位置（逻辑坐标）

    // ---- UI 布局常量 ----
    float panel_w_ = 200.0f;
    float topbar_h_ = 56.0f;
    float viewport_x_ = 200.0f;
    float viewport_y_ = 56.0f;

    // ---- UI 元素 ----
    Btn btn_save_, btn_load_, btn_clear_, btn_back_;
    Btn btn_delete_map_;
    bool confirm_delete_ = false;      // 删除地图二次确认
    std::string confirm_delete_name_;  // 待确认删除的地图名
    SDL_FRect filename_rect_{};
    SDL_FRect hp_input_rect_{}, del_btn_{};
    SDL_FRect sel_info_rect_{};
    float tool_scroll_ = 0.0f;         // 工具列表滚动偏移

    SDL_FRect size_small_{}, size_med_{}, size_large_{};
    SDL_FRect mode_single_{}, mode_batch_{};
    SDL_FRect brush_1_{}, brush_5_{}, brush_10_{};
    int brush_size_ = 1;               // 地形笔刷直径（格）：1/5/10
    float size_label_y_ = 0.0f, mode_label_y_ = 0.0f;
    float brush_label_y_ = 0.0f;
    SDL_FRect pc_2_{}, pc_4_{}, pc_8_{};          // 玩家数量按钮
    float pc_label_y_ = 0.0f;
    std::vector<SDL_FRect> owner_btns_;           // 归属按钮：index 0=中立, 1..N=玩家N
    float owner_label_y_ = 0.0f, owner_grid_top_ = 0.0f;
    float map_label_y_ = 0.0f;
    float tools_top_ = 0.0f, tools_bottom_ = 0.0f;   // 工具列表可视区域
    float tools_content_h_ = 0.0f;                   // 工具内容总高
    float map_list_top_ = 0.0f, map_list_bottom_ = 0.0f; // 地图文件列表可视区域
    SDL_FRect minimap_rect_{};                       // 小地图（右下角）

    // ---- 方法 ----
    void layout();

    void reset_to_empty();
    void snapshot_entities();          // 从世界同步到 entities_
    void spawn_from_records();         // 依据 entities_ 重建世界实体
    GameObject* spawn_record(const EntityRecord& r);
    EntityRecord make_record(GameObject* obj) const;
    void remove_record(uint64_t obj_id);
    void cleanup_invalid();            // 删除落水等非法实体
    bool record_on_water(const EntityRecord& r) const;

    void place_at(float mx, float my);
    void paint_terrain_at(float mx, float my);
    void erase_at(float mx, float my);          // 橡皮擦：删除鼠标下的实体
    bool cell_occupied(int cx, int cy) const;         // 该格是否被实体（资源/建筑/单位）占据
    bool placement_valid(float mx, float my) const;   // 当前工具在鼠标处能否放置（用于预览红/绿）
    CollisionBox entity_placement_box(float mx, float my) const;   // 实体放置盒（以鼠标所在格为中心，供预览/校验/放置共用）
    void select_at(float mx, float my);
    void delete_selected();
    void apply_health_input();

    void do_save();
    void do_load();
    void do_clear();
    void do_delete_map();
    void do_back();

    void apply_size(MapSize s);
    void apply_player_count(int n);   // 设置玩家数量（2/4/8），重建归属按钮
    void refresh_map_files();
    std::string next_default_name() const;   // 未使用的最大编号（mapNN）

    void zoom_at(float mx, float my, float wheel);
    bool inside_viewport(float x, float y) const;
    bool inside_panel(float x, float y) const;
    Vector2 viewport_to_world(float mx, float my) const;
    Vector2 world_to_viewport(const Vector2& w) const;
    SDL_FRect scrolled(const SDL_FRect& r) const;

    void bake_terrain();
    void render_terrain();
    void render_grid_overlay();
    void render_entities();
    void render_minimap();
    bool point_in_minimap(float x, float y) const;
    void minimap_click(float x, float y);
    void render_ui();
    void draw_button(const SDL_FRect& r, const std::string& label, bool active, bool hover);
    bool point_in(const SDL_FRect& r, float x, float y) const;
    bool handle_ui_click(float x, float y);
    void handle_key(int key);
    void append_text(const char* txt);
    void append_hp_text(const char* txt);
    void set_status(const std::string& msg);

    // ---- 工具辅助 ----
    bool is_terrain_tool(Tool t) const;
    bool is_resource_tool(Tool t) const;
    bool is_unit_tool(Tool t) const;
    bool is_building_tool(Tool t) const;
    ResourceEntityType tool_to_resource(Tool t) const;
    UnitEntityType tool_to_unit(Tool t) const;
    BuildingEntityType tool_to_building(Tool t) const;

    static int resource_size_cells(ResourceEntityType t);
    static int building_size_cells(BuildingEntityType t);
};

#endif // !_MAP_EDITOR_SCENE_H_
