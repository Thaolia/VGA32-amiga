#!/usr/bin/env python3
"""Verifica il dump chip RAM della ROM test/mkrom_lace.py (LOF, trames longues/courtes)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "lace.ram", "rb").read()
def w(a): return ram[a] << 8 | ram[a + 1]

ent = [(w(0x1000 + 4 * i), w(0x1002 + 4 * i)) for i in range(12)]
want = [(312, 0), (311, 1), (312, 0), (311, 1), (312, 0), (311, 1),   # LACE : LOF alterne
        (312, 1), (312, 1), (312, 1),                                 # sans LACE : LOF figé
        (None, 0), (311, 0), (311, 0)]                                # VPOSW LOF=0 : trames courtes
ok = w(0x104) == 0x600D
print(f"{'OK ' if ok else 'FAIL'} fin du programme")
for i, ((gl, gf), (wl, wf)) in enumerate(zip(ent, want)):
    good = gf == wf and (wl is None or gl == wl)
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} trame {i}: derniere ligne {gl} LOF {gf} (attendu {wl} / {wf})")
sys.exit(0 if ok else 1)
