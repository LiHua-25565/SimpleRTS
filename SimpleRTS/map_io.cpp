#include "map_io.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "building_type.h"

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

bool load_map_resize(const std::string& path, GameMap& map, std::vector<EntityRecord>& entities, int& player_count) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::string line;
    if (!std::getline(in, line)) return false;   // magic 行
    int w = 0, h = 0;
    auto read_kv = [&](char key, int& out) -> bool {
        if (!std::getline(in, line)) return false;
        std::istringstream ls(line);
        char k = 0; int v = 0;
        if (!(ls >> k >> v) || k != key) return false;
        out = v;
        return true;
        };
    if (!read_kv('W', w)) return false;
    if (!read_kv('H', h)) return false;
    if (w <= 0 || h <= 0) return false;
    in.close();

    map.resize(w, h);
    return load_map(path, map, entities, player_count);
}

std::vector<SpawnPoint> compute_spawn_points(const GameMap& map, const std::vector<EntityRecord>& entities, int player_count) {
    if (player_count < 1) player_count = 1;
    int w = map.get_width();
    int h = map.get_height();
    const int TC_CELLS = 20;   // 城镇中心占地格数

    // 检查以 (gx,gy) 为左上角的 TC_CELLS×TC_CELLS 城镇中心是否全为陆地且不越界
    auto tc_passable = [&](int gx, int gy) -> bool {
        for (int yy = gy; yy < gy + TC_CELLS; ++yy) {
            for (int xx = gx; xx < gx + TC_CELLS; ++xx) {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) return false;
                if (map.get_grid()[yy][xx] == TerrainType::Water) return false;
            }
        }
        return true;
    };

    // 默认点若落水，螺旋向外找最近的可放城镇中心的陆地位置
    auto fix_to_land = [&](int gx, int gy) -> std::pair<int, int> {
        if (tc_passable(gx, gy)) return { gx, gy };
        int maxr = std::max(w, h);
        for (int r = 1; r < maxr; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;   // 只搜最外圈
                    int nx = gx + dx, ny = gy + dy;
                    if (tc_passable(nx, ny)) return { nx, ny };
                }
            }
        }
        return { gx, gy };
    };

    // 默认分布：环形均布（2 玩家为左右对称），落水则就近修正到陆地
    auto default_pos = [&](int idx) -> SpawnPoint {
        SpawnPoint p;
        p.from_tc = false;
        int n = player_count;
        float cx = w * 0.5f, cy = h * 0.5f;
        float rx = w * 0.5f - 14.0f;
        float ry = h * 0.5f - 14.0f;
        float ang = 0.0f;
        if (n == 2) {
            ang = (idx == 0) ? 3.14159265f : 0.0f;   // 左 / 右
        }
        else {
            ang = -3.14159265f / 2.0f + idx * (2.0f * 3.14159265f / n);   // 从正上方开始
        }
        float gx = cx + std::cos(ang) * rx - 10.0f;
        float gy = cy + std::sin(ang) * ry - 10.0f;
        p.gx = (int)std::floor(gx + 0.5f);
        p.gy = (int)std::floor(gy + 0.5f);
        if (p.gx < 2) p.gx = 2;
        if (p.gy < 2) p.gy = 2;
        if (p.gx > w - TC_CELLS - 2) p.gx = w - TC_CELLS - 2;
        if (p.gy > h - TC_CELLS - 2) p.gy = h - TC_CELLS - 2;
        auto [lx, ly] = fix_to_land(p.gx, p.gy);
        p.gx = lx; p.gy = ly;
        return p;
    };

    std::vector<SpawnPoint> points(player_count);
    for (int i = 0; i < player_count; ++i) points[i] = default_pos(i);

    // 收集地图上实际摆放的城镇中心（按 player, gy, gx 排序）
    struct TC { int player, gx, gy; };
    std::vector<TC> tcs;
    for (const auto& e : entities) {
        if (e.kind == EntityRecord::Kind::Building && e.type == (int)BuildingEntityType::TownCenter)
            tcs.push_back({ e.player, e.gx, e.gy });
    }
    std::sort(tcs.begin(), tcs.end(), [](const TC& a, const TC& b) {
        if (a.player != b.player) return a.player < b.player;
        if (a.gy != b.gy) return a.gy < b.gy;
        return a.gx < b.gx;
    });

    std::vector<bool> used(tcs.size(), false);
    // 先按 player 归属映射到对应槽位
    for (int i = 0; i < player_count; ++i) {
        for (size_t t = 0; t < tcs.size(); ++t) {
            if (used[t]) continue;
            if (tcs[t].player == i + 1) {
                points[i] = SpawnPoint{ tcs[t].gx, tcs[t].gy, true };
                used[t] = true;
                break;
            }
        }
    }
    // 剩余城镇中心顺序填到尚未有城镇中心的槽位
    size_t ti = 0;
    for (int i = 0; i < player_count; ++i) {
        if (points[i].from_tc) continue;
        while (ti < tcs.size() && used[ti]) ++ti;
        if (ti < tcs.size()) {
            points[i] = SpawnPoint{ tcs[ti].gx, tcs[ti].gy, true };
            used[ti] = true;
            ++ti;
        }
    }

    // 解析出生点重叠：城镇中心 20x20 脚印严格相交即视为重叠，重叠的靠后出生点螺旋偏移到
    // 最近的不重叠、全陆地、不越界位置，保证每个玩家的城镇中心都能放下（位置太近也能偏移后放下）。
    auto footprints_overlap = [&](const SpawnPoint& a, const SpawnPoint& b) {
        return a.gx < b.gx + TC_CELLS && a.gx + TC_CELLS > b.gx &&
               a.gy < b.gy + TC_CELLS && a.gy + TC_CELLS > b.gy;
    };
    auto overlaps_any = [&](const SpawnPoint& p, int upto) {
        for (int j = 0; j < upto; ++j)
            if (footprints_overlap(p, points[j])) return true;
        return false;
    };
    auto fix_overlap = [&](int gx, int gy, int upto) -> std::pair<int, int> {
        SpawnPoint cand{ gx, gy, false };
        if (!overlaps_any(cand, upto) && tc_passable(gx, gy)) return { gx, gy };
        int maxr = std::max(w, h);
        for (int r = 1; r < maxr; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    int nx = gx + dx, ny = gy + dy;
                    if (nx < 0 || ny < 0 || nx + TC_CELLS > w || ny + TC_CELLS > h) continue;
                    if (!tc_passable(nx, ny)) continue;
                    SpawnPoint c{ nx, ny, false };
                    if (!overlaps_any(c, upto)) return { nx, ny };
                }
            }
        }
        return { gx, gy };
    };

    for (int i = 0; i < player_count; ++i) {
        if (overlaps_any(points[i], i)) {
            auto [nx, ny] = fix_overlap(points[i].gx, points[i].gy, i);
            points[i].gx = nx;
            points[i].gy = ny;
        }
    }

    return points;
}
