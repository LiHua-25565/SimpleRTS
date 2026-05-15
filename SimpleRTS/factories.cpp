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

GameObject* ObjectFactory::create_resource(ResourceEntityType type, int grid_x, int grid_y, bool allow_overlap)
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
    SDL_Texture* tex = TextureCache::instance()->get_resource_texture(
        type, currentPlayerId, (int)w, (int)h);
    if (tex) {
        render->texture = tex;
        render->color = Color::White;
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
    auto* ownership = obj->add_component<Ownership>();
    ownership->playerId = currentPlayerId;
    ownership->teamId = 0;

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_unit(const CollisionBox& collisionBox, bool allow_overlap)
{
    if (!map) return nullptr;

    // 检查目标位置是否可通行（不是水）
    int cx = (int)(collisionBox.position.x / map->get_cell_size());
    int cy = (int)(collisionBox.position.y / map->get_cell_size());
    if (!map->is_cell_passable(cx, cy))
        return nullptr;

    // 如果不允许重叠，则检查碰撞
    if (!allow_overlap && check_overlap(collisionBox))
        return nullptr;

    auto* obj = new GameObject(collisionBox);

    obj->add_component<Renderable>()->color = Color::Red;
    obj->add_component<Selectable>();
    auto* movable = obj->add_component<Movable>();
    movable->speed = 80.0f;
    obj->add_component<UnitType>()->category = UnitCategory::Villager;

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}