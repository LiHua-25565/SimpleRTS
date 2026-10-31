#include "factories.h"
#include "texture_cache.h"
#include "resources_mgr.h"
#include "entity_defs.h"
#include "tech_effects.h"
#include "components.h"

#include <algorithm>
#include <cmath>

void ObjectFactory::init(GameMap* map) {
    this->map = map;
}

void ObjectFactory::set_player_color(int player_id, Color c) {
    if (c == Color::None) color_overrides_.erase(player_id);
    else color_overrides_[player_id] = c;
}

Color ObjectFactory::player_color(int player_id) const {
    auto it = color_overrides_.find(player_id);
    if (it != color_overrides_.end()) return it->second;
    return get_player_color(player_id);
}

GameObject* ObjectFactory::check_overlap(const CollisionBox& box) const {
    std::vector<GameObject*> candidates;
    WorldEntityMgr::instance()->query_area(box, candidates);
    for (auto* obj : candidates) {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        if (obj->get_collision_box().overlaps_strict(box)) {
            return obj;
        }
    }
    return nullptr;
}

GameObject* ObjectFactory::create_resource_by_type(ResourceEntityType type, int grid_x, int grid_y, bool allow_overlap)
{
    if (!map) return nullptr;

    int cell_size = map->get_cell_size();
    int size_cells = 0;
    int health = 100;
    ResourceType output = ResourceType::Wood;
    Color color;

    switch (type) {
    case ResourceEntityType::Wood:    size_cells = 2;  health = 20;  output = ResourceType::Wood;  color = Color::Green;  break;
    case ResourceEntityType::SGold:   size_cells = 10; health = 3000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::LGold:   size_cells = 15; health = 8000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::Stone:   size_cells = 10; health = 3000; output = ResourceType::Stone; color = Color::Gray;   break;
    case ResourceEntityType::Berries: size_cells = 4;  health = 500;   output = ResourceType::Food;  color = Color::Orange; break;
    default: return nullptr;
    }

    float w = (float)(size_cells * cell_size);
    float h = (float)(size_cells * cell_size);
    float x = (float)(grid_x * cell_size);
    float y = (float)(grid_y * cell_size);

    CollisionBox box{ {x, y}, w, h };

    if (!map->is_box_passable(box))
        return nullptr;

    if (!allow_overlap && check_overlap(box))
        return nullptr;

    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    uint32_t tex_id = TextureCache::instance()->get_resource_texture(type, (int)w, (int)h);
    render->texture_id = tex_id;
    render->color = color;          // 纹理失效时的备份色

    obj->add_component<FlashComponent>();

    auto* harvestable = obj->add_component<Harvestable>();
    harvestable->entity_type = type;
    harvestable->output_type = output;

    auto* health_comp = obj->add_component<Health>();
    health_comp->max_health = health;
    health_comp->current_health = health;

    obj->add_component<Selectable>();

    WorldEntityMgr::instance()->insert_object(obj);
    map->add_object_to_dynamic_obstacle_field(obj);
    return obj;
}

GameObject* ObjectFactory::create_unit_by_type(UnitEntityType type, const CollisionBox& box, bool allow_overlap)
{
    const UnitDef* def = EntityCatalog::instance()->find_unit(type);
    if (!def) return nullptr;
    return create_custom_unit(*def, box, allow_overlap);
}

GameObject* ObjectFactory::create_building_by_type(BuildingEntityType type, int grid_x, int grid_y, bool allow_overlap) {
    const BuildingDef* def = EntityCatalog::instance()->find_building(type);
    if (!def) return nullptr;
    return create_custom_building(*def, grid_x, grid_y, allow_overlap);
}

GameObject* ObjectFactory::create_villager(const CollisionBox& box, bool allow_overlap) {
    return create_unit_by_type(UnitEntityType::Villager, box, allow_overlap);
}

GameObject* ObjectFactory::create_archer(const CollisionBox& box, bool allow_overlap) {
    return create_unit_by_type(UnitEntityType::Archer, box, allow_overlap);
}

GameObject* ObjectFactory::create_crossbowman(const CollisionBox& box, bool allow_overlap) {
    return create_unit_by_type(UnitEntityType::Crossbowman, box, allow_overlap);
}

GameObject* ObjectFactory::create_town_center(int grid_x, int grid_y, bool allow_overlap) {
    return create_building_by_type(BuildingEntityType::TownCenter, grid_x, grid_y, allow_overlap);
}

GameObject* ObjectFactory::create_archery_range(int grid_x, int grid_y, bool allow_overlap) {
    return create_building_by_type(BuildingEntityType::ArcheryRange, grid_x, grid_y, allow_overlap);
}

