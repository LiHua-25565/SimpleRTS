#include "render_system.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "texture_cache.h"

void RenderSystem::on_update(float delta)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [entityId, obj] : pool) // 避免与 target_id 变量名混淆，这里改为 entityId
    {
        if (!obj->check_valid()) continue;

        // 更新闪烁
        auto* flash = obj->get_component<FlashComponent>();
        if (flash && flash->flash_active) {
            flash->flash_timer -= delta;
            if (flash->flash_timer <= 0.0f) {
                flash->flash_active = false;
                flash->flash_timer = 0.0f;
            }
            flash->blink_timer -= delta;
            if (flash->blink_timer <= 0.0f) {
                flash->blink_timer += flash->blink_interval;
                flash->blink_on = !flash->blink_on;
            }
        }

        auto* renderable = obj->get_component<Renderable>();
        auto* anim = obj->get_component<ImpactAnimation>();
        if (!renderable || !anim) continue;

        if (!anim->is_attacking)
            continue;

        // 推进动画时间
        anim->anim_pass_time += delta;

        // 动画结束，自动关闭并复位渲染框
        if (anim->anim_pass_time >= anim->anim_wait_time)
        {
            anim->is_attacking = false;
            anim->anim_pass_time = 0.0f;
            renderable->collision_box = obj->get_collision_box();
            continue;
        }

        // 计算三角形波偏移量
        float half = anim->anim_wait_time * 0.5f;
        float progress = (anim->anim_pass_time <= half)
            ? anim->anim_pass_time / half
            : 1.0f - (anim->anim_pass_time - half) / half;

        const auto& logic_box = obj->get_collision_box();
        float offset = progress * anim->impact_distance * logic_box.height; // 正方形单位

        Vector2 dir = anim->direction;
        if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
        else dir = dir.normalize();

        CollisionBox anim_box = logic_box;
        anim_box.position.x += dir.x * offset;
        anim_box.position.y += dir.y * offset;
        renderable->collision_box = anim_box;

        if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
        else dir = dir.normalize();
    }
}

void RenderSystem::on_render()
{
    const auto& pool = WorldEntityMgr::instance()->get_object_pool();
    const auto& selected_ids = SelectionMgr::instance()->get_selected_object_id_set();
    int local_player_id = ResourcesMgr::instance()->get_local_player_id();

    for (const auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* renderable = obj->get_component<Renderable>();
        auto* animation = obj->get_component<ImpactAnimation>();
        if (!animation || !animation->is_attacking)
            renderable->collision_box = obj->get_collision_box();

        if (!renderable) continue;

        const auto& collider = renderable->collision_box;
        const auto& pos = collider.position;
        float w = collider.width;
        float h = collider.height;

        SDL_Texture* tex = nullptr;
        auto* gatherer = obj->get_component<Gatherer>();
        auto* unit_type = obj->get_component<UnitType>();
        if (gatherer && unit_type) {
            // 农民类单位，根据携带资源决定纹理
            ResourceType carried = gatherer->carried_type; // 可能为 None
            SDL_Color color = to_sdl_color(renderable->color); // 阵营色
            tex = TextureCache::instance()->get_carrying_unit_texture(unit_type->type, color, (int)collider.width, (int)collider.height, carried);
        }
        else {
            tex = renderable->texture; // 普通纹理（可能已经生成过）
        }

        RenderCmd cmd{};
        cmd.position = pos;
        cmd.w = w;
        cmd.h = h;
        if (tex) cmd.texture = tex;
        cmd.layer = RenderLayer::Unit;
        cmd.color = to_sdl_color(renderable->color);

        bool is_selected = selected_ids.count(id) > 0;
        auto* ownership = obj->get_component<Ownership>();
        int player_id = ownership ? ownership->player_id : 0;
        bool is_local = (player_id == local_player_id);

        // ---- 边框处理（闪烁 > 选中 > 阵营） ----
        auto* flash = obj->get_component<FlashComponent>();
        if (flash && flash->flash_active && flash->blink_on)
        {
            // 闪烁状态：亮白加粗
            cmd.border_width = 2;
            cmd.border_color = to_sdl_color(Color::White);
        }
        else if (is_selected)
        {
            // 选中状态：亮白加粗
            cmd.border_width = 2;
            cmd.border_color = to_sdl_color(Color::White);
        }
        else
        {
            // 未选中状态：根据阵营显示边框颜色
            cmd.border_width = 1;
            if (is_local)
                cmd.border_color = to_sdl_color(Color::SoftBlue);  // 淡蓝（己方）
            else if (player_id == 0)
                cmd.border_color = to_sdl_color(Color::Gray);  // 灰色（中立）
            else
                cmd.border_color = to_sdl_color(Color::SoftRed);  // 淡红（敌方示例）
        }

        RenderMgr::instance()->push_cmd(cmd);
    }
}