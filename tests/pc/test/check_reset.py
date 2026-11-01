#!/usr/bin/env python3
"""Verifica il dump chip RAM della ROM test/mkrom_reset.py (reboot à chaud par RESET)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "reset.ram", "rb").read()
def l(a): return int.from_bytes(ram[a:a + 4], "big")

checks = [
    ("compteur de passages (RAM conservée)", l(0x100), 3),
    ("passage 3 atteint", l(0x104), 0x600DF00D),
    ("DDRA au boot", ram[0x111], 0),
    ("DDRA après RESET depuis la ROM", ram[0x112], 0),
    ("DDRA après RESET depuis la chip RAM", ram[0x113], 0),
]
ok = True
for name, got, want in checks:
    good = got == want
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got:X} atteso {want:X}")
sys.exit(0 if ok else 1)
