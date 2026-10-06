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
]
ok = True
for name, addr, want in checks:
    got = w(addr)
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name} @{addr:05X}: {got:04X} atteso {want:04X}")
sys.exit(0 if ok else 1)
