#ifndef _ATTACK_SYSTEM_H_
#define _ATTACK_SYSTEM_H_

#include "factories.h"

class AttackSystem {
public:
    void on_update(float delta);

    void set_factory(ObjectFactory* factory);

private:
    ObjectFactory* factory;
};

#endif // !_ATTACK_SYSTEM_H_
