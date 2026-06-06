#include "render_system.h"
#include "render_mgr.h"
#include "selection_mgr.h"
#include "resources_mgr.h"
#include "texture_cache.h"
#include "components.h"

void RenderSystem::on_update(float delta)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [entityId, obj] : pool)
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

        // 更新建造进度
        auto* build_prog = obj->get_component<BuildProgressComponent>();
        if (build_prog && build_prog->active) {
            build_prog->elapsed += delta;
            if (build_prog->elapsed >= build_prog->total_time) {
                build_prog->active = false;          // 标记完成
                // 完成后移除组件，释放内存（可选）
                obj->remove_component<BuildProgressComponent>();
            }
        }

        // 更新单位动画
        auto* renderable = obj->get_component<Renderable>();
        auto* anim = obj->get_component<ImpactAnimation>();
        if (!renderable || !anim) continue;

        if (!anim->is_attacking) continue;

        anim->anim_pass_time += delta;

        if (anim->anim_pass_time >= anim->anim_wait_time)
        {
            anim->is_attacking = false;
            anim->anim_pass_time = 0.0f;
            renderable->collision_box = obj->get_collision_box();
            continue;
        }

        float half = anim->anim_wait_time * 0.5f;
        float progress = (anim->anim_pass_time <= half)
            ? anim->anim_pass_time / half
            : 1.0f - (anim->anim_pass_time - half) / half;

        const auto& logic_box = obj->get_collision_box();
        float offset = progress * anim->impact_distance * logic_box.height;

        Vector2 dir = anim->direction;
        if (dir.length() < 0.01f) dir = { 1.0f, 0.0f };
        else dir = dir.normalize();

        CollisionBox anim_box = logic_box;
        anim_box.position.x += dir.x * offset;
        anim_box.position.y += dir.y * offset;
        renderable->collision_box = anim_box;
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
        if (!renderable) continue;

        auto* animation = obj->get_component<ImpactAnimation>();
        if (!animation || !animation->is_attacking)
            renderable->collision_box = obj->get_collision_box();

        const auto& collider = renderable->collision_box;
        const auto& pos = collider.position;
        float w = collider.width;
        float h = collider.height;

        // 基础纹理 ID（建筑、资源、普通单位）
        uint32_t tex_id = renderable->texture_id;
        auto* gatherer = obj->get_component<Gatherer>();
        auto* unit_type = obj->get_component<UnitType>();
        if (gatherer && unit_type) {
            ResourceType carried = gatherer->carried_type;
            SDL_Color color = to_sdl_color(renderable->color);
            tex_id = TextureCache::instance()->get_carrying_unit_texture(
                unit_type->type, color, (int)collider.width, (int)collider.height, carried);
        }

        RenderCmd cmd{};
        cmd.position = pos;
        cmd.w = w;
        cmd.h = h;
        cmd.texture_id = tex_id;
        auto* anim = obj->get_component<ImpactAnimation>();
        if (anim && anim->is_attacking)
            cmd.layer = RenderLayer::Animation;
        else
            cmd.layer = RenderLayer::Unit;
        cmd.color = to_sdl_color(renderable->color);

        // 建造动画：透明度渐变 + 头顶进度条
        auto* build_prog = obj->get_component<BuildProgressComponent>();
        if (build_prog && build_prog->active) {
            float progress = build_prog->elapsed / build_prog->total_time;
            if (progress > 1.0f) progress = 1.0f;
            // 透明度从 100 到 255
            cmd.color.a = static_cast<Uint8>(100 + 155 * progress);

            // 进度条
            const auto& collider = obj->get_collision_box();
            float bar_w = collider.width * 0.8f;
            float bar_h = 6.0f;
            Vector2 bar_pos = {
                collider.position.x + (collider.width - bar_w) * 0.5f,
                collider.position.y - bar_h - 2.0f
            };

            RenderCmd bar_bg;
            bar_bg.layer = RenderLayer::Animation;
            bar_bg.position = bar_pos;
            bar_bg.w = bar_w;
            bar_bg.h = bar_h;
            bar_bg.color = { 60, 60, 60, 200 };
            RenderMgr::instance()->push_cmd(bar_bg);

            RenderCmd bar_fg;
            bar_fg.layer = RenderLayer::Animation;
            bar_fg.position = bar_pos;
            bar_fg.w = bar_w * progress;
            bar_fg.h = bar_h;
            bar_fg.color = { 0, 200, 0, 200 };
            RenderMgr::instance()->push_cmd(bar_fg);
        }

        bool is_selected = selected_ids.count(id) > 0;
        auto* ownership = obj->get_component<Ownership>();
        int player_id = ownership ? ownership->player_id : 0;
        bool is_local = (player_id == local_player_id);

        auto* flash = obj->get_component<FlashComponent>();
        if (flash && flash->flash_active && flash->blink_on)
        {
            cmd.border_width = 2;
            cmd.border_color = to_sdl_color(Color::White);
        }
        else if (is_selected)
        {
            cmd.border_width = 2;
            cmd.border_color = to_sdl_color(Color::White);
        }
        else
        {
            cmd.border_width = 1;
            if (is_local)
                cmd.border_color = to_sdl_color(Color::SoftBlue);
            else if (player_id == 0)
                cmd.border_color = to_sdl_color(Color::Gray);
            else
                cmd.border_color = to_sdl_color(Color::SoftRed);
        }

        RenderMgr::instance()->push_cmd(cmd);
    }
}