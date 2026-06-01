#ifndef _PRODUCTION_SYSTEM_H_
#define _PRODUCTION_SYSTEM_H_

#include "game_object.h"
#include "factories.h"

class ProductionSystem {
public:
    void set_factory(ObjectFactory* factory) { this->factory = factory; }
    void on_update(float delta);

private:
    ObjectFactory* factory = nullptr;

    // 在建筑周围寻找空位生成单位
    bool try_spawn_unit(GameObject* building, UnitEntityType type);
};

#endif // _PRODUCTION_SYSTEM_H_