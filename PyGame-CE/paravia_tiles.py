#!/usr/bin/env python3
"""paravia_tiles.py - pygame tilesets and the map view.

Two interchangeable tilesets:

  FontTileSet   Phase 1. Renders each tile ID as a character from a
                monospace font (TTF, or an 8x8 bitmap strip). The 1978
                look.

  SpriteTileSet Phase 2. Renders each tile ID from a sprite atlas
                image. Same interface, so MapView and the game logic
                never notice the swap.

Both implement:  get(tile_id) -> pygame.Surface of (tile_w, tile_h)

MapView blits a paravia_tilemap.TileMap through whichever tileset you
hand it. The blit loop is the whole renderer:

    for each row, col: screen.blit(tileset.get(grid[row][col]), ...)
"""

import pygame

from paravia_tilemap import GLYPHS, T

# Symbolic color name -> (foreground, background) RGB.
# Loosely CGA-meets-curses; tune to taste.
PALETTE = {
    "sky": ((120, 170, 220), (24, 32, 48)),
    "wall": ((200, 200, 190), (24, 32, 48)),
    "tower": ((230, 210, 120), (24, 32, 48)),
    "building": ((240, 240, 240), (24, 32, 48)),
    "field": ((110, 180, 90), (28, 44, 28)),
    "plow": ((230, 190, 140), (28, 44, 28)),
    "grain": ((220, 180, 60), (40, 36, 20)),
    "grain_bg": ((90, 80, 50), (40, 36, 20)),
}
DEFAULT_BG = (24, 32, 48)


class FontTileSet:
    """Phase 1: character tiles from a font.

    font_path=None uses pygame's bundled monospace fallback. Point it
    at a C64-style TTF (e.g. one of the PETSCII revivals) for maximum
    period flavor. Tiles are cached; rendering happens once per ID.
    """

    def __init__(self, tile_w=16, tile_h=16, font_path=None):
        self.tile_w = tile_w
        self.tile_h = tile_h
        if font_path:
            self.font = pygame.font.Font(font_path, tile_h)
        else:
            name = pygame.font.match_font("dejavusansmono,couriernew,monospace")
            self.font = pygame.font.Font(name, tile_h)
        self._cache = {}

    def get(self, tile_id):
        surf = self._cache.get(tile_id)
        if surf is None:
            ch, color_name = GLYPHS[tile_id]
            fg, bg = PALETTE.get(color_name, ((255, 255, 255), DEFAULT_BG))
            surf = pygame.Surface((self.tile_w, self.tile_h))
            surf.fill(bg)
            glyph = self.font.render(ch, True, fg)
            surf.blit(
                glyph,
                (
                    (self.tile_w - glyph.get_width()) // 2,
                    (self.tile_h - glyph.get_height()) // 2,
                ),
            )
            self._cache[tile_id] = surf
        return surf


class BitmapFontTileSet:
    """Phase 1.5: character tiles from an 8x8 bitmap font strip.

    Expects an image containing 8x8 glyphs laid out left-to-right in
    ASCII order starting at code 32 (space). This is exactly a C64
    character ROM dumped to PNG, so it doubles as a dry run for the
    real hardware, and the loader below is already 90% of a sprite
    atlas loader.
    """

    def __init__(self, image_path, scale=2, glyph_size=8, first_code=32):
        self.src = pygame.image.load(image_path).convert()
        self.glyph_size = glyph_size
        self.first_code = first_code
        self.scale = scale
        self.tile_w = glyph_size * scale
        self.tile_h = glyph_size * scale
        self._cache = {}

    def get(self, tile_id):
        surf = self._cache.get(tile_id)
        if surf is None:
            ch, color_name = GLYPHS[tile_id]
            fg, bg = PALETTE.get(color_name, ((255, 255, 255), DEFAULT_BG))
            idx = ord(ch) - self.first_code
            g = self.glyph_size
            cell = self.src.subsurface((idx * g, 0, g, g)).copy()
            # Recolor: treat white as ink, black as paper.
            recolored = pygame.Surface((g, g))
            recolored.fill(bg)
            cell.set_colorkey((0, 0, 0))
            ink = cell.copy()
            ink.fill(fg, special_flags=pygame.BLEND_MULT)
            recolored.blit(ink, (0, 0))
            surf = pygame.transform.scale(recolored, (self.tile_w, self.tile_h))
            self._cache[tile_id] = surf
        return surf


class SpriteTileSet:
    """Phase 2: tiles from a sprite atlas.

    atlas_map is {tile_id: (col, row)} in atlas grid coordinates.
    Any tile ID missing from the map falls back to `fallback`
    (typically a FontTileSet), so you can replace art one tile at a
    time - draw the castle sprites first, leave the fields as
    characters, ship it, iterate.
    """

    def __init__(self, atlas_path, atlas_map, tile_w=16, tile_h=16,
                 src_tile=16, fallback=None):
        self.atlas = pygame.image.load(atlas_path).convert_alpha()
        self.atlas_map = atlas_map
        self.tile_w = tile_w
        self.tile_h = tile_h
        self.src_tile = src_tile
        self.fallback = fallback
        self._cache = {}

    def get(self, tile_id):
        surf = self._cache.get(tile_id)
        if surf is None:
            pos = self.atlas_map.get(tile_id)
            if pos is None:
                if self.fallback is None:
                    raise KeyError("no sprite or fallback for %r" % (tile_id,))
                return self.fallback.get(tile_id)
            col, row = pos
            s = self.src_tile
            cell = self.atlas.subsurface((col * s, row * s, s, s))
            surf = pygame.transform.scale(cell, (self.tile_w, self.tile_h))
            self._cache[tile_id] = surf
        return surf


class MapView:
    """Blits a TileMap through a tileset at a pixel offset."""

    def __init__(self, tileset, origin=(0, 0), title_color=(255, 255, 255)):
        self.tileset = tileset
        self.origin = origin
        self.title_color = title_color
        self.title_font = pygame.font.Font(
            pygame.font.match_font("dejavusansmono,couriernew,monospace"), 18
        )

    def pixel_size(self, tilemap):
        return (
            tilemap.cols * self.tileset.tile_w,
            tilemap.rows * self.tileset.tile_h,
        )

    def draw(self, screen, tilemap):
        ox, oy = self.origin
        tw, th = self.tileset.tile_w, self.tileset.tile_h
        for r, row in enumerate(tilemap.grid):
            y = oy + r * th
            for c, tile_id in enumerate(row):
                screen.blit(self.tileset.get(tile_id), (ox + c * tw, y))
        if tilemap.title:
            label = self.title_font.render(tilemap.title, True, self.title_color)
            screen.blit(label, (ox + 8, oy + 4))
