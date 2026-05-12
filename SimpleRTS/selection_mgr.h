#ifndef _SELECTION_MGR_H_
#define _SELECTION_MGR_H_

#include <vector>
#include <unordered_set>
#include "game_object.h"

class SelectionMgr
{
public:
    enum class SelectMode
    {
        Normal,   // 普通：清空重选
        Add,      // Ctrl：加选
        Remove    // Alt：减选
    };

public:
    static SelectionMgr* instance();

    void set_select_mode(SelectMode mode);

    const SelectMode& get_current_mode() const;

    void select_in_area(const CollisionBox& world_area);

    bool select_at_point(const Vector2& world_pos);

    void select_single(GameObject* obj);

    void clear();
	
    const std::unordered_set<uint64_t>& get_selected_object_id_set() const
    {
        return selected_object_id_set;
    }

private:
    SelectionMgr();
    ~SelectionMgr();

    SelectMode current_mode = SelectMode::Normal;
	std::unordered_set<uint64_t> selected_object_id_set;
};

#endif // !_SELECTION_MGR_H_

