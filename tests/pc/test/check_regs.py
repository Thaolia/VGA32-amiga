#!/usr/bin/env python3
"""Verifica il dump chip RAM della ROM test/mkrom_regs.py (registres custom, miroir chip)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "regs.ram", "rb").read()
def w(a): return int.from_bytes(ram[a:a + 2], "big")
def l(a): return int.from_bytes(ram[a:a + 4], "big")

checks = [
    ("VHPOSR : position horizontale atteinte (>= $80, < $E4)", ram[0x200], 0x80 <= ram[0x200] < 0xE4),
    ("SERDATR au repos : TBE | TSRE | RXD", w(0x202), w(0x202) & 0x3800 == 0x3800),
    ("INTREQR après SERDAT : TBE (bit 0)", w(0x204), w(0x204) & 1 == 1),
    ("SERDATR après SERDAT : TBE", w(0x206), w(0x206) & 0x2000 != 0),
    ("miroir chip $80010 -> $10", l(0x208), l(0x208) == 0xCAFEBABE),
    ("miroir chip $180014 -> $14", l(0x20C), l(0x20C) == 0x0BADF00D),
    ("DMACONR après blit nul : BZERO", w(0x210), w(0x210) & 0x2000 != 0),
    ("DMACONR après blit non nul : pas de BZERO", w(0x212), w(0x212) & 0x2000 == 0),
    ("POTGOR, POTGO=$0800 (DATLY sorti bas) : bit 10 à 0", w(0x214), w(0x214) & 0x0400 == 0),
    ("POTGOR, POTGO=$0C00 (DATLY sorti haut) : bit 10 à 1", w(0x216), w(0x216) & 0x0400 != 0),
    ("POTGOR, POTGO=$0000 (entrée, bouton relâché) : bit 10 à 1", w(0x218), w(0x218) & 0x0400 != 0),
    ("PRA, DF0 vide sélectionné : /WPRO actif (bit 3 à 0)", ram[0x220], ram[0x220] & 0x08 == 0),
    ("PRA, DF0 vide sélectionné : /RDY inactif (bit 5 à 1)", ram[0x220], ram[0x220] & 0x20 != 0),
    ("fin atteinte", l(0x21C), l(0x21C) == 0x600DF00D),
]
ok = True
for name, got, good in checks:
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got:X}")
sys.exit(0 if ok else 1)
