#include "factories.h"
#include "texture_cache.h"
#include "resources_mgr.h"

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
        if (obj->get_collision_box().overlaps_strict(box)) {  // 改为严格相交
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
    if (tex_id) {
        render->texture_id = tex_id;
        render->color = color;          // 备份色在纹理失效时使用
    }
    else {
        render->color = color;
    }

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
    switch (type) {
    case UnitEntityType::Villager:
        return create_villager(box, allow_overlap);
    case UnitEntityType::Archer:
        return create_archer(box, allow_overlap);
    case UnitEntityType::Crossbowman:
        return create_crossbowman(box, allow_overlap);
    default:
        return nullptr;
    }
}

GameObject* ObjectFactory::create_building_by_type(BuildingEntityType type, int grid_x, int grid_y, bool allow_overlap) {
    switch (type) {
    case BuildingEntityType::TownCenter:
        return create_town_center(grid_x, grid_y, allow_overlap);
    case BuildingEntityType::ArcheryRange:
        return create_archery_range(grid_x, grid_y, allow_overlap);
    default:
        return nullptr;
    }
}

GameObject* ObjectFactory::create_villager(const CollisionBox& box, bool allow_overlap)
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
        UnitEntityType::Villager, sdl_color, (int)box.width, (int)box.height);
    if (tex_id) {
        render->texture_id = tex_id;
    }

    obj->add_component<Selectable>();
    obj->add_component<ImpactAnimation>();
    obj->add_component<FlashComponent>();

    auto* movable = obj->add_component<Movable>();
    movable->speed = 60.0f;

    auto* unitType = obj->add_component<UnitType>();
    unitType->type = UnitEntityType::Villager;

    auto* gatherer = obj->add_component<Gatherer>();
    gatherer->gather_amount = 10;
    gatherer->gather_interval = 1.0f;

    auto* health = obj->add_component<Health>();
    health->max_health = 50;
    health->current_health = 50;

    auto* attack = obj->add_component<Attack>();
    attack->damage = 10;
    // 设置农民对各护甲的穿透值
    attack->armor_penetration[static_cast<int>(ArmorType::None)] = 1;
    attack->armor_penetration[static_cast<int>(ArmorType::Light)] = 1;
    attack->armor_penetration[static_cast<int>(ArmorType::Heavy)] = 0;
    attack->armor_penetration[static_cast<int>(ArmorType::Building)] = 0;

    // 添加护甲组件（农民为轻甲）
    auto* armor = obj->add_component<Armor>();
    armor->type = ArmorType::Light;
    armor->armor_value = 1;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_archer(const CollisionBox& box, bool allow_overlap)
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
        UnitEntityType::Archer, sdl_color, (int)box.width, (int)box.height);
    if (tex_id) {
        render->texture_id = tex_id;
    }

    obj->add_component<Selectable>();
    obj->add_component<ImpactAnimation>();

    auto* movable = obj->add_component<Movable>();
    movable->speed = 60.0f;

    auto* unitType = obj->add_component<UnitType>();
    unitType->type = UnitEntityType::Archer;

    auto* attack = obj->add_component<Attack>();
    attack->damage = 8;
    attack->attack_interval = 1.5f;
    attack->range = 200.0f;
    attack->is_ranged = true;
    // 设置弓兵对各护甲的穿透值（远程有较高的穿透）
    attack->armor_penetration[static_cast<int>(ArmorType::None)] = 2;
    attack->armor_penetration[static_cast<int>(ArmorType::Light)] = 3;
    attack->armor_penetration[static_cast<int>(ArmorType::Heavy)] = 1;
    attack->armor_penetration[static_cast<int>(ArmorType::Building)] = 0;

    // 添加护甲组件（弓兵为无甲）
    auto* armor = obj->add_component<Armor>();
    armor->type = ArmorType::None;
    armor->armor_value = 0;

    auto* health = obj->add_component<Health>();
    health->max_health = 40;
    health->current_health = 40;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_crossbowman(const CollisionBox& box, bool allow_overlap) {
    if (!map) return nullptr;
    if (!allow_overlap && check_overlap(box)) return nullptr;

    if (!map->is_box_passable(box)) return nullptr;

    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    Color unit_color = player_color(current_player_id);
    render->color = unit_color;

    SDL_Color sdl_color = to_sdl_color(unit_color);
    uint32_t tex_id = TextureCache::instance()->get_unit_texture(
        UnitEntityType::Crossbowman, sdl_color, (int)box.width, (int)box.height);
    if (tex_id) render->texture_id = tex_id;

    obj->add_component<Selectable>();
    obj->add_component<ImpactAnimation>();
    obj->add_component<FlashComponent>();

    auto* movable = obj->add_component<Movable>();
    movable->speed = 60.0f;

    auto* unitType = obj->add_component<UnitType>();
    unitType->type = UnitEntityType::Crossbowman;

    auto* attack = obj->add_component<Attack>();
    attack->damage = 12;
    attack->attack_interval = 1.8f;
    attack->range = 250.0f;
    attack->is_ranged = true;
    attack->armor_penetration[static_cast<int>(ArmorType::None)] = 3;
    attack->armor_penetration[static_cast<int>(ArmorType::Light)] = 4;
    attack->armor_penetration[static_cast<int>(ArmorType::Heavy)] = 2;
    attack->armor_penetration[static_cast<int>(ArmorType::Building)] = 1;

    auto* armor = obj->add_component<Armor>();
    armor->type = ArmorType::Light;
    armor->armor_value = 2;

    auto* health = obj->add_component<Health>();
    health->max_health = 45;
    health->current_health = 45;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_town_center(int grid_x, int grid_y, bool allow_overlap)
{
    if (!map) return nullptr;

    const int size_cells = 20;
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
    Color tc_color = player_color(current_player_id);
    render->color = tc_color;

    SDL_Color sdl_color = to_sdl_color(tc_color);
    uint32_t tex_id = TextureCache::instance()->get_building_texture(
        BuildingEntityType::TownCenter, sdl_color, (int)w, (int)h);
    if (tex_id) {
        render->texture_id = tex_id;
    }

    obj->add_component<Structure>();
    obj->add_component<FlashComponent>();
    obj->add_component<BuildingType>()->type = BuildingEntityType::TownCenter;

    auto* dropoff = obj->add_component<ResourceDropoff>();
    dropoff->accept_mask = ALL_MASK;

    obj->add_component<Selectable>();
    obj->add_component<ProductionQueue>();

    // 建造进度组件（5秒建造时间）
    auto* build_prog = obj->add_component<BuildProgressComponent>();
    build_prog->total_time = 5.0f;
    build_prog->active = true;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    auto* health = obj->add_component<Health>();
    health->max_health = 1000;
    health->current_health = 1000;

    WorldEntityMgr::instance()->insert_object(obj);
    map->add_object_to_dynamic_obstacle_field(obj);
    return obj;
}

GameObject* ObjectFactory::create_archery_range(int grid_x, int grid_y, bool allow_overlap) {
    if (!map) return nullptr;

    const int size_cells = 20;
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
    Color tc_color = player_color(current_player_id);
    render->color = tc_color;

    SDL_Color sdl_color = to_sdl_color(tc_color);
    uint32_t tex_id = TextureCache::instance()->get_building_texture(
        BuildingEntityType::ArcheryRange, sdl_color, (int)w, (int)h);
    if (tex_id) {
        render->texture_id = tex_id;
    }

    obj->add_component<Structure>();
    obj->add_component<FlashComponent>();
    obj->add_component<BuildingType>()->type = BuildingEntityType::ArcheryRange;

    // 靶场不是资源提交点（只有城镇中心收资源），不挂 ResourceDropoff

    obj->add_component<Selectable>();
    obj->add_component<ProductionQueue>();

    // 建造进度组件
    auto* build_prog = obj->add_component<BuildProgressComponent>();
    build_prog->total_time = 5.0f;
    build_prog->active = true;

    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = ResourcesMgr::instance()->get_team_id(current_player_id);

    auto* health = obj->add_component<Health>();
    health->max_health = 1200;
    health->current_health = 1200;

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
    render->color = arrow_color;   // 箭矢无纹理，纯色

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