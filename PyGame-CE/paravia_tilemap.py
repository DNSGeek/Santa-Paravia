#!/usr/bin/env python3
"""paravia_tilemap.py - renderer-blind map composition.

Builds the Santa Paravia city map as a 2D grid of tile IDs from a
Player's state. No pygame, no curses, no I/O. Any front end (pygame,
terminal, or a 40x25 screen-RAM fill on certain 8-bit hardware) can
consume the grid.

Semantics are ported from the curses DrawMap() in paravia.c:
  - Three horizontal bands: sky (20%), walled city (50%), fields (30%)
  - Wall width/height scale with Land (5000..50000)
  - Guard tower height = soldier adequacy (soldiers vs Land/1000)
  - Buildings drawn left-to-right above the gate:
      cathedral, palace, market, mill
  - Plowman at top of field band when Serfs >= Land/10,
    descends proportionally otherwise
  - Grain reserve bar on the bottom row
"""

from enum import IntEnum, auto


class T(IntEnum):
    """Tile IDs. The tileset layer maps these to glyphs or sprites."""

    EMPTY = 0
    SKY = auto()
    # Wall pieces
    WALL_H = auto()
    WALL_V = auto()
    WALL_UL = auto()
    WALL_UR = auto()
    WALL_LL = auto()
    WALL_LR = auto()
    GATE_L = auto()
    GATE_R = auto()
    # Guard tower
    TOWER_WALL = auto()
    TOWER_TOP_A = auto()   # battlement 'n'
    TOWER_TOP_B = auto()   # battlement '_'
    TOWER_FILL = auto()
    # Buildings
    CATHEDRAL = auto()
    PALACE = auto()
    MARKET = auto()
    MILL = auto()
    KEEP_ROOF = auto()     # '^'
    KEEP_FLAG = auto()     # 'n'
    KEEP_L = auto()        # '['
    KEEP_DOOR = auto()     # 'H'
    KEEP_R = auto()        # ']'
    # Fields
    FIELD_A = auto()       # '.'
    FIELD_B = auto()       # '"'
    # Plowman "o-HH-8>" one tile per char
    PLOW_0 = auto()
    PLOW_1 = auto()
    PLOW_2 = auto()
    PLOW_3 = auto()
    PLOW_4 = auto()
    PLOW_5 = auto()
    PLOW_6 = auto()
    # Grain bar
    GRAIN_FULL = auto()
    GRAIN_EMPTY = auto()


PLOWMAN = [T.PLOW_0, T.PLOW_1, T.PLOW_2, T.PLOW_3, T.PLOW_4, T.PLOW_5, T.PLOW_6]

LAND_MIN, LAND_MAX = 5000, 50000
GRAIN_BAR_MAX = 20000


