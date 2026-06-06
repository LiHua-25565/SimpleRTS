#include "resources_mgr.h"

ResourcesMgr* ResourcesMgr::instance()
{
	static ResourcesMgr mgr;
	return &mgr;
}

void ResourcesMgr::init(int player_count)
{
	player_resources.clear();
	player_resources.resize(player_count);
	player_resource_types.assign(player_count, {});   // 初始为空

	// 默认阵营（例如人类）的资源类型
	std::vector<ResourceType> default_types = {
		ResourceType::Gold, ResourceType::Wood,
		ResourceType::Food, ResourceType::Stone
	};
	for (int i = 0; i < player_count; ++i) {
		set_player_resource_types(i, default_types);
		// 初始资源数值
		auto& bag = player_resources[i];
		bag[ResourceType::Wood] = 20000;
		bag[ResourceType::Food] = 20000;
		bag[ResourceType::Gold] = 10000;
		bag[ResourceType::Stone] = 10000;
	}
}

int ResourcesMgr::get_resource(int player_id, ResourceType type) const
{
	if (player_id < 0 || player_id >= (int)player_resources.size()) return 0;
	return player_resources[player_id][type];
}

void ResourcesMgr::set_resource(int player_id, ResourceType type, int amount)
{
	if (player_id < 0 || player_id >= (int)player_resources.size()) return;
	player_resources[player_id][type] = amount;
	if (on_resource_changed)
		on_resource_changed(player_id, type, amount);
}

bool ResourcesMgr::spend_resource(int player_id, ResourceType type, int amount)
{
	if (player_id < 0 || player_id >= (int)player_resources.size()) return false;
	int& res = player_resources[player_id][type];
	if (res < amount) return false;
	res -= amount;
	if (on_resource_changed)
		on_resource_changed(player_id, type, res);
	return true;
}

void ResourcesMgr::add_resource(int player_id, ResourceType type, int amount)
{
	if (player_id < 0 || player_id >= (int)player_resources.size()) return;
	int& res = player_resources[player_id][type];
	res += amount;
	if (on_resource_changed)
		on_resource_changed(player_id, type, res);	
}

void ResourcesMgr::set_player_resource_types(int player_id, const std::vector<ResourceType>& types) {
	if (player_id < 0 || player_id >= (int)player_resource_types.size()) return;
	player_resource_types[player_id] = types;
}

const std::vector<ResourceType>& ResourcesMgr::get_player_resource_types(int player_id) const {
	static std::vector<ResourceType> empty;
	if (player_id < 0 || player_id >= (int)player_resource_types.size()) return empty;
	return player_resource_types[player_id];
}

void ResourcesMgr::set_player_team(int player_id, int team_id) {
	player_team_map[player_id] = team_id;
}

int ResourcesMgr::get_team_id(int player_id) const {
	auto it = player_team_map.find(player_id);
	if (it != player_team_map.end())
		return it->second;
	// 默认情况下，没有设置映射的队伍 ID = player_id
	return player_id;
}