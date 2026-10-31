# -*- coding: utf-8 -*-
"""Generate symmetric 1v1 / 2v2 / 3v3 / 4v4 maps for MySTR.

Output: SimpleRTS/maps/map1v1_*.srmap, map2v2_*.srmap, ...
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "maps"
OUT.mkdir(exist_ok=True)

RSIZE = {0: 2, 1: 10, 2: 15, 3: 10, 4: 4}
RHP = {0: 20, 1: 3000, 2: 8000, 3: 3000, 4: 500}


class Builder:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.grid = [["L"] * w for _ in range(h)]
        self.occ = [[False] * w for _ in range(h)]
        self.entities = []
        self.warns = 0

    def water_ellipse(self, cx, cy, rx, ry):
        for y in range(self.h):
            for x in range(self.w):
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.grid[y][x] = "W"

    def water_rect(self, x0, y0, x1, y1):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.grid[y][x] = "W"

    def can_place(self, gx, gy, size):
        if gx < 0 or gy < 0 or gx + size > self.w or gy + size > self.h:
            return False
        for y in range(gy, gy + size):
            for x in range(gx, gx + size):
                if self.grid[y][x] == "W" or self.occ[y][x]:
                    return False
        return True

    def mark(self, gx, gy, size):
        for y in range(gy, gy + size):
            for x in range(gx, gx + size):
                self.occ[y][x] = True

    def tc(self, gx, gy, player):
        if not self.can_place(gx, gy, 20):
            self.warns += 1
            print(f"  WARN TC player {player} at {gx},{gy}")
            return
        self.mark(gx, gy, 20)
        self.entities.append(f"B 0 {gx} {gy} {player}")

    def res(self, rtype, gx, gy):
        size = RSIZE[rtype]
        if not self.can_place(gx, gy, size):
            self.warns += 1
            print(f"  WARN R{rtype} at {gx},{gy}")
            return
        self.mark(gx, gy, size)
        self.entities.append(f"R {rtype} {gx} {gy} {RHP[rtype]}")

    def near_base(self, tc_x, tc_y, side):
        # side=1: resources to the right; side=-1: to the left
        def px(off, size):
            return tc_x + off if side > 0 else tc_x - off - size
        specs = [
            (0, 44, -14), (0, 50, 8), (0, 60, -28), (0, 54, 26),
            (4, 56, 16), (1, 68, -8), (3, 68, 18),
        ]
        for rtype, off, dy in specs:
            self.res(rtype, px(off, RSIZE[rtype]), tc_y + dy)


def write_map(name, b):
    with open(OUT / f"{name}.srmap", "w", newline="\n", encoding="ascii") as f:
        f.write("MYSTRMAP 1\n")
        f.write(f"W {b.w}\nH {b.h}\nC 10\nP {len({int(e.split()[4]) for e in b.entities if e.startswith('B ')})}\n")
        for row in b.grid:
            f.write("".join(row) + "\n")
        for e in b.entities:
            f.write(e + "\n")


def add_center_gold(b, amount=2):
    # central land resources; skip if water/occupied
    cx, cy = b.w // 2, b.h // 2
    positions = [(cx - 20, cy - 8), (cx + 5, cy - 8), (cx - 8, cy + 15)]
    for i in range(amount):
        if i < len(positions):
            x, y = positions[i]
            b.res(2, max(0, min(b.w - 15, x)), max(0, min(b.h - 15, y)))


def add_forest_cover(b, count=28):
    # deterministic scattered tree clusters around the middle band
    cx, cy = b.w // 2, b.h // 2
    for i in range(count):
        angle = i * 2.399963
        radius = 35 + (i % 5) * 18
        x = int(cx + math.cos(angle) * radius) - 1
        y = int(cy + math.sin(angle) * radius * 0.65) - 1
        b.res(0, max(1, min(b.w - 3, x)), max(1, min(b.h - 3, y)))


def build(name, team_size, layout):
    w = {1: 240, 2: 340, 3: 440, 4: 540}[team_size]
    h = {1: 150, 2: 240, 3: 280, 4: 320}[team_size]
    b = Builder(w, h)

    # terrain
    cx, cy = w // 2, h // 2
    if layout == "lake":
        b.water_ellipse(cx, cy, w * 0.19, h * 0.24)
    elif layout == "river":
        # vertical river with two bridges
        b.water_rect(cx - 9, 12, cx + 9, h - 12)
        for by in (int(h * 0.30), int(h * 0.70)):
            for y in range(by, min(h, by + 12)):
                for x in range(cx - 9, cx + 9):
                    b.grid[y][x] = "L"
    elif layout == "forest":
        pass
    elif layout == "ring":
        for y in range(h):
            for x in range(w):
                dx = (x - cx) / (w * 0.22)
                dy = (y - cy) / (h * 0.28)
                d = dx * dx + dy * dy
                if 0.45 <= d <= 1.0:
                    b.grid[y][x] = "W"
        # four bridges through the ring
        for y in range(cy - h, cy + h):
            for x in (cx - 4, cx + 3):
                if 0 <= x < w and 0 <= y < h:
                    b.grid[y][x] = "L"

    # bases
    margin = 20
    if team_size == 1:
        ys = [(h - 20) // 2]
    else:
        span = h - 40 - 20
        ys = [margin + i * span // (team_size - 1) for i in range(team_size)]
    left_x, right_x = 20, w - 40
    for i, y in enumerate(ys):
        b.tc(left_x, y, i + 1)
        b.tc(right_x, y, team_size + i + 1)
        b.near_base(left_x, y, 1)
        b.near_base(right_x, y, -1)

    add_center_gold(b)
    if layout == "forest":
        add_forest_cover(b)
    else:
        # a little cover near the lanes
        for i in range(12):
            x = 20 + (i * 37) % (w - 40)
            y = 30 + (i * 53) % (h - 60)
            b.res(0, x, y)

    write_map(name, b)
    return b.warns


def main():
    specs = [
        ("map1v1_duel", 1, "forest"),
        ("map1v1_river", 1, "river"),
        ("map2v2_lake", 2, "lake"),
        ("map2v2_forest", 2, "forest"),
        ("map3v3_river", 3, "river"),
        ("map3v3_ring", 3, "ring"),
        ("map4v4_forest", 4, "forest"),
        ("map4v4_lake", 4, "lake"),
    ]
    for name, n, layout in specs:
        warns = build(name, n, layout)
        print(f"{name}: {n}v{n}, {layout}, warnings={warns}")


if __name__ == "__main__":
    main()
