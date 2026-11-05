#!/usr/bin/env python3
"""Verifica il PPM della ROM test/mkrom_sprite_vs.py : sprite 1 rouge à (100-131, $A0-$A3), sprite 5
vert à (100-131, $B0-$B3), pas de rouge ailleurs."""
import sys
d = open(sys.argv[1], 'rb').read()
idx = d.index(b'255\n') + 4
hdr = d[:idx].split(); w, h = int(hdr[1]), int(hdr[2])
def px(x, y):
    o = idx + (y * w + x) * 3; return (d[o], d[o + 1], d[o + 2])
R, N, G = (255, 0, 0), (0, 0, 0), (0, 255, 0)
checks = [("sprite haut gauche", px(100, 0xA0), R), ("sprite bas droite", px(131, 0xA3), R),
          ("au-dessus (aucune ligne d'image prise pour POS/CTL)", px(115, 0x9F), N),
          ("en dessous", px(115, 0xA4), N), ("à gauche", px(98, 0xA1), N), ("à droite", px(133, 0xA1), N),
          ("sprite 5 haut gauche", px(100, 0xB0), G), ("sprite 5 bas droite", px(131, 0xB3), G),
          ("sprite 5 : rien au-dessus", px(115, 0xAF), N), ("sprite 5 : rien en dessous", px(115, 0xB4), N)]
red_elsewhere = sum(1 for y in range(h) for x in range(0, w, 2)
                    if px(x, y) == R and not (100 <= x <= 131 and 0xA0 <= y <= 0xA3))
checks.append(("aucun pixel rouge ailleurs", red_elsewhere, 0))
fail = 0
for n, g, wt in checks:
    ok = g == wt
    print(f"{'OK ' if ok else 'FAIL'} {n}: {g} atteso {wt}")
    fail += not ok
sys.exit(1 if fail else 0)
