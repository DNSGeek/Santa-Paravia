#!/usr/bin/env python3
"""demo_map.py - interactive proof of the tile renderer.

Drives the map straight from a paravia_player.Player so you can watch
the DrawMap semantics respond live:

  LEFT / RIGHT   sell / buy land        (wall grows and shrinks)
  UP / DOWN      hire / dismiss soldiers (tower grows and shrinks)
  S / X          more / fewer serfs      (plowman climbs and descends)
  G / B          more / less grain       (bottom bar)
  T              dump the map to the terminal via render_text()
  ESC            quit

Run:  python3 demo_map.py
"""

import sys

import pygame

from paravia_player import Player
from paravia_tilemap import build_map, render_text
from paravia_tiles import FontTileSet, MapView

COLS, ROWS = 44, 25
TILE = 18
FPS = 30


def main():
    pygame.init()
    tileset = FontTileSet(tile_w=TILE, tile_h=TILE)
    view = MapView(tileset, origin=(0, 0))

    player = Player("Tomasso", 0)
    tilemap = build_map(player, COLS, ROWS)

    screen = pygame.display.set_mode(view.pixel_size(tilemap))
    pygame.display.set_caption("Santa Paravia - tile renderer demo")
    clock = pygame.time.Clock()

    dirty = True
    while True:
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                pygame.quit()
                return
            if ev.type == pygame.KEYDOWN:
                k = ev.key
                if k == pygame.K_ESCAPE:
                    pygame.quit()
                    return
                elif k == pygame.K_RIGHT:
                    player.Land += 2000
                elif k == pygame.K_LEFT:
                    player.Land = max(player.Land - 2000, 1000)
                elif k == pygame.K_UP:
                    player.Soldiers += 5
                elif k == pygame.K_DOWN:
                    player.Soldiers = max(player.Soldiers - 5, 0)
                elif k == pygame.K_s:
                    player.Serfs += 250
                elif k == pygame.K_x:
                    player.Serfs = max(player.Serfs - 250, 0)
                elif k == pygame.K_g:
                    player.GrainReserve += 2000
                elif k == pygame.K_b:
                    player.GrainReserve = max(player.GrainReserve - 2000, 0)
                elif k == pygame.K_t:
                    print(render_text(tilemap))
                    sys.stdout.flush()
                dirty = True

        if dirty:
            tilemap = build_map(player, COLS, ROWS)
            view.draw(screen, tilemap)
            pygame.display.flip()
            dirty = False

        clock.tick(FPS)


if __name__ == "__main__":
    main()
