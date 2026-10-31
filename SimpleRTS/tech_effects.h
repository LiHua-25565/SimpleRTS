#ifndef _TECH_EFFECTS_H_
#define _TECH_EFFECTS_H_

#include <string>

class GameObject;
struct TechEffect;

// 把一条科技效果应用到单个单位（目标类型不匹配时自动忽略）。
void apply_tech_effect_to_unit(GameObject* obj, const TechEffect& effect);

// 研究完成时：把该科技的效果应用到玩家已有的单位。
void apply_tech_effects_to_player(int player_id, const std::string& tech_name);

// 新建单位时：把玩家已经完成的科技效果补到新单位上。
void apply_completed_techs_to_unit(GameObject* obj, int player_id);

#endif // !_TECH_EFFECTS_H_
