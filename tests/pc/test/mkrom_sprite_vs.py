#!/usr/bin/env python3
"""ROM sintetica : sprite positionné par le copper comme un VSprite de graphics (graphics-gels-v34
§5.8). Le copper écrit SPR1PTH/PTL vers les données d'image SANS en-tête POS/CTL, puis SPR1POS et
SPR1CTL : le sprite part de ces registres, le DMA lit les données à partir du pointeur.
Sprite 1 rouge (COLOR17), 16 x 4, HSTART comme test/mkrom_sprite.py (x lores 50), lignes $A0-$A3.
Sprite 5 vert (COLOR25), classique (en-tête POS/CTL en mémoire), armé par SPR5PTL en début de trame,
lignes $B0-$B3 : chaque SPRxPTL doit armer son propre canal (pointeurs espacés de 4 octets).
Verifica con test/check_sprite_vs.py sur le PPM."""
import struct, sys

def mw(addr, val):
    return struct.pack(">HHI", 0x33FC, val & 0xFFFF, addr)

H = bytes.fromhex
B = 0xDFF000
code  = H("4FF90007F000")                               # lea $7F000,sp
code += H("13FC000300BFE201") + H("13FC000200BFE001")   # overlay off
for i in range(4):                                      # image en $52000 : 4 lignes, puis 0,0
    code += mw(0x52000 + 4 * i, 0xFFFF) + mw(0x52002 + 4 * i, 0x0000)
code += mw(0x52010, 0x0000) + mw(0x52012, 0x0000)
code += mw(0x52100, 0xB059) + mw(0x52102, 0xB401)       # sprite 5 en $52100 : POS, CTL
for i in range(4):
    code += mw(0x52104 + 4 * i, 0xFFFF) + mw(0x52106 + 4 * i, 0x0000)
code += mw(0x52114, 0x0000) + mw(0x52116, 0x0000)
cop = [0x0134, 0x0005, 0x0136, 0x2100,                  # SPR5PTH/PTL -> $52100 (avec en-tête)
       0x9001, 0xFFFE,
       0x0124, 0x0005, 0x0126, 0x2000,                  # SPR1PTH/PTL -> $52000 (données)
       0x0148, 0xA059, 0x014A, 0xA401,                  # SPR1POS / SPR1CTL (H0 = 1)
       0xFFFF, 0xFFFE]
for i, wv in enumerate(cop):
    code += mw(0x3000 + 2 * i, wv)
code += mw(B + 0x08E, 0x2C81) + mw(B + 0x090, 0xF4C1)
code += mw(B + 0x092, 0x0038) + mw(B + 0x094, 0x00D0)
code += mw(B + 0x100, 0x1200) + mw(B + 0x108, 0x0000)
code += mw(B + 0x0E0, 0x0004) + mw(B + 0x0E2, 0x0000)  # plan vide en $40000
code += mw(B + 0x180, 0x0000) + mw(B + 0x1A2, 0x0F00)  # COLOR00 noir, COLOR17 rouge
code += mw(B + 0x1B2, 0x00F0)                           # COLOR25 vert (sprites 4-5)
code += mw(B + 0x080, 0x0000) + mw(B + 0x082, 0x3000)
code += mw(B + 0x088, 0x0000)                           # COPJMP1
code += mw(B + 0x096, 0x83A0)                           # DMAEN | BPLEN | COPEN | SPREN
code += H("60FE")

rom = bytearray(0x40000)
struct.pack_into(">II", rom, 0, 0x00040000, 0x00FC0010)
rom[0x10:0x10 + len(code)] = code
out = sys.argv[1] if len(sys.argv) > 1 else "testrom_sprite_vs.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_sprite_vs: {len(code)} byte di codice")
