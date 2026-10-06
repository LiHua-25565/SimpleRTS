#include "ai_system.h"
#include "world_entity_mgr.h"
#include "components.h"
#include "resources_mgr.h"
#include "util.h"
#include "production_data.h"
#include "factories.h"
#include <algorithm>
#include <cmath>

// ======================================================================
// 电脑玩家 AI —— 命令层实现
//
// 所有决策只“写命令”（Movable 目标 / Gatherer 任务 / Attack 目标 /
// ProductionQueue 入队），具体执行完全复用现有的移动/采集/攻击/生产系统，
// 与人类输入走同一条路，因此不会与拥堵自愈、RVO、波次判定等已有机制冲突。
// ======================================================================

void AISystem::add_ai_player(int player_id, const Vector2& rally_point)
{
    PlayerState st;
    st.player_id = player_id;
    st.team_id   = ResourcesMgr::instance()->get_team_id(player_id);
    st.rally     = rally_point;
    st.threshold = config_.wave_threshold;
    st.decide_timer = (float)(player_id % 4) * 0.05f;   // 错峰决策
    players_[player_id] = st;
}

void AISystem::reset()
{
    players_.clear();
    attack_signals_.clear();
    defense_signals_.clear();
}

const AISystem::TeamSignal& AISystem::get_attack_signal(int team_id) const
{
    static const TeamSignal empty;
    auto it = attack_signals_.find(team_id);
    return it != attack_signals_.end() ? it->second : empty;
}

const AISystem::TeamSignal& AISystem::get_defense_signal(int team_id) const
{
    static const TeamSignal empty;
    auto it = defense_signals_.find(team_id);
    return it != defense_signals_.end() ? it->second : empty;
}

int AISystem::get_phase(int player_id) const
{
    auto it = players_.find(player_id);
    if (it == players_.end()) return -1;
    return static_cast<int>(it->second.phase);
}

int AISystem::get_wave_number(int player_id) const
{
    auto it = players_.find(player_id);
    return it == players_.end() ? -1 : it->second.wave_number;
}

int AISystem::count_military(int player_id) const { return count_military_internal(player_id); }
int AISystem::count_villagers(int player_id) const { return count_villagers_internal(player_id); }

// ============================ 主循环 ============================

void AISystem::on_update(float delta)
{
    for (auto& [pid, st] : players_)
    {
        st.decide_timer += delta;
        st.harass_timer  -= delta;
        st.build_retry   -= delta;
        if (st.decide_timer >= config_.decision_interval)
        {
            st.decide_timer = 0.0f;
            run_decision(st);
        }
    }
    for (auto& [tid, sig] : attack_signals_)
    {
        sig.time_left -= delta;
        if (sig.time_left <= 0.0f) sig.active = false;
    }
    for (auto& [tid, sig] : defense_signals_)
    {
        sig.time_left -= delta;
        if (sig.time_left <= 0.0f) sig.active = false;
    }
}

void AISystem::run_decision(PlayerState& st)
{
    cleanup_groups(st);
    assign_idle_villagers(st);
    manage_production(st);
    manage_buildings(st);
    detect_threats_and_defend(st);
    update_wave(st);
    command_military(st);
    launch_harass(st);
}

// ============================ 经济 ============================

