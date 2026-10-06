#include "map_io.h"
#include <fstream>
#include <sstream>

// .srmap 文本格式（版本 1）：
//   第 1 行：  MYSTRMAP 1
//   第 2 行：  W <width>
//   第 3 行：  H <height>
//   第 4 行：  C <cell_size>
//   第 5 行：  P <player_count>        （可选，旧文件无此行，载入时默认 2）
//   随后 height 行，每行 width 个字符：'L'=陆地(Mud) / 'W'=水域(Water)
//   之后每条实体一行（直到 EOF）：
//     R <type> <gx> <gy> <health>       资源
//     B <type> <gx> <gy> <player>       建筑
//     U <type> <wx> <wy> <player>       单位
static const std::string MAGIC = "MYSTRMAP";
static const int VERSION = 1;

bool save_map(const std::string& path, const GameMap& map, const std::vector<EntityRecord>& entities, int player_count) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    int w = map.get_width();
    int h = map.get_height();
    int c = map.get_cell_size();

    out << MAGIC << " " << VERSION << "\n";
    out << "W " << w << "\n";
    out << "H " << h << "\n";
    out << "C " << c << "\n";
    out << "P " << player_count << "\n";

    const auto& grid = map.get_grid();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            out << (grid[y][x] == TerrainType::Water ? 'W' : 'L');
        out << "\n";
    }

    for (const auto& e : entities) {
        switch (e.kind) {
        case EntityRecord::Kind::Resource:
            out << "R " << e.type << " " << e.gx << " " << e.gy << " " << e.health << "\n";
            break;
        case EntityRecord::Kind::Building:
            out << "B " << e.type << " " << e.gx << " " << e.gy << " " << e.player << "\n";
            break;
        case EntityRecord::Kind::Unit:
            out << "U " << e.type << " " << (int)e.wx << " " << (int)e.wy << " " << e.player << "\n";
            break;
        }
    }
    return true;
}

bool load_map(const std::string& path, GameMap& map, std::vector<EntityRecord>& entities, int& player_count) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::string line;
    if (!std::getline(in, line)) return false;
    {
        std::istringstream ls(line);
        std::string magic;
        int ver = 0;
        if (!(ls >> magic >> ver)) return false;
        if (magic != MAGIC) return false;
        (void)ver;
    }

    int w = 0, h = 0, c = 0;
    auto read_header = [&](char key, int& out) -> bool {
        if (!std::getline(in, line)) return false;
        std::istringstream ls(line);
        char k = 0;
        int v = 0;
        if (!(ls >> k >> v) || k != key) return false;
        out = v;
        return true;
        };
    if (!read_header('W', w)) return false;
    if (!read_header('H', h)) return false;
    if (!read_header('C', c)) return false;
    if (w <= 0 || h <= 0 || c <= 0) return false;
    if (w != map.get_width() || h != map.get_height()) return false;

    // 可选玩家数量行：P <count>（旧格式无此行，默认 2）
    player_count = 2;
    std::string first;
    if (!std::getline(in, first)) return false;
    if (first.rfind("P ", 0) == 0) {
        std::istringstream ls(first);
        char k = 0; int v = 0;
        if ((ls >> k >> v) && k == 'P' && v >= 1) player_count = v;
        if (!std::getline(in, line)) return false;   // 继续读第一行地形
    }
    else {
        line = first;   // 该行就是第一行地形
    }

    for (int y = 0; y < h; ++y) {
        if (y > 0 && !std::getline(in, line)) return false;
        for (int x = 0; x < w && x < (int)line.size(); ++x)
            map.set_grid_by_pos(x, y, (line[x] == 'W') ? TerrainType::Water : TerrainType::Mud);
    }
    map.rebuild_static_obstacle_field();

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream es(line);
        char k = 0;
        if (!(es >> k)) continue;

        EntityRecord r;
        if (k == 'R') {
            int t = 0, gx = 0, gy = 0, hp = 0;
            if (!(es >> t >> gx >> gy >> hp)) continue;
            r.kind = EntityRecord::Kind::Resource;
            r.type = t; r.gx = gx; r.gy = gy; r.health = hp;
        }
        else if (k == 'B') {
            int t = 0, gx = 0, gy = 0, p = 0;
            if (!(es >> t >> gx >> gy >> p)) continue;
            r.kind = EntityRecord::Kind::Building;
            r.type = t; r.gx = gx; r.gy = gy; r.player = p;
        }
        else if (k == 'U') {
            int t = 0, p = 0;
            float wx = 0, wy = 0;
            if (!(es >> t >> wx >> wy >> p)) continue;
            r.kind = EntityRecord::Kind::Unit;
            r.type = t; r.wx = wx; r.wy = wy; r.player = p;
        }
        else {
            continue;
        }
        entities.push_back(r);
    }

    return true;
}