class TileMap:
    """A cols x rows grid of tile IDs plus a title string."""

    def __init__(self, cols, rows):
        self.cols = cols
        self.rows = rows
        self.title = ""
        self.grid = [[T.EMPTY] * cols for _ in range(rows)]

    def put(self, row, col, tile):
        if 0 <= row < self.rows and 0 <= col < self.cols:
            self.grid[row][col] = tile

    def hline(self, row, col0, col1, tile):
        for c in range(col0, col1):
            self.put(row, c, tile)

    def vline(self, col, row0, row1, tile):
        for r in range(row0, row1):
            self.put(r, col, tile)


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def build_map(player, cols=44, rows=25):
    """Compose the city map for `player` into a TileMap.

    cols/rows are in tiles. 44x25 leaves room for a stats panel beside
    it at 80 columns; use whatever fits your window.
    """
    tm = TileMap(cols, rows)
    tm.title = "%s, %d AD" % (player.City, player.Year)

    # --- Band layout (same fractions as paravia.c) ------------------------
    sky_rows = rows // 5
    city_rows = rows // 2
    field_rows = rows - sky_rows - city_rows

    sky_top = 0
    city_top = sky_top + sky_rows
    field_top = city_top + city_rows

    # --- City wall, scaled by Land ----------------------------------------
    land = clamp(player.Land, LAND_MIN, LAND_MAX)
    wall_w = int(
        (land - LAND_MIN) / (LAND_MAX - LAND_MIN) * (cols - 12)
    ) + 10
    wall_w = min(wall_w, cols - 2)

    wall_h = clamp((city_rows * wall_w) // max(cols - 2, 1), 4, city_rows - 1)

    wall_left = (cols - wall_w) // 2
    wall_top = city_top + (city_rows - wall_h) // 2
    wall_bot = wall_top + wall_h - 1
    wall_right = wall_left + wall_w - 1

    tm.hline(wall_top, wall_left + 1, wall_right, T.WALL_H)
    tm.hline(wall_bot, wall_left + 1, wall_right, T.WALL_H)
    tm.vline(wall_left, wall_top + 1, wall_bot, T.WALL_V)
    tm.vline(wall_right, wall_top + 1, wall_bot, T.WALL_V)
    tm.put(wall_top, wall_left, T.WALL_UL)
    tm.put(wall_top, wall_right, T.WALL_UR)
    tm.put(wall_bot, wall_left, T.WALL_LL)
    tm.put(wall_bot, wall_right, T.WALL_LR)

    gate_col = wall_left + wall_w // 2
    tm.put(wall_bot, gate_col, T.GATE_L)
    tm.put(wall_bot, gate_col + 1, T.GATE_R)

    # --- Guard tower: soldiers vs Land/1000 adequacy ----------------------
    needed = max(player.Land, 1) // 1000 + 1
    ratio = clamp(player.Soldiers / needed, 0.0, 2.0)
    tower_max_h = sky_rows + 2
    tower_h = max(int(ratio / 2.0 * tower_max_h), 1)
    tower_w = 5
    tower_left = wall_left
    tower_base = wall_top            # sits on top of the wall
    tower_top = max(tower_base - tower_h, sky_top)

    for r in range(tower_top, tower_base):
        tm.put(r, tower_left, T.TOWER_WALL)
        tm.put(r, tower_left + tower_w - 1, T.TOWER_WALL)
        for c in range(tower_left + 1, tower_left + tower_w - 1):
            tm.put(r, c, T.TOWER_FILL)
    for c in range(tower_left, tower_left + tower_w):
        tm.put(tower_top, c, T.TOWER_TOP_A if c % 2 == 0 else T.TOWER_TOP_B)

    # --- Buildings above the gate ------------------------------------------
    brow = wall_bot - 1
    bcol = wall_left + 2
    bmax = wall_right - 1

    def draw_buildings(tile, count):
        nonlocal bcol
        for _ in range(count):
            if bcol >= bmax:
                return
            tm.put(brow, bcol, tile)
            bcol += 1

    draw_buildings(T.CATHEDRAL, player.Cathedral)
    draw_buildings(T.PALACE, player.Palace)
    draw_buildings(T.MARKET, player.Marketplaces)
    draw_buildings(T.MILL, player.Mills)

    # --- Keep in the wall centre -------------------------------------------
    keep_col = wall_left + wall_w // 2 - 1
    keep_row = clamp(wall_top + wall_h // 2, wall_top + 1, wall_bot - 1)
    if keep_row - 1 > wall_top:
        tm.put(keep_row - 1, keep_col, T.KEEP_ROOF)
        tm.put(keep_row - 1, keep_col + 1, T.KEEP_FLAG)
        tm.put(keep_row - 1, keep_col + 2, T.KEEP_ROOF)
    tm.put(keep_row, keep_col, T.KEEP_L)
    tm.put(keep_row, keep_col + 1, T.KEEP_DOOR)
    tm.put(keep_row, keep_col + 2, T.KEEP_R)

    # --- Fields --------------------------------------------------------------
    for r in range(field_top, field_top + field_rows - 1):
        for c in range(cols):
            tm.put(r, c, T.FIELD_B if (r + c) % 4 == 0 else T.FIELD_A)

    # --- Plowman: top of band = all land in production ----------------------
    serfs_needed = max(player.Land // 10, 1)
    prod = clamp(player.Serfs / serfs_needed, 0.0, 1.0)
    plow_row = clamp(
        field_top + int((1.0 - prod) * (field_rows - 2)),
        field_top,
        field_top + field_rows - 2,
    )
    plow_col = cols // 3
    for i, tile in enumerate(PLOWMAN):
        tm.put(plow_row, plow_col + i, tile)

    # --- Grain reserve bar on the bottom row ---------------------------------
    frac = clamp(player.GrainReserve / GRAIN_BAR_MAX, 0.0, 1.0)
    filled = int(frac * cols)
    grain_row = rows - 1
    tm.hline(grain_row, 0, filled, T.GRAIN_FULL)
    tm.hline(grain_row, filled, cols, T.GRAIN_EMPTY)

    return tm


# --- Phase-1 glyph table -----------------------------------------------------
# tile ID -> (char, color_name). The pygame FontTileSet consumes this,
# and render_text() below uses just the chars. Colors are symbolic here;
# the tileset layer decides actual RGB values (or ignores them entirely
# once you swap to sprites).
GLYPHS = {
    T.EMPTY: (" ", "sky"),
    T.SKY: (" ", "sky"),
    T.WALL_H: ("-", "wall"),
    T.WALL_V: ("|", "wall"),
    T.WALL_UL: ("+", "wall"),
    T.WALL_UR: ("+", "wall"),
    T.WALL_LL: ("+", "wall"),
    T.WALL_LR: ("+", "wall"),
    T.GATE_L: ("[", "wall"),
    T.GATE_R: ("]", "wall"),
    T.TOWER_WALL: ("|", "tower"),
    T.TOWER_TOP_A: ("n", "tower"),
    T.TOWER_TOP_B: ("_", "tower"),
    T.TOWER_FILL: (" ", "tower"),
    T.CATHEDRAL: ("+", "building"),
    T.PALACE: ("P", "building"),
    T.MARKET: ("M", "building"),
    T.MILL: ("~", "building"),
    T.KEEP_ROOF: ("^", "building"),
    T.KEEP_FLAG: ("n", "building"),
    T.KEEP_L: ("[", "building"),
    T.KEEP_DOOR: ("H", "building"),
    T.KEEP_R: ("]", "building"),
    T.FIELD_A: (".", "field"),
    T.FIELD_B: ('"', "field"),
    T.PLOW_0: ("o", "plow"),
    T.PLOW_1: ("-", "plow"),
    T.PLOW_2: ("H", "plow"),
    T.PLOW_3: ("H", "plow"),
    T.PLOW_4: ("-", "plow"),
    T.PLOW_5: ("8", "plow"),
    T.PLOW_6: (">", "plow"),
    T.GRAIN_FULL: ("#", "grain"),
    T.GRAIN_EMPTY: (".", "grain_bg"),
}


def render_text(tm):
    """Debug renderer: TileMap -> string. No pygame required.

    This is the free bonus of the layered design: a pure-terminal mode
    for testing game logic without opening a window.
    """
    lines = [tm.title]
    for row in tm.grid:
        lines.append("".join(GLYPHS[t][0] for t in row))
    return "\n".join(lines)