// ==================== 数据驱动兵种 ====================
GameObject* ObjectFactory::create_custom_unit(const UnitDef& def, const CollisionBox& box, bool allow_overlap)
{
    if (!map) return nullptr;
    if (!map->is_box_passable(box)) return nullptr;
    if (!allow_overlap && check_overlap(box)) return nullptr;

    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    Color unit_color = player_color(current_player_id);
    render->color = unit_color;

    SDL_Color sdl_color = to_sdl_color(unit_color);
    uint32_t tex_id = TextureCache::instance()->get_unit_texture(
        static_cast<UnitEntityType>(def.type_id), sdl_color, (int)box.width, (int)box.height);
    if (tex_id) render->texture_id = tex_id;

    obj->add_component<Selectable>();
    obj->add_component<ImpactAnimation>();
    obj->add_component<FlashComponent>();

    auto* movable = obj->add_component<Movable>();
    movable->speed = std::max(1.0f, def.speed);

    auto* unitType = obj->add_component<UnitType>();
    unitType->type = static_cast<UnitEntityType>(def.type_id);

    auto* health = obj->add_component<Health>();
    health->max_health = std::max(1, def.hp);
    health->current_health = health->max_health;

    auto* armor = obj->add_component<Armor>();
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
        armor->reduction[i] = std::max(0, def.defense[i]);

    if (def.has_attack()) {
        auto* attack = obj->add_component<Attack>();
        attack->damage = std::max(0, def.attack_damage);
        attack->damage_type = def.damage_type;
        attack->attack_interval = std::max(0.05f, def.attack_interval);
        attack->range = std::max(0.0f, def.attack_range);
        attack->is_ranged = def.attack_ranged;
    }

    if (def.has_gather()) {
        auto* gatherer = obj->add_component<Gatherer>();
        gatherer->gather_amount = std::max(0, def.gather_amount);
        gatherer->gather_interval = std::max(0.05f, def.gather_interval);
        gatherer->carry_capacity = std::max(1, def.carry_capacity);
    }

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    WorldEntityMgr::instance()->insert_object(obj);
    apply_completed_techs_to_unit(obj, current_player_id);
    return obj;
}

// ==================== 数据驱动建筑 ====================
GameObject* ObjectFactory::create_custom_building(const BuildingDef& def, int grid_x, int grid_y, bool allow_overlap)
{
    if (!map) return nullptr;

    const int size_cells = std::clamp(def.size_cells, 1, 80);
    int cell_size = map->get_cell_size();
    float w = (float)(size_cells * cell_size);
    float h = (float)(size_cells * cell_size);
    float x = (float)(grid_x * cell_size);
    float y = (float)(grid_y * cell_size);
    CollisionBox box{ {x, y}, w, h };

    if (!allow_overlap) {
        if (!map->is_box_passable(box)) return nullptr;
        if (check_overlap(box)) return nullptr;
    }

    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    Color build_color = player_color(current_player_id);
    render->color = build_color;

    SDL_Color sdl_color = to_sdl_color(build_color);
    uint32_t tex_id = TextureCache::instance()->get_building_texture(
        static_cast<BuildingEntityType>(def.type_id), sdl_color, (int)w, (int)h);
    if (tex_id) render->texture_id = tex_id;

    obj->add_component<Structure>();
    obj->add_component<FlashComponent>();
    obj->add_component<BuildingType>()->type = static_cast<BuildingEntityType>(def.type_id);

    if (def.dropoff) {
        auto* dropoff = obj->add_component<ResourceDropoff>();
        dropoff->accept_mask = ALL_MASK;
    }

    obj->add_component<Selectable>();
    obj->add_component<ProductionQueue>();

    auto* build_prog = obj->add_component<BuildProgressComponent>();
    build_prog->total_time = std::max(0.1f, def.build_time);
    build_prog->active = true;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    auto* health = obj->add_component<Health>();
    health->max_health = std::max(1, def.hp);
    health->current_health = health->max_health;

    auto* armor = obj->add_component<Armor>();
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
        armor->reduction[i] = std::max(0, def.defense[i]);

    WorldEntityMgr::instance()->insert_object(obj);
    map->add_object_to_dynamic_obstacle_field(obj);
    return obj;
}

uint64_t ObjectFactory::create_projectile_by_type(ProjectileType type, const Vector2& start, const Vector2& target, int damage, uint64_t target_id)
{
    switch (type) {
    case ProjectileType::Arrow:
        return create_arrow(start, target, damage, target_id);
    default:
        return 0;
    }
}

uint64_t ObjectFactory::create_arrow(const Vector2& start, const Vector2& target, int damage, uint64_t target_id)
{
    float w = 12.0f, h = 4.0f;
    CollisionBox box{ {start.x - w / 2, start.y - h / 2}, w, h };
    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    Color arrow_color = player_color(current_player_id);
    render->color = arrow_color;

    auto* proj = obj->add_component<Projectile>();
    proj->speed = 600.0f;
    proj->target_id = target_id;
    proj->damage = damage;

    auto* movable = obj->add_component<Movable>();
    Vector2 dir = (target - start).normalize();
    movable->velocity = dir * proj->speed;
    movable->target = { -1, -1 };

    WorldEntityMgr::instance()->insert_object(obj);
    return obj->get_id();
}

Color ObjectFactory::get_player_color(int player_id)
{
    switch (player_id) {
    case 1:  return Color::DarkBlue;
    case 2:  return Color::DarkRed;
    case 3:  return Color::LeafGreen;
    case 4:  return Color::Gold;
    case 5:  return Color::Purple;
    case 6:  return Color::Orange;
    case 7:  return Color::Cyan;
    case 8:  return Color::Magenta;
    default: return Color::LightGray;
    }
}
