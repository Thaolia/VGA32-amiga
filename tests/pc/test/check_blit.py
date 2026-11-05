#!/usr/bin/env python3
"""Verifica il dump chip RAM contro i risultati attesi (calcolati a mano da HRM)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "blit.ram", "rb").read()
def w(a): return ram[a] << 8 | ram[a + 1]

checks = [
    # T1: copia 3x2 con DMOD=2
    ("T1 r0w0", 0x4000, 0xFFFF), ("T1 r0w1", 0x4002, 0x0F0F), ("T1 r0w2", 0x4004, 0x3C3C),
    ("T1 r1w0", 0x4008, 0x1111), ("T1 r1w1", 0x400A, 0x2222), ("T1 r1w2", 0x400C, 0x3333),
    # T2: shift ASH=4 con riporto
    ("T2 w0", 0x4100, 0x0FFF), ("T2 w1", 0x4102, 0xF0F0), ("T2 w2", 0x4104, 0xF3C3),
    # T3: D=A|C con FWM=00FF LWM=FF00
    ("T3 w0", 0x4200, 0x50FF), ("T3 w1", 0x4202, 0x0F05),
    # T4: linea 45°: riga y ha il bit 0x8000>>y
    *[(f"T4 y{y}", 0x5000 + 40 * y, 0x8000 >> y) for y in range(8)],
    # T5: linea (0,0)-(6,2): pixel (0,0)(1,0)(2,1)(3,1)(4,1)(5,2)(6,2)
    ("T5 y0", 0x6000, 0xC000), ("T5 y1", 0x6028, 0x3800), ("T5 y2", 0x6050, 0x0600),
    # T6: linea ripida (0,0)-(2,6): pixel (0,0)(0,1)(1,2)(1,3)(1,4)(2,5)(2,6)
    ("T6 y0", 0x7000, 0x8000), ("T6 y1", 0x7028, 0x8000), ("T6 y2", 0x7050, 0x4000),
    ("T6 y3", 0x7078, 0x4000), ("T6 y4", 0x70A0, 0x4000), ("T6 y5", 0x70C8, 0x2000),
    ("T6 y6", 0x70F0, 0x2000),
    # T7: EFE sur $0810, du bit 0 vers le bit 15 : bit 4 ouvre (gardé), bit 11 ferme (perdu)
    ("T7 fill exclusif", 0x4300, 0x07F0),
    # T8 (graphics-line-v34 §10.2, ROM réelle observée)
    *[(f"T8a $FF00 vers la droite w{i}", 0x8000 + 40 + 2 * i, 0xFF00) for i in range(3)],
    *[(f"T8b $FF00 vers la gauche w{i}", 0x8000 + 120 + 2 * i, 0x7F80) for i in range(3)],
    *[(f"T8c verticale x=40 y{y}", 0x8004 + 40 * y, 0x0080 if (8 <= y <= 15 or 24 <= y <= 30) else 0)
      for y in range(8, 31)],
    ("T8d $8001 w0", 0x8000 + 200, 0x8001), ("T8d $8001 w1", 0x8000 + 202, 0x8001),
    ("T8d $8001 w2", 0x8000 + 204, 0x8000),
]
ok = True
for name, addr, want in checks:
    got = w(addr)
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name} @{addr:05X}: {got:04X} atteso {want:04X}")
sys.exit(0 if ok else 1)
