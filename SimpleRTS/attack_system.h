#ifndef _ATTACK_SYSTEM_H_
#define _ATTACK_SYSTEM_H_

#include "factories.h"
#include "damage_pipeline.h"

class AttackSystem {
public:
    AttackSystem();

    void on_update(float delta);

    void set_factory(ObjectFactory* factory);

private:
    ObjectFactory* factory;
    DamagePipeline damage_pipeline;
};

#endif // !_ATTACK_SYSTEM_H_