// 分配闲置农民：按 食物(3) → 金(4) → 木(其余) 的目标配比，
// 每人挑离自己最近的对应资源，站位点走周界分配（复用采集拥堵治理）。
//
// 发呆修复的要点：
// 1) 首选资源类型采空了，就退而求其次采其它还有矿的类型（食物→金→木），
//    而不是原地 continue 让农民永久闲置（此前只在“不是木”时才回退到木，
//    木采空时农民就彻底罢工了）。
// 2) 全图都没矿可采时，把农民叫回基地集结区待命——发呆也要发在家里，
//    不要站在采空的资源坑旁一动不动。
void AISystem::assign_idle_villagers(PlayerState& st)
{
    // 统计当前各类资源上的农民数（只统计目标资源仍然存活的）
    int on_food = 0, on_gold = 0, on_wood = 0;
    for (auto* v : villagers_of(st.player_id))
    {
        int t = gatherer_target_type(v);
        if (t == (int)ResourceType::Food) ++on_food;
        else if (t == (int)ResourceType::Gold) ++on_gold;
        else if (t == (int)ResourceType::Wood) ++on_wood;
    }

    // 选矿偏好：优先选血量还够一车（40）的矿，避免把农民派去马上
    // 要采空的矿上，人到矿没了又白跑一趟（来回空跑看起来就像发呆）
    auto pick_resource = [this](const Vector2& c, ResourceType t) -> GameObject* {
        GameObject* healthy = nullptr;   float healthy_d = 1e9f;
        GameObject* fallback = nullptr;  float fallback_d = 1e9f;
        auto& pool = WorldEntityMgr::instance()->get_object_pool();
        for (auto& [id, o] : pool)
        {
            if (!o->check_valid()) continue;
            auto* h = o->get_component<Harvestable>();
            if (!h || h->output_type != t) continue;
            auto* hp = o->get_component<Health>();
            if (!hp || hp->current_health <= 0) continue;
            float d = (o->get_collision_box().get_center_position() - c).length();
            if (hp->current_health >= 40)
            {
                if (d < healthy_d) { healthy_d = d; healthy = o; }
            }
            else if (d < fallback_d)
            {
                fallback_d = d; fallback = o;
            }
        }
        return healthy ? healthy : fallback;
    };

    // 每类资源当前是否还有存活矿（本决策周期只查一次）
    const bool has_food = pick_resource(st.rally, ResourceType::Food) != nullptr;
    const bool has_gold = pick_resource(st.rally, ResourceType::Gold) != nullptr;
    const bool has_wood = pick_resource(st.rally, ResourceType::Wood) != nullptr;

    for (auto* v : villagers_of(st.player_id))
    {
        auto* g = v->get_component<Gatherer>();
        if (!g || g->target_resource_id != 0 || g->carried_amount > 0 || g->dropoff_target_id != 0)
            continue;

        // 需要哪一类资源：优先补不足的配额；配额类型已枯竭时直接跳到下一优先类型
        ResourceType want = ResourceType::Wood;
        if (on_food < config_.food_villagers && has_food) want = ResourceType::Food;
        else if (on_gold < config_.gold_villagers && has_gold) want = ResourceType::Gold;
        else if (!has_wood)
        {
            if (has_food) want = ResourceType::Food;
            else if (has_gold) want = ResourceType::Gold;
        }

        const Vector2 center = v->get_collision_box().get_center_position();
        GameObject* res = pick_resource(center, want);
        // 首选类型没找到活矿：按 食物→金→木 顺序找任一还活着的矿
        if (!res && want != ResourceType::Food) res = pick_resource(center, ResourceType::Food);
        if (!res && want != ResourceType::Gold) res = pick_resource(center, ResourceType::Gold);
        if (!res && want != ResourceType::Wood) res = pick_resource(center, ResourceType::Wood);

        if (!res)
        {
            // 全图无矿可采：回基地集结区待命，别站在野地里发呆
            auto* mv = v->get_component<Movable>();
            if (mv)
            {
                const Vector2 dest = st.rally + spread_offset(v->get_id());
                if ((dest - center).length() > 60.0f)
                {
                    mv->target = dest;
                    mv->flow_target = dest;
                }
            }
            continue;
        }

        // 占一个配额坑，避免一个决策周期内全员分去同一种资源
        if (want == ResourceType::Food) ++on_food;
        else if (want == ResourceType::Gold) ++on_gold;
        else ++on_wood;

        order_gather(v, res);
    }
}

void AISystem::order_gather(GameObject* u, GameObject* resource)
{
    auto* g = u->get_component<Gatherer>();
    auto* mv = u->get_component<Movable>();
    if (!g || !mv || !resource) return;
    g->target_resource_id = resource->get_id();
    g->stand_target = compute_perimeter_target(
        u->get_id(), u->get_collision_box().get_center_position(),
        u->get_collision_box(), resource->get_collision_box(), 2.0f);
    mv->target = g->stand_target;
    mv->flow_target = mv->target;
}

