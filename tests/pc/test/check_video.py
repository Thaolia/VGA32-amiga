#!/usr/bin/env python3
"""Verifica il PPM del test video contro l'immagine attesa (ground truth)."""
import sys

def load_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b"255"
        data = f.read()
    return w, h, data

def px(data, w, x, y):
    i = (y * w + x) * 3
    return tuple(data[i:i + 3])

w, h, d = load_ppm(sys.argv[1] if len(sys.argv) > 1 else "screen.ppm")
assert (w, h) == (640, 312), f"dimensioni {w}x{h}"

RED, GREEN, WHITE = (255, 0, 0), (0, 255, 0), (255, 255, 255)
checks = [
    ("riga 10 rossa",           px(d, w, 160, 10),  RED),
    ("riga 100 rossa",          px(d, w, 160, 100), RED),
    ("riga 44 px0 bianco",      px(d, w, 0, 44),    WHITE),
    ("riga 44 px15 bianco",     px(d, w, 30, 44),   WHITE),   # 15 lores -> 30
    ("riga 44 px16 rosso",      px(d, w, 32, 44),   RED),     # 16 lores -> 32
    ("riga 45 px0 rosso",       px(d, w, 0, 45),    RED),
    ("riga 200 verde",          px(d, w, 160, 200), GREEN),
    ("riga 300 verde",          px(d, w, 160, 300), GREEN),
]
ok = True
for name, got, want in checks:
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got} atteso {want}")
sys.exit(0 if ok else 1)
