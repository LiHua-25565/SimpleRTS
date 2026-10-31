#ifndef _DEF_EDITOR_SCENE_H_
#define _DEF_EDITOR_SCENE_H_

#include "scene.h"
#include "entity_defs.h"

#include <string>
#include <vector>

// 兵种 / 建筑编辑器：
//   - 左侧列表 + 右侧属性表单
//   - 支持新建、删除、保存 / 导入 .srdef 文件
//   - 名字默认生成文字纹理（最多 4 个字），也可填图片路径
class DefEditorScene : public Scene {
public:
    DefEditorScene() = default;
    ~DefEditorScene() = default;

    void on_input(const SDL_Event& event) override;
    void on_update(float delta) override;
    void on_render() override;
    void on_enter() override;
    void on_exit() override;

private:
    enum class Category { Units, Buildings, Techs };

    enum class Field {
        None,
        FileName,
        Name,
        Texture,
        Hp,
        DefenseNormal,
        DefensePierce,
        DefenseSiege,
        Speed,
        CostWood,
        CostFood,
        CostGold,
        CostStone,
        ProduceTime,
        BuildTime,
        SizeCells,
        ResearchTime,
        AttackDamage,
        AttackRange,
        AttackInterval,
        GatherAmount,
        GatherInterval,
        CarryCapacity,
    };

    struct FieldView {
        Field field = Field::None;
        SDL_FRect rect{};
        std::string label;
    };

    // ---- 状态 ----
    Category category_ = Category::Units;
    int selected_unit_id_ = 0;
    int selected_building_id_ = 0;
    std::string selected_tech_name_;

    Field focus_ = Field::None;
    std::string edit_buf_;
    bool edit_replace_on_type_ = false;
    int editing_effect_ = -1;              // 正在编辑的科技效果下标
    std::string file_name_ = "custom_defs";
    std::string status_;
    float status_timer_ = 0.0f;

    float list_scroll_ = 0.0f;
    float produce_scroll_ = 0.0f;
    float require_scroll_ = 0.0f;
    float research_scroll_ = 0.0f;
    float effect_scroll_ = 0.0f;
    int delete_confirm_id_ = 0;
    std::string delete_confirm_tech_;

    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;

    std::vector<FieldView> fields_;

    // ---- 固定布局 ----
    static constexpr float TOP_H = 60.0f;
    static constexpr float LEFT_W = 300.0f;
    static constexpr float CONTENT_X = 312.0f;
    static constexpr float LIST_TOP = 214.0f;
    static constexpr float LIST_BOTTOM = 700.0f;
    static constexpr float LIST_ROW_H = 34.0f;

    SDL_FRect file_rect_{};
    SDL_FRect btn_save_{}, btn_load_{}, btn_back_{};
    SDL_FRect tab_units_{}, tab_buildings_{}, tab_techs_{};
    SDL_FRect btn_new_{}, btn_delete_{};
    SDL_FRect list_rect_{};
    SDL_FRect form_rect_{};
    SDL_FRect preview_rect_{};
    SDL_FRect damage_type_btn_{};
    SDL_FRect attack_toggle_{}, gather_toggle_{}, ranged_toggle_{}, dropoff_toggle_{};
    SDL_FRect requires_rect_{};
    SDL_FRect research_rect_{};
    SDL_FRect produce_rect_{};
    SDL_FRect effect_list_rect_{};
    SDL_FRect effect_add_btn_{};

    // ---- 辅助 ----
    EntityCatalog* catalog();
    UnitDef* current_unit();
    BuildingDef* current_building();
    TechDef* current_tech();
    bool has_selection() const;
    int selected_id() const;
    std::string selected_name() const;

    void rebuild_fields();
    const FieldView* find_field_view(Field f) const;

    std::string field_text(Field f) const;
    bool field_is_float(Field f) const;
    bool field_is_integer(Field f) const;
    bool field_is_text(Field f) const;
    void begin_edit(Field f);
    void commit_edit();
    void cancel_edit();
    void apply_field(Field f, const std::string& text);

    void select_category(Category c);
    void select_unit(int id);
    void select_building(int id);
    void select_tech(const std::string& name);
    void ensure_selection();

    void do_new();
    void do_delete();
    void do_save();
    void do_load();
    void do_back();
    void set_status(const std::string& msg);

    bool point_in(const SDL_FRect& r, float x, float y) const;
    void handle_click(float x, float y);
    void toggle_damage_type();
    void toggle_plugin(uint32_t flag);
    void toggle_bool(bool& value);
    void toggle_produce(int unit_type_id);
    void toggle_required_tech(const std::string& tech_name);
    void toggle_research_tech(const std::string& tech_name);
    bool draw_tech_checklist(const SDL_FRect& rect, float& scroll, const std::string& csv,
                             bool two_columns = true);

    // 科技效果
    SDL_FRect effect_value_rect(int index) const;
    void begin_effect_edit(int index);
    void apply_effect_edit(int index, const std::string& text);
    void add_tech_effect();
    void remove_tech_effect(int index);
    void cycle_effect_target(TechEffect& effect);
    void cycle_effect_type(TechEffect& effect);
    void draw_tech_effects(TechDef& def);

    void layout();
    void draw_button(const SDL_FRect& r, const std::string& label, bool active, bool danger = false);
    void draw_input(const FieldView& view);
    void draw_toggle(const SDL_FRect& r, const std::string& label, bool active);
    void draw_topbar();
    void draw_side_list();
    void draw_form();
    void draw_unit_form(UnitDef& def);
    void draw_building_form(BuildingDef& def);
    void draw_tech_form(TechDef& def);
    void draw_preview(const std::string& name, uint32_t tex_id);
};

#endif // !_DEF_EDITOR_SCENE_H_