int AISystem::gatherer_target_type(GameObject* villager) const
{
    auto* g = villager->get_component<Gatherer>();
    if (!g || g->target_resource_id == 0) return -1;
    GameObject* res = WorldEntityMgr::instance()->get_object_by_id(g->target_resource_id);
    if (!res || !res->check_valid()) return -1;
    auto* h = res->get_component<Harvestable>();
    if (!h) return -1;
    auto* hp = res->get_component<Health>();
    if (hp && hp->current_health <= 0) return -1;   // 已采空的不计入配额
    return (int)h->output_type;
}

// 生产：农民补到目标数；黄金富裕就持续出兵到常备军上限
void AISystem::manage_production(PlayerState& st)
{
    GameObject* tc = own_town_center(st.player_id);
    if (!tc) return;
    auto* q = tc->get_component<ProductionQueue>();
    if (!q) q = tc->add_component<ProductionQueue>();

    auto& rm = *ResourcesMgr::instance();
    const int food = rm.get_resource(st.player_id, ResourceType::Food);
    const int gold = rm.get_resource(st.player_id, ResourceType::Gold);

    // 队列里已有的单位数（含正在生产）
    int queued_villagers = 0, queued_military = 0;
    for (const auto& e : q->queue)
    {
        if (e.type != ProductionType::Unit) continue;
        if (e.unit_type == UnitEntityType::Villager) ++queued_villagers;
        else ++queued_military;
    }
    GameObject* ar = own_archery_range(st.player_id);
    if (ar)
    {
        auto* aq = ar->get_component<ProductionQueue>();
        if (aq) for (const auto& e : aq->queue)
            if (e.type == ProductionType::Unit && e.unit_type != UnitEntityType::Villager) ++queued_military;
    }

    // 1) 补农民（城镇中心队列最多压 2 个）
    if (count_villagers_internal(st.player_id) + queued_villagers < config_.target_villagers &&
        q->queue.size() < 2 && food >= 10)
    {
        enqueue_unit(tc, UnitEntityType::Villager, 5.0f, 10, 0, st.player_id);
    }

    // 2) 出兵：优先靶场，其次城镇中心
    if (count_military_internal(st.player_id) + queued_military < config_.max_army && gold >= 15)
    {
        GameObject* producer = ar;
        if (producer)
        {
            auto* aq = producer->get_component<ProductionQueue>();
            if (aq && aq->queue.size() < 3)
                enqueue_unit(producer, UnitEntityType::Archer, 8.0f, 0, 15, st.player_id);
        }
        else if (q->queue.size() < 2)
        {
            enqueue_unit(tc, UnitEntityType::Archer, 8.0f, 0, 15, st.player_id);
        }
    }
}

void AISystem::enqueue_unit(GameObject* building, UnitEntityType type, float time,
    int food_cost, int gold_cost, int player_id)
{
    auto& rm = *ResourcesMgr::instance();
    if (food_cost > 0 && rm.get_resource(player_id, ResourceType::Food) < food_cost) return;
    if (gold_cost > 0 && rm.get_resource(player_id, ResourceType::Gold) < gold_cost) return;
    if (food_cost > 0 && !rm.spend_resource(player_id, ResourceType::Food, food_cost)) return;
    if (gold_cost > 0 && !rm.spend_resource(player_id, ResourceType::Gold, gold_cost)) return;

    auto* q = building->get_component<ProductionQueue>();
    if (!q) q = building->add_component<ProductionQueue>();

    ProductionQueue::QueueEntry entry;
    entry.type = ProductionType::Unit;
    entry.unit_type = type;
    entry.total_time = time;
    entry.cost_amounts[(int)ResourceType::Food] = food_cost;
    entry.cost_amounts[(int)ResourceType::Gold] = gold_cost;
    q->queue.push_back(entry);
}

// 铺建筑：主基地旁环形搜索一块 20x20 空地放靶场
void AISystem::manage_buildings(PlayerState& st)
{
    if (st.has_archery_range || !factory_) return;
    auto& rm = *ResourcesMgr::instance();
    if (rm.get_resource(st.player_id, ResourceType::Wood) < 150 ||
        rm.get_resource(st.player_id, ResourceType::Gold) < 50) return;
    if (st.build_retry > 0.0f) return;
    st.build_retry = 3.0f;

    if (try_place_building(st, BuildingEntityType::ArcheryRange))
        st.has_archery_range = true;
}

bool AISystem::try_place_building(PlayerState& st, BuildingEntityType type)
{
    if (!factory_) return false;
    GameMap* map = WorldEntityMgr::instance()->get_map();
    if (!map) return false;
    const int cs = map->get_cell_size();
    const int cx = (int)(st.rally.x / cs), cy = (int)(st.rally.y / cs);
    const int w = map->get_width(), h = map->get_height();

    factory_->set_player_id(st.player_id);
    for (int r = 2; r <= 16; ++r)
    {
        for (int gx = cx - r; gx <= cx + r; ++gx)
        {
            for (int gy = cy - r; gy <= cy + r; ++gy)
            {
                if (std::max(std::abs(gx - cx), std::abs(gy - cy)) != r) continue;  // 只扫环
                if (gx < 0 || gy < 0 || gx >= w || gy >= h) continue;
                GameObject* b = factory_->create_building_by_type(type, gx, gy, false);
                if (b)
                {
                    auto& rm = *ResourcesMgr::instance();
                    const ProductionItem* item = get_build_item(type);
                    int wood = item ? item->cost_amounts[(int)ResourceType::Wood] : 0;
                    int gold = item ? item->cost_amounts[(int)ResourceType::Gold] : 0;
                    if (wood > 0) rm.spend_resource(st.player_id, ResourceType::Wood, wood);
                    if (gold > 0) rm.spend_resource(st.player_id, ResourceType::Gold, gold);
                    return true;
                }
            }
        }
    }
    return false;
}

// ============================ 军事 ============================

// 基地威胁检测 + 协防：任一队友（含人类）基地旁出现敌军就拉响队伍防御信号，
// 并派出 defend_group 人前往威胁点
void AISystem::detect_threats_and_defend(PlayerState& st)
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();

    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* bt = obj->get_component<BuildingType>();
        if (!bt || bt->type != BuildingEntityType::TownCenter) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->team_id != st.team_id) continue;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;

        Vector2 tc_center = obj->get_collision_box().get_center_position();
        GameObject* intruder = nearest_enemy_military(tc_center, st.team_id, config_.threat_radius);
        if (!intruder) continue;

        // 拉响全队防御信号（威胁位置 = 受袭基地）
        set_defense_signal(st.team_id, tc_center, 5.0f);

        // 派出协防：进攻中抽 2 人，平时抽满编
        int want = (st.phase == PlayerState::Phase::Attacking)
            ? std::max(2, config_.defend_group / 2) : config_.defend_group;
        std::vector<GameObject*> army = military_of(st.player_id);
        int sent = 0;
        for (auto* u : army)
        {
            if (sent >= want) break;
            if (st.defenders.count(u->get_id())) continue;
            st.defenders.insert(u->get_id());
            st.defend_dest = tc_center;
            ++sent;
        }
        if (sent > 0) st.harassers.clear();   // 协防优先于骚扰
        return;                               // 一次处理一个威胁
    }

    // 威胁解除：解散协防队
    st.defenders.clear();
}

// 波次状态机
void AISystem::update_wave(PlayerState& st)
{
    const float dt = config_.decision_interval;
    st.phase_time += dt;

    if (st.phase == PlayerState::Phase::Massing)
    {
        int army = count_military_internal(st.player_id) - (int)st.defenders.size();

        // 协同进攻：队友已拉响进攻信号且目标不是我
        const TeamSignal& sig = get_attack_signal(st.team_id);
        if (sig.active && sig.target_player >= 0 && sig.target_player != st.player_id &&
            army >= config_.join_min_army)
        {
            begin_attack(st, sig.target_player);
            return;
        }

        // 主动进攻：攒够本波阈值且过了开局宽限期
        if (army >= st.threshold && st.phase_time >= config_.first_wave_grace)
        {
            int target = -1;
            if (!find_enemy_base(st.team_id, &target)) return;   // 敌人已被消灭
            begin_attack(st, target);
        }
    }
    else if (st.phase == PlayerState::Phase::Attacking)
    {
        int army = count_military_internal(st.player_id);
        int floor = std::max(2, (int)(st.army_at_attack_start * config_.retreat_ratio));
        if (army <= floor)
        {
            st.phase = PlayerState::Phase::Cooldown;
            st.phase_time = 0.0f;
        }
        else if (st.phase_time > config_.attack_timeout)
        {
            st.phase = PlayerState::Phase::Cooldown;
            st.phase_time = 0.0f;
        }
    }
    else // Cooldown
    {
        if (st.phase_time >= config_.reattack_cooldown)
        {
            st.phase = PlayerState::Phase::Massing;
            st.phase_time = 0.0f;
        }
    }
}

void AISystem::begin_attack(PlayerState& st, int target_player)
{
    st.phase = PlayerState::Phase::Attacking;
    st.phase_time = 0.0f;
    st.target_player = target_player;
    st.army_at_attack_start = count_military_internal(st.player_id);
    ++st.wave_number;
    st.threshold = std::min(config_.wave_threshold + st.wave_number * config_.wave_growth,
                            config_.max_army);
    st.harassers.clear();                       // 骚扰小队归队参战
    set_attack_signal(st.team_id, target_player, 8.0f);
}

// 单位指挥：每 0.25s 给全军写一遍“微操”命令
void AISystem::command_military(PlayerState& st)
{
    Vector2 base_center = st.rally;
    GameObject* enemy_base = find_enemy_base(st.team_id, nullptr);

    for (GameObject* u : military_of(st.player_id))
    {
        const Vector2 center = u->get_collision_box().get_center_position();
        auto* attack = u->get_component<Attack>();
        auto* mv = u->get_component<Movable>();
        if (!attack || !mv) continue;

        // 1) 优先打人：射程内有敌就扑上去
        GameObject* target = nearest_enemy_unit(center, st.team_id, config_.engage_radius);
        if (!target) target = nearest_enemy_building(center, st.team_id, config_.engage_radius);

        if (target)
        {
            attack->target_id = target->get_id();
            attack->auto_attack = false;
            Vector2 t_center = target->get_collision_box().get_center_position();
            if (attack->is_ranged)
                mv->target = compute_ranged_outer_target(center, t_center,
                    u->get_collision_box(), target->get_collision_box(), attack->range, 5.0f);
            else
                mv->target = compute_outer_target(center, t_center,
                    u->get_collision_box(), target->get_collision_box(), 5.0f);
            mv->flow_target = mv->target;
            continue;
        }

        attack->target_id = 0;
        attack->auto_attack = false;

        // 2) 没有眼前目标：按当前角色决定去向
        if (st.defenders.count(u->get_id()))
        {
            if ((st.defend_dest - center).length() > 60.0f)
                order_attack_move(u, st.defend_dest);
        }
        else if (st.harassers.count(u->get_id()))
        {
            Vector2 har = enemy_resource_area(st.team_id);
            if ((har - center).length() > 60.0f)
                order_attack_move(u, har);
        }
        else if (st.phase == PlayerState::Phase::Attacking && enemy_base)
        {
            Vector2 dest = enemy_base->get_collision_box().get_center_position()
                + spread_offset(u->get_id());
            if ((dest - center).length() > 80.0f)
                order_attack_move(u, dest);
        }
        else
        {
            // 积攒/冷却：驻守基地（带散开偏移），顺便就地迎敌
            Vector2 dest = base_center + spread_offset(u->get_id());
            if ((dest - center).length() > 150.0f)
                order_attack_move(u, dest);
        }
    }
}

void AISystem::order_attack_move(GameObject* u, const Vector2& dest)
{
    auto* mv = u->get_component<Movable>();
    if (!mv) return;
    mv->target = dest;
    mv->flow_target = dest;
    if (auto* at = u->get_component<Attack>())
    {
        at->target_id = 0;
        at->auto_attack = false;
    }
}

// 骚扰：第二波之后、攒兵阶段、兵力富余时，派小队去敌方资源区袭扰
void AISystem::launch_harass(PlayerState& st)
{
    if (st.phase != PlayerState::Phase::Massing || st.wave_number < 1) return;
    if (st.harass_timer > 0.0f) return;
    if (count_military_internal(st.player_id) - (int)st.defenders.size() < 4) return;

    Vector2 dest = enemy_resource_area(st.team_id);
    std::vector<GameObject*> army = military_of(st.player_id);
    std::sort(army.begin(), army.end(), [&](GameObject* a, GameObject* b) {
        float da = (dest - a->get_collision_box().get_center_position()).length();
        float db = (dest - b->get_collision_box().get_center_position()).length();
        return da < db;
        });

    int sent = 0;
    for (auto* u : army)
    {
        if (sent >= config_.harass_group) break;
        if (st.defenders.count(u->get_id())) continue;
        st.harassers.insert(u->get_id());
        ++sent;
    }
    if (sent > 0) st.harass_timer = config_.harass_interval;
}

void AISystem::cleanup_groups(PlayerState& st)
{
    for (auto it = st.harassers.begin(); it != st.harassers.end(); )
    {
        GameObject* u = WorldEntityMgr::instance()->get_object_by_id(*it);
        if (!u || !u->check_valid()) it = st.harassers.erase(it);
        else ++it;
    }
    for (auto it = st.defenders.begin(); it != st.defenders.end(); )
    {
        GameObject* u = WorldEntityMgr::instance()->get_object_by_id(*it);
        if (!u || !u->check_valid()) it = st.defenders.erase(it);
        else ++it;
    }
}

// ============================ 工具 ============================

std::vector<GameObject*> AISystem::military_of(int player_id)
{
    std::vector<GameObject*> out;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        auto* at = obj->get_component<Attack>();
        if (!at || obj->get_component<Gatherer>()) continue;   // 农民不算军事单位
        out.push_back(obj);
    }
    return out;
}

std::vector<GameObject*> AISystem::villagers_of(int player_id)
{
    std::vector<GameObject*> out;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Gatherer>()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        out.push_back(obj);
    }
    return out;
}

std::vector<GameObject*> AISystem::buildings_of(int player_id)
{
    std::vector<GameObject*> out;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Structure>()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        out.push_back(obj);
    }
    return out;
}

int AISystem::count_military_internal(int player_id) const
{
    int n = 0;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (obj->get_component<Projectile>()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->player_id != player_id) continue;
        auto* at = obj->get_component<Attack>();
        if (at && !obj->get_component<Gatherer>()) ++n;
    }
    return n;
}

int AISystem::count_villagers_internal(int player_id) const
{
    int n = 0;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Gatherer>()) continue;
        auto* own = obj->get_component<Ownership>();
        if (own && own->player_id == player_id) ++n;
    }
    return n;
}

bool AISystem::is_enemy(GameObject* o, int my_team) const
{
    auto* own = o->get_component<Ownership>();
    return own && own->team_id != my_team;
}

GameObject* AISystem::nearest_enemy_unit(const Vector2& center, int my_team,
    float max_dist, float* out_dist) const
{
    GameObject* best = nullptr;
    float best_d = max_dist;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid() || obj->get_component<Projectile>()) continue;
        if (!is_enemy(obj, my_team)) continue;
        if (!obj->get_component<Movable>()) continue;          // 只打能打的
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        float d = (obj->get_collision_box().get_center_position() - center).length();
        if (d < best_d) { best_d = d; best = obj; }
    }
    if (out_dist) *out_dist = best ? best_d : max_dist;
    return best;
}

GameObject* AISystem::nearest_enemy_building(const Vector2& center, int my_team, float max_dist) const
{
    GameObject* best = nullptr;
    float best_d = max_dist;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Structure>()) continue;
        if (!is_enemy(obj, my_team)) continue;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        float d = (obj->get_collision_box().get_center_position() - center).length();
        if (d < best_d) { best_d = d; best = obj; }
    }
    return best;
}

GameObject* AISystem::nearest_enemy_military(const Vector2& center, int my_team, float max_dist) const
{
    GameObject* best = nullptr;
    float best_d = max_dist;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid() || obj->get_component<Projectile>()) continue;
        if (!is_enemy(obj, my_team)) continue;
        auto* at = obj->get_component<Attack>();
        if (!at || obj->get_component<Gatherer>()) continue;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        float d = (obj->get_collision_box().get_center_position() - center).length();
        if (d < best_d) { best_d = d; best = obj; }
    }
    return best;
}

GameObject* AISystem::nearest_resource_of(const Vector2& center, ResourceType type) const
{
    GameObject* best = nullptr;
    float best_d = 1e9f;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* h = obj->get_component<Harvestable>();
        if (!h || h->output_type != type) continue;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        float d = (obj->get_collision_box().get_center_position() - center).length();
        if (d < best_d) { best_d = d; best = obj; }
    }
    return best;
}

GameObject* AISystem::find_enemy_base(int my_team, int* out_owner_player) const
{
    // 找“最弱”敌人的城镇中心（总实力 = 军事单位 + 建筑）
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    std::unordered_map<int, int> power;
    std::unordered_map<int, GameObject*> tc_of;
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* own = obj->get_component<Ownership>();
        if (!own || own->team_id == my_team) continue;
        int p = own->player_id;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        auto* bt = obj->get_component<BuildingType>();
        if (bt && bt->type == BuildingEntityType::TownCenter) tc_of[p] = obj;
        power[p] += 1;
    }
    if (tc_of.empty()) return nullptr;

    int best_p = -1, best_power = 1 << 30;
    for (auto& [p, o] : tc_of)
    {
        if (power[p] < best_power) { best_power = power[p]; best_p = p; }
    }
    if (out_owner_player) *out_owner_player = best_p;
    return tc_of[best_p];
}

Vector2 AISystem::enemy_resource_area(int my_team) const
{
    // 敌方基地旁的资源点（对手最可能派农民去的地方）
    int owner = -1;
    GameObject* base = find_enemy_base(my_team, &owner);
    if (!base) return { 0.0f, 0.0f };
    Vector2 bc = base->get_collision_box().get_center_position();

    GameObject* best_res = nullptr;
    float best_d = 1e9f;
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        if (!obj->get_component<Harvestable>()) continue;
        auto* hp = obj->get_component<Health>();
        if (hp && hp->current_health <= 0) continue;
        float d = (obj->get_collision_box().get_center_position() - bc).length();
        if (d < best_d) { best_d = d; best_res = obj; }
    }
    if (best_res) return best_res->get_collision_box().get_center_position();
    return bc;
}

GameObject* AISystem::own_town_center(int player_id) const
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* bt = obj->get_component<BuildingType>();
        if (!bt || bt->type != BuildingEntityType::TownCenter) continue;
        auto* own = obj->get_component<Ownership>();
        if (own && own->player_id == player_id) return obj;
    }
    return nullptr;
}

GameObject* AISystem::own_archery_range(int player_id) const
{
    auto& pool = WorldEntityMgr::instance()->get_object_pool();
    for (auto& [id, obj] : pool)
    {
        if (!obj->check_valid()) continue;
        auto* bt = obj->get_component<BuildingType>();
        if (!bt || bt->type != BuildingEntityType::ArcheryRange) continue;
        auto* own = obj->get_component<Ownership>();
        if (own && own->player_id == player_id) return obj;
    }
    return nullptr;
}

Vector2 AISystem::spread_offset(uint64_t id) const
{
    int k = (int)(id % 97);
    return { (float)((k % 7) - 3) * 36.0f, (float)((k / 7 % 7) - 3) * 36.0f };
}

void AISystem::set_attack_signal(int team_id, int target_player, float duration)
{
    TeamSignal& s = attack_signals_[team_id];
    s.active = true;
    s.time_left = duration;
    s.target_player = target_player;
}

void AISystem::set_defense_signal(int team_id, const Vector2& pos, float duration)
{
    TeamSignal& s = defense_signals_[team_id];
    s.active = true;
    s.time_left = duration;
    s.pos = pos;
}
