#!/usr/bin/env python3
"""Verifica il dump chip RAM della ROM test/mkrom_regs.py (registres custom, miroir chip)."""
import sys

ram = open(sys.argv[1] if len(sys.argv) > 1 else "regs.ram", "rb").read()
rmb = len(sys.argv) > 2 and sys.argv[2] == "rmb"   # run avec le bouton droit tenu
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
    ("PRA, DF0 vide sélectionné : /WPRO actif (bit 3 à 0)", ram[0x220], ram[0x220] & 0x08 == 0),
    ("PRA, DF0 vide sélectionné : /RDY inactif (bit 5 à 1)", ram[0x220], ram[0x220] & 0x20 != 0),
    ("butée de la tête : 83 pas pour revenir au cylindre 0", w(0x226), w(0x226) == 83),
    ("INTREQ bit 14 : niveau 6 servi une fois", w(0x224), w(0x224) == 1),
    ("fin atteinte", l(0x21C), l(0x21C) == 0x600DF00D),
]
# ET câblé : la sortie haute n'empêche pas le bouton de tirer la ligne à la masse
for pot, a in ((0x0C00, 0x216), (0x0000, 0x218), (0x0F00, 0x222)):
    if rmb:
        checks.append((f"POTGOR, POTGO=${pot:04X}, bouton droit tenu : bit 10 à 0", w(a), w(a) & 0x0400 == 0))
    else:
        checks.append((f"POTGOR, POTGO=${pot:04X}, bouton relâché : bit 10 à 1", w(a), w(a) & 0x0400 != 0))
ok = True
for name, got, good in checks:
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} {name}: {got:X}")
sys.exit(0 if ok else 1)
