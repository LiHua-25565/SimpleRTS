#ifndef _DAMAGE_PIPELINE_H_
#define _DAMAGE_PIPELINE_H_

#include "components.h"
#include <vector>
#include <memory>

class GameObject;
class DamageModifier {
public:
    virtual ~DamageModifier() = default;
    virtual int process(int incoming_damage, const GameObject* attacker, const GameObject* target) = 0;
};

class DamagePipeline {
public:
    void add_modifier(std::unique_ptr<DamageModifier> modifier);
    int calculate(int base_damage, const GameObject* attacker, const GameObject* target);
private:
    std::vector<std::unique_ptr<DamageModifier>> modifiers;
};

class ArmorModifier : public DamageModifier {
public:
    int process(int incoming_damage, const GameObject* attacker, const GameObject* target) override;
};

#endif // !_DAMAGE_PIPELINE_H_