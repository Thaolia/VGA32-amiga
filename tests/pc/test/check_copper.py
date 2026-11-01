#!/usr/bin/env python3
"""Verifica il PPM della ROM test/mkrom_copper.py (WAIT horizontal, ligne 255)."""
import sys

data = open(sys.argv[1] if len(sys.argv) > 1 else "test_copper.ppm", "rb").read()
hdr = data.split(b"\n", 3)
w, h = map(int, hdr[1].split())
px = hdr[3]
def at(x, y):                       # x en pixels lores (le PPM double chaque pixel)
    o = 3 * (y * w + 2 * x)
    return tuple(px[o:o + 3])

BLUE, RED, GREEN, BLACK = (0, 0, 255), (255, 0, 0), (0, 255, 0), (0, 0, 0)
checks = [
    ("ligne $30 : couleur initiale", 100, 0x30, BLUE),
    ("ligne $40 : debut de ligne avant le WAIT h=$40", 2, 0x40, BLUE),
    ("ligne $40 : apres le WAIT h=$40", 100, 0x40, RED),
    ("ligne $41", 100, 0x41, RED),
    ("ligne $50 : changement apres la partie visible", 319, 0x50, RED),
    ("ligne $51", 5, 0x51, GREEN),
    ("ligne 272 (> 255) : plans encore allumes", 100, 272, GREEN),
    ("ligne $12D : plans eteints (WAIT $2C01 apres $FFDF)", 100, 0x12D, BLACK),
]
ok = True
for name, x, y, want in checks:
    got = at(x, y)
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got} atteso {want}")
sys.exit(0 if ok else 1)
