#!/usr/bin/env python3
"""Verifica il dump chip RAM della ROM test/mkrom_sysreset.py (/RESET système, RAM conservée)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "sysreset.ram", "rb").read()
def w(a): return int.from_bytes(ram[a:a + 2], "big")
def l(a): return int.from_bytes(ram[a:a + 4], "big")

checks = [
    ("compteur de passages (RAM conservée)", l(0x100), 2),
    ("passage 2 atteint", l(0x104), 0x600DF00D),
    ("INTENAR au boot", w(0x110), 0),
    ("INTENAR après le reset", w(0x112), 0),
    ("DMACONR après le reset (sans BZERO)", w(0x114) & 0x1FFF, 0),
    ("DDRA de CIA-A après le reset", ram[0x116], 0),
]
ok = True
for name, got, want in checks:
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got:X} atteso {want:X}")
sys.exit(0 if ok else 1)
