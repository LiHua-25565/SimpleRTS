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

	for (auto& bag : player_resources) {
		bag[ResourceType::Wood] = 200;
		bag[ResourceType::Food] = 200;
		bag[ResourceType::Gold] = 100;
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