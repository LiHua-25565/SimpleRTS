#ifndef _GAME_OBJECT_H_
#define _GAME_OBJECT_H_

#include "vector2.h"
#include "collision_box.h"
#include "components.h"

#include <unordered_map>
#include <typeindex>
#include <memory>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

class GameObject
{
public:
    ~GameObject() = default;
    GameObject() = default;

    GameObject(const CollisionBox& collision_box):
        collision_box(collision_box){};
    
    template<typename T>
    T* get_component() {
        auto it = components.find(typeid(T));
        if (it != components.end())
            return static_cast<T*>(it->second.get());
        return nullptr;
    }

    // const 版本：返回只读指针
    template<typename T>
    const T* get_component() const {
        auto it = components.find(typeid(T));
        if (it != components.end())
            return static_cast<const T*>(it->second.get());
        return nullptr;
    }

    template<typename T, typename... Args>
    T* add_component(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = comp.get();
        components[typeid(T)] = std::move(comp);
        return ptr;
    }

    template<typename T>
    void remove_component() {
        components.erase(typeid(T));
    }

    void set_collision_box(const CollisionBox& collision_box)
    {
        this->collision_box = collision_box;
        is_dirty = true;
    }

    const CollisionBox& get_collision_box() const
    {
        return collision_box;
    }

    bool check_dirty() const
    {
        return is_dirty;
    }

    void clear_dirty()
    {
        is_dirty = false;
    }

    const uint64_t get_id() const
    {
        return id;
    }

    void set_id(uint64_t id)
    {
        this->id = id;
    }

    bool check_valid() const
    {
        return is_valid;
    }

    void set_valid(bool flag)
    {
        is_valid = flag;
    }

protected:
    uint64_t id = 0;
    CollisionBox collision_box;
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;

    bool is_valid = true;
    bool is_dirty = true;       // 在四叉树中是否需要更新
};

#endif // !_GAME_OBJECT_H_