#include "factories.h"
#include "texture_cache.h"

void ObjectFactory::init(GameMap* map) {

    this->map = map;
}

GameObject* ObjectFactory::create_resource(ResourceEntityType type, int grid_x, int grid_y)
{
    if (!map) return nullptr;

    int cell_size = map->get_cell_size();
    int size_cells = 0;
    int health = 100;
    ResourceType output = ResourceType::Wood;
    // 颜色不再需要，纹理会覆盖，但保留为后备
    Color color;

    switch (type) {
    case ResourceEntityType::Wood:    size_cells = 4;  health = 100;  output = ResourceType::Wood;  color = Color::Green;  break;
    case ResourceEntityType::SGold:   size_cells = 10; health = 3000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::LGold:   size_cells = 15; health = 8000; output = ResourceType::Gold;  color = Color::Yellow; break;
    case ResourceEntityType::Stone:   size_cells = 10; health = 3000; output = ResourceType::Stone; color = Color::Gray;   break;
    case ResourceEntityType::Berries: size_cells = 6;  health = 50;   output = ResourceType::Food;  color = Color::Orange; break;
    default: return nullptr;
    }

    float w = (float)(size_cells * cell_size);
    float h = (float)(size_cells * cell_size);
    float x = (float)(grid_x * cell_size);
    float y = (float)(grid_y * cell_size);

    CollisionBox box{ {x, y}, w, h };
    auto* obj = new GameObject(box);

    // 渲染组件：优先使用纹理
    auto* render = obj->add_component<Renderable>();
    SDL_Texture* tex = TextureCache::instance()->get_resource_texture(
        type, currentPlayerId, (int)w, (int)h);
    if (tex) {
        render->texture = tex;
        render->color = Color::White;   // 纹理原色
    }
    else {
        render->color = color;          // 降级纯色
    }

    auto* harvestable = obj->add_component<Harvestable>();
    harvestable->entity_type = type;
    harvestable->output_type = output;

    auto* health_comp = obj->add_component<Health>();
    health_comp->max_health = health;
    health_comp->current_health = health;

    obj->add_component<Selectable>();
    // 所有权组件（中立 playerId=0）
    auto* ownership = obj->add_component<Ownership>();
    ownership->playerId = currentPlayerId;
    ownership->teamId = 0;   // 中立 teamId=0

    WorldEntityMgr::instance()->insert_object(obj);
    return obj;
}

GameObject* ObjectFactory::create_unit(const CollisionBox& collision_box)
{
	auto* obj = new GameObject(collision_box);

	obj->add_component<Renderable>()->color = Color::Red;
	obj->add_component<Selectable>();
	obj->add_component<Movable>();
	obj->add_component<UnitType>()->category = UnitCategory::Villager;

	WorldEntityMgr::instance()->insert_object(obj);
	return obj;
}
