#!/usr/bin/env python3
"""Generate XBM icon data for Bluetooth and WiFi status icons.

Both icons target 17px effective content height so they look
proportionally balanced when placed side by side in the header.

Run: python3 generate_icons.py
"""
import math


def make_grid(w, h):
    return [[0] * w for _ in range(h)]


def show_grid(grid):
    for row in grid:
        print("".join("#" if p else "." for p in row))


def content_bounds(grid):
    h, w = len(grid), len(grid[0])
    top = next((r for r in range(h) if any(grid[r])), h)
    bot = next((r for r in range(h - 1, -1, -1) if any(grid[r])), -1)
    return top, bot


def to_xbm_array(grid, name):
    h, w = len(grid), len(grid[0])
    bpr = (w + 7) // 8
    vals = []
    for row in grid:
        for bi in range(bpr):
            v = 0
            for bit in range(8):
                col = bi * 8 + bit
                if col < w and row[col]:
                    v |= 1 << bit
            vals.append(v)
    lines = [
        f"#define {name}_W {w}",
        f"#define {name}_H {h}",
        f"const unsigned char {name.lower()}_bits[] PROGMEM = {{",
    ]
    for i in range(0, len(vals), bpr):
        chunk = vals[i : i + bpr]
        lines.append("  " + ", ".join(f"0x{v:02X}" for v in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def point_to_segment_dist(px, py, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    len_sq = dx * dx + dy * dy
    if len_sq == 0:
        return math.sqrt((px - x0) ** 2 + (py - y0) ** 2)
    t = max(0, min(1, ((px - x0) * dx + (py - y0) * dy) / len_sq))
    proj_x = x0 + t * dx
    proj_y = y0 + t * dy
    return math.sqrt((px - proj_x) ** 2 + (py - proj_y) ** 2)


def draw_thick_line(grid, x0, y0, x1, y1, thickness):
    h, w = len(grid), len(grid[0])
    half_t = thickness / 2.0
    for y in range(h):
        for x in range(w):
            if point_to_segment_dist(x, y, x0, y0, x1, y1) <= half_t:
                grid[y][x] = 1


def fill_arc_band(grid, cx, cy, r_inner, r_outer, deg_start, deg_end):
    h, w = len(grid), len(grid[0])
    for y in range(h):
        for x in range(w):
            dx = x - cx
            dy = cy - y
            r = math.sqrt(dx * dx + dy * dy)
            if r < r_inner or r > r_outer:
                continue
            angle = math.degrees(math.atan2(dy, dx))
            if angle < 0:
                angle += 360
            if deg_start <= angle <= deg_end:
                grid[y][x] = 1


# ========= BLUETOOTH RUNE (13 x 17) =========
bt_w, bt_h = 13, 17
bt = make_grid(bt_w, bt_h)

cx = 6.0
top, bot, mid = 0.0, 16.0, 8.0
rx, lx = 11.0, 1.0
uy, ly = 4.0, 12.0
thick = 1.7

draw_thick_line(bt, cx, top, cx, bot, thick)
draw_thick_line(bt, cx, top, rx, uy, thick)
draw_thick_line(bt, rx, uy, cx, mid, thick)
draw_thick_line(bt, cx, mid, rx, ly, thick)
draw_thick_line(bt, rx, ly, cx, bot, thick)
draw_thick_line(bt, lx, ly, cx, mid, thick)
draw_thick_line(bt, lx, uy, cx, mid, thick)

t, b = content_bounds(bt)
print(f"=== BLUETOOTH RUNE ({bt_w}x{bt_h}) content rows {t}-{b} = {b-t+1}px ===")
show_grid(bt)
print()
print(to_xbm_array(bt, "BT_RUNE"))
print()

# ========= WIFI ICON (23 x 17) =========
# Canvas widened so arcs can be large enough to fill the full 17px
# height, matching the BT icon. Origin at bottom-center.
wifi_w, wifi_h = 23, 17
wifi = make_grid(wifi_w, wifi_h)

wx, wy = 11.0, 16.0

fill_arc_band(wifi, wx, wy, 0, 3.2, 54, 126)
fill_arc_band(wifi, wx, wy, 5.2, 8.5, 46, 134)
fill_arc_band(wifi, wx, wy, 10.5, 16, 42, 138)

t2, b2 = content_bounds(wifi)
print(f"=== WIFI ICON ({wifi_w}x{wifi_h}) content rows {t2}-{b2} = {b2-t2+1}px ===")
show_grid(wifi)
print()
print(to_xbm_array(wifi, "WIFI_ICON"))
