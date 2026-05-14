#include "factories.h"

void ObjectFactory::init(SDL_Renderer* renderer, TTF_Font* font, GameMap* map)
{
    this->renderer = renderer;
    for (auto& [type, name] : resourceNames) {
        // 根据资源类型决定背景尺寸（像素）
        int cells = 0;
        switch (type) {
        case ResourceEntityType::Wood: cells = 2; break;
        case ResourceEntityType::SGold: cells = 10; break;
        case ResourceEntityType::LGold: cells = 15; break;
        case ResourceEntityType::Stone: cells = 10; break;
        case ResourceEntityType::Berries: cells = 4; break;
        default: continue;
        }
        int texSize = cells * 10; // 假设 cell_size=10，纹理像素尺寸与实体尺寸一致
        SDL_Texture* tex = create_text_texture(name,
            { 40, 40, 40, 255 },  // 深灰背景
            { 255, 255, 255, 255 }, // 白色文字
            texSize);
        resourceTextures[type] = tex;
    }
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

GameObject* ObjectFactory::create_resource(ResourceEntityType type, int grid_x, int grid_y, const GameMap* map)
{
    int cell_size = map->get_cell_size();
    int size_cells = 0;
    int health = 100;
    ResourceType output = ResourceType::Wood;
    Color color;

    switch (type) {
    case ResourceEntityType::Wood:
        size_cells = 2;   // 2×2格
        health = 100;
        output = ResourceType::Wood;
        color = Color::Green;
        break;
    case ResourceEntityType::SGold:
        size_cells = 10;  // 10×10格
        health = 3000;
        output = ResourceType::Gold;
        color = Color::Yellow;
        break;
    case ResourceEntityType::LGold:
        size_cells = 15;  // 15×15格
        health = 8000;
        output = ResourceType::Gold;
        color = Color::Yellow;
        break;
    case ResourceEntityType::Stone:
        size_cells = 10;  // 10×10格
        health = 3000;
        output = ResourceType::Stone;
        color = Color::Gray;
        break;
    case ResourceEntityType::Berries:
        size_cells = 4;   // 4×4格
        health = 50;
        output = ResourceType::Food;
        color = Color::Orange;
        break;
    default: return nullptr;
    }

    float w = (float)(size_cells * cell_size);
    float h = (float)(size_cells * cell_size);
    float x = (float)(grid_x * cell_size);
    float y = (float)(grid_y * cell_size);

    CollisionBox box{ {x, y}, w, h };
    auto* obj = new GameObject(box);

    auto* render = obj->add_component<Renderable>();
    render->color = color;

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