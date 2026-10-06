# -*- coding: utf-8 -*-
"""
生成两张左右对称的单挑地图（.srmap 文本格式 v1）。

地图 4（map04.srmap）：中间一个大湖，左右基地只能通过上下两条狭窄走廊通行。
地图 5（map05.srmap）：中间一条被木头（树）填满的纵向通道，上下各留开阔小道，
                    进攻要么绕上下小道，要么伐木挖穿中间木墙。

资源类型（ResourceEntityType）：0=Wood(2x2) 1=SGold(10x10) 2=LGold(15x15) 3=Stone(10x10) 4=Berries(4x4)
建筑类型（BuildingEntityType）：0=TownCenter(20x20) 1=ArcheryRange
"""
import os

W, H, C, P = 300, 200, 10, 8

# 资源尺寸与血量（与 factories.cpp 对齐）
RSIZE = {0: 2, 1: 10, 2: 15, 3: 10, 4: 4}
RHP = {0: 20, 1: 3000, 2: 8000, 3: 3000, 4: 500}

# 建筑尺寸（城镇中心 20x20）
BSIZE = {0: 20, 1: 20}


def new_grid():
    return [['L'] * W for _ in range(H)]


def can_place(grid, occ, gx, gy, size):
    if gx < 0 or gy < 0 or gx + size > W or gy + size > H:
        return False
    for yy in range(gy, gy + size):
        for xx in range(gx, gx + size):
            if grid[yy][xx] == 'W':
                return False
            if occ[yy][xx]:
                return False
    return True


def mark(occ, gx, gy, size):
    for yy in range(gy, gy + size):
        for xx in range(gx, gx + size):
            occ[yy][xx] = True


class Builder:
    def __init__(self, grid):
        self.grid = grid
        self.occ = [[False] * W for _ in range(H)]
        self.entities = []
        self.warns = 0

    def resource(self, rtype, gx, gy, mirror=True):
        size = RSIZE[rtype]
        if can_place(self.grid, self.occ, gx, gy, size):
            mark(self.occ, gx, gy, size)
            self.entities.append("R %d %d %d %d" % (rtype, gx, gy, RHP[rtype]))
        else:
            self.warns += 1
            print("  WARN resource R%d at (%d,%d) blocked" % (rtype, gx, gy))
            return
        if mirror:
            gxr = W - size - gx
            if can_place(self.grid, self.occ, gxr, gy, size):
                mark(self.occ, gxr, gy, size)
                self.entities.append("R %d %d %d %d" % (rtype, gxr, gy, RHP[rtype]))
            else:
                self.warns += 1
                print("  WARN mirror R%d at (%d,%d) blocked" % (rtype, gxr, gy))

    def building(self, btype, gx, gy, player, mirror_to=None):
        size = BSIZE[btype]
        if not can_place(self.grid, self.occ, gx, gy, size):
            self.warns += 1
            print("  WARN building B%d at (%d,%d) blocked" % (btype, gx, gy))
            return
        mark(self.occ, gx, gy, size)
        self.entities.append("B %d %d %d %d" % (btype, gx, gy, player))
        if mirror_to is not None:
            gxr = W - size - gx
            if not can_place(self.grid, self.occ, gxr, gy, size):
                self.warns += 1
                print("  WARN mirror building B%d at (%d,%d) blocked" % (btype, gxr, gy))
                return
            mark(self.occ, gxr, gy, size)
            self.entities.append("B %d %d %d %d" % (btype, gxr, gy, mirror_to))


def write_map(path, grid, entities):
    with open(path, 'w', newline='\n', encoding='ascii') as f:
        f.write("MYSTRMAP 1\n")
        f.write("W %d\n" % W)
        f.write("H %d\n" % H)
        f.write("C %d\n" % C)
        f.write("P %d\n" % P)
        for row in grid:
            f.write(''.join(row) + '\n')
        for e in entities:
            f.write(e + '\n')


def fill_water(grid, x0, x1, y0, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            grid[y][x] = 'W'


# ---------------------------------------------------------------------------
# 地图 4：中央湖 + 上下两条狭窄走廊
# ---------------------------------------------------------------------------
def build_map4():
    grid = new_grid()
    # 中央湖：x in [80,219]（140 宽），y in [16,183]（168 高）
    fill_water(grid, 80, 219, 16, 183)
    # 上下走廊：y 0..15 与 y 184..199 保持陆地

    b = Builder(grid)

    # 城镇中心（左右对称的 1v1 出生点）
    b.building(0, 12, 88, 1, mirror_to=2)

    # —— 左玩家基地资源（对称镜像到右玩家）——
    # 树木（2x2，hp20）
    for (x, y) in [(44, 76), (48, 80), (46, 88), (50, 96), (44, 104), (48, 112),
                   (6, 50), (10, 130), (6, 150), (72, 88), (74, 96)]:
        b.resource(0, x, y)
    # 浆果（4x4，hp500）
    for (x, y) in [(36, 60), (36, 120)]:
        b.resource(4, x, y)
    # 小金矿（10x10，hp3000）
    for (x, y) in [(2, 24), (2, 164)]:
        b.resource(1, x, y)
    # 石矿（10x10，hp3000）
    b.resource(3, 0, 94)
    # 大金矿（15x15，hp8000，基地最深处）
    b.resource(2, 0, 0)

    return grid, b


# ---------------------------------------------------------------------------
# 地图 5：中间木墙（树填满）+ 上下两条开阔小道
# ---------------------------------------------------------------------------
def build_map5():
    grid = new_grid()  # 全陆地

    b = Builder(grid)

    # 城镇中心
    b.building(0, 12, 88, 1, mirror_to=2)

    # 左玩家基地资源（镜像）
    for (x, y) in [(44, 76), (48, 80), (46, 88), (50, 96), (44, 104), (48, 112),
                   (6, 50), (10, 130), (6, 150), (136, 88), (138, 96), (136, 104)]:
        b.resource(0, x, y)
    for (x, y) in [(36, 60), (36, 120)]:
        b.resource(4, x, y)
    for (x, y) in [(2, 24), (2, 164)]:
        b.resource(1, x, y)
    b.resource(3, 0, 94)
    b.resource(2, 0, 0)

    # 中间纵向木墙：x in [146,153]（8 格宽 = 4 列 2x2 树），y in [34,165]（132 格高）
    # 树填满整条“道路”，可通过伐木挖穿
    for gy in range(34, 166, 2):
        for gx in range(146, 154, 2):
            b.resource(0, gx, gy, mirror=False)

    return grid, b


def verify(path, name):
    lines = open(path, encoding='ascii').read().split('\n')
    assert lines[0] == "MYSTRMAP 1", name + " magic"
    w = int(lines[1].split()[1])
    h = int(lines[2].split()[1])
    p = int(lines[4].split()[1])
    assert (w, h, p) == (W, H, P), name + " header"
    ter = lines[5:5 + H]
    assert len(ter) == H and all(len(r) == W for r in ter), name + " terrain size"

    # 地形左右对称
    for y in range(H):
        for x in range(W):
            assert ter[y][x] == ter[y][W - 1 - x], "%s terrain not symmetric at (%d,%d)" % (name, x, y)

    # 实体解析
    res = []
    bld = []
    for ln in lines[5 + H:]:
        ln = ln.strip()
        if not ln:
            continue
        p_ = ln.split()
        if p_[0] == 'R':
            res.append((int(p_[1]), int(p_[2]), int(p_[3])))
        elif p_[0] == 'B':
            bld.append((int(p_[1]), int(p_[2]), int(p_[3]), int(p_[4])))

    # 无重叠 + 无落水
    occ = [[False] * W for _ in range(H)]
    for (rt, gx, gy) in res:
        s = RSIZE[rt]
        assert can_place(ter_to_grid(ter), occ, gx, gy, s), "%s R%d overlap/water at (%d,%d)" % (name, rt, gx, gy)
        mark(occ, gx, gy, s)
    for (bt, gx, gy, pl) in bld:
        s = BSIZE[bt]
        assert can_place(ter_to_grid(ter), occ, gx, gy, s), "%s B%d overlap/water at (%d,%d)" % (name, bt, gx, gy)
        mark(occ, gx, gy, s)

    # 资源左右对称（每个左资源应有一个镜像右资源）
    from collections import Counter
    cnt = Counter()
    for (rt, gx, gy) in res:
        s = RSIZE[rt]
        if gx < W // 2:
            cnt[(rt, gx, gy)] += 1
    for (rt, gx, gy), c in cnt.items():
        s = RSIZE[rt]
        gr = W - s - gx
        assert (rt, gr, gy) in [(r[0], r[1], r[2]) for r in res], "%s resource no mirror: R%d (%d,%d)" % (name, rt, gx, gy)

    print("%s OK: %d resources, %d buildings" % (name, len(res), len(bld)))
    return len(res), len(bld)


def ter_to_grid(ter):
    return [list(r) for r in ter]


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, "maps")
    os.makedirs(outdir, exist_ok=True)

    grid4, b4 = build_map4()
    p4 = os.path.join(outdir, "map04.srmap")
    write_map(p4, grid4, b4.entities)
    print("map04 warnings: %d" % b4.warns)

    grid5, b5 = build_map5()
    p5 = os.path.join(outdir, "map05.srmap")
    write_map(p5, grid5, b5.entities)
    print("map05 warnings: %d" % b5.warns)

    verify(p4, "map04")
    verify(p5, "map05")
    print("DONE")
