#include "factories.h"
#include "texture_cache.h"

void ObjectFactory::init(GameMap* map) {

    this->map = map;
}

GameObject* ObjectFactory::check_overlap(const CollisionBox& box) const
{
    std::vector<GameObject*> candidates;
    WorldEntityMgr::instance()->query_area(box, candidates);

    for (auto* obj : candidates) {
        if (!obj->check_valid()) continue;  // 跳过即将销毁的
        // 跳过投射物等临时实体（如果你有 Projectile 组件，可在此过滤）
        if (obj->get_component<Projectile>()) continue; // 假设有 Projectile 组件

        if (obj->get_collision_box().intersects(box)) {
            return obj;   // 发现重叠
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
    case ResourceEntityType::Wood:    size_cells = 2;  health = 100;  output = ResourceType::Wood;  color = Color::Green;  break;
    case ResourceEntityType::SGold:   size_cells = 10; health = 3000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::LGold:   size_cells = 15; health = 8000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::Stone:   size_cells = 10; health = 3000; output = ResourceType::Stone; color = Color::Gray;   break;
    case ResourceEntityType::Berries: size_cells = 4;  health = 50;   output = ResourceType::Food;  color = Color::Orange; break;
    default: return nullptr;
    }

    float w = (float)(size_cells * cell_size);
    float h = (float)(size_cells * cell_size);
    float x = (float)(grid_x * cell_size);
    float y = (float)(grid_y * cell_size);

    CollisionBox box{ {x, y}, w, h };

    // 检查整个资源区域是否全部可通行（不能有水）
    for (int row = 0; row < size_cells; ++row)
        for (int col = 0; col < size_cells; ++col)
            if (!map->is_cell_passable(grid_x + col, grid_y + row))
                return nullptr;

    // 如果不允许重叠，检查实体遮挡
    if (!allow_overlap && check_overlap(box))
        return nullptr;

    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    SDL_Texture* tex = TextureCache::instance()->get_resource_texture( type, (int)w, (int)h);
    if (tex) {
        render->texture = tex;
        render->color = color;
    }
    else {
        render->color = color;
    }

    auto* harvestable = obj->add_component<Harvestable>();
    harvestable->entity_type = type;
    harvestable->output_type = output;

    auto* health_comp = obj->add_component<Health>();
    health_comp->max_health = health;
    health_comp->current_health = health;

    obj->add_component<Selectable>();

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_unit_by_type(UnitEntityType type, const CollisionBox& box, bool allow_overlap)
{
    switch (type) {
    case UnitEntityType::Villager:
        return create_villager(box, allow_overlap);
        // 未来添加：
        // case UnitEntityType::Cavalry: return create_cavalry(box, allow_overlap);
        // ...
    default:
        return nullptr;
    }
}

GameObject* ObjectFactory::create_villager(const CollisionBox& box, bool allow_overlap)
{
    if (!map) return nullptr;

    // 检查可通行性
    int cx = (int)(box.position.x / map->get_cell_size());
    int cy = (int)(box.position.y / map->get_cell_size());
    if (!map->is_cell_passable(cx, cy)) return nullptr;

    // 检查重叠
    if (!allow_overlap && check_overlap(box)) return nullptr;

    auto* obj = new GameObject(box);

    // 纹理
    auto* render = obj->add_component<Renderable>();

    // 1. 设置单位的自身颜色（阵营颜色）
    Color unit_color = get_player_color(current_player_id);
    render->color = unit_color;

    // 2. 获取纹理时传入 SDL_Color
    SDL_Color sdl_color = to_sdl_color(unit_color);
    SDL_Texture* tex = TextureCache::instance()->get_unit_texture(
        UnitEntityType::Villager, sdl_color, (int)box.width, (int)box.height);
    if (tex) {
        render->texture = tex;
    }

    obj->add_component<Selectable>();
    obj->add_component<ImpactAnimation>();

    auto* movable = obj->add_component<Movable>();
    movable->speed = 60.0f;

    auto* unitType = obj->add_component<UnitType>();
    unitType->type = UnitEntityType::Villager;

    // 采集组件
    auto* gatherer = obj->add_component<Gatherer>();
    gatherer->gather_amount = 10;
    gatherer->gather_interval = 1.0f;

    // 生命值
    auto* health = obj->add_component<Health>();
    health->max_health = 50;
    health->current_health = 50;

    // 所有权
    auto* ownership = obj->add_component<Ownership>();
    ownership->player_id = current_player_id;
    ownership->team_id = 0;

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

    // 可通行性检查
    for (int row = 0; row < size_cells; ++row)
        for (int col = 0; col < size_cells; ++col)
            if (!map->is_cell_passable(grid_x + col, grid_y + row))
                return nullptr;

    if (!allow_overlap && check_overlap(box)) return nullptr;

    auto* obj = new GameObject(box);

    // 渲染组件：使用建筑纹理
    auto* render = obj->add_component<Renderable>();
    Color tc_color = get_player_color(current_player_id);
    render->color = tc_color;

    SDL_Color sdl_color = to_sdl_color(tc_color);
    SDL_Texture* tex = TextureCache::instance()->get_building_texture(
        BuildingEntityType::TownCenter, sdl_color, (int)w, (int)h);
    if (tex) {
        render->texture = tex;

        // 建筑结构标记
        obj->add_component<Structure>();
        // 资源交付能力
        auto* dropoff = obj->add_component<ResourceDropoff>();
        dropoff->accept_mask = ALL_MASK;

        // 可选中
        obj->add_component<Selectable>();

        // 所有权
        auto* ownership = obj->add_component<Ownership>();
        ownership->player_id = current_player_id;
        ownership->team_id = 0;

        // 生命值
        auto* health = obj->add_component<Health>();
        health->max_health = 1000;
        health->current_health = 1000;

        WorldEntityMgr::instance()->insert_object(obj);
        return obj;
    }
}

Color ObjectFactory::get_player_color(int player_id)
{
    switch (player_id) {
    case 1:  return Color::DarkBlue;
    case 2:  return Color::DarkRed;
    default: return Color::LightGray;
    }
}