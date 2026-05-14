#ifndef _RESOURCES_MGR_H_
#define _RESOURCES_MGR_H_
#include "resources_type.h"
#include <vector>
#include <functional>

// 每个玩家的资源袋
struct ResourceBag {
    int amounts[static_cast<int>(ResourceType::Count)] = { 0 };

    int& operator[](ResourceType type) {
        return amounts[static_cast<int>(type)];
    }
    const int& operator[](ResourceType type) const {
        return amounts[static_cast<int>(type)];
    }
};

class ResourcesMgr {
public:
    static ResourcesMgr* instance();

    // 初始化玩家数量（可在进入游戏时调用）
    void init(int player_count = 1);

    // 资源读写
    int get_resource(int player_id, ResourceType type) const;
    void set_resource(int player_id, ResourceType type, int amount);

    // 安全扣除，返回是否成功
    bool spend_resource(int player_id, ResourceType type, int amount);
    // 增加资源
    void add_resource(int player_id, ResourceType type, int amount);

    // 资源变动事件（供 UI 订阅）
    void set_on_resource_changed(std::function<void(int player_id, ResourceType type, int new_value)> callback) { on_resource_changed = callback; }

    // 获取当前玩家 ID（可扩展为根据视角或控制方返回）
    int get_local_player_id() const { return local_player_id; }
    void set_local_player_id(int id) { local_player_id = id; }

    // 设置某个玩家关注的资源类型（通常在创建玩家或阵营时调用一次）
    void set_player_resource_types(int player_id, const std::vector<ResourceType>& types);
    // 获取某个玩家关注的资源类型列表
    const std::vector<ResourceType>& get_player_resource_types(int player_id) const;

private:
    ResourcesMgr() = default;
    std::vector<ResourceBag> player_resources;   // 按玩家 ID 索引
    std::vector<std::vector<ResourceType>> player_resource_types;
    int local_player_id = -1;                     // 当前玩家
    std::function<void(int player_id, ResourceType type, int new_value)> on_resource_changed;
};


#endif // !_RESOURCES_MGR_H_
