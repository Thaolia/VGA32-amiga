#!/usr/bin/env python3
"""ROM sintetica : position horizontale des WAIT du copper et passage de la ligne 255.
1 plan lores plein (COLOR01 partout dans la fenêtre), liste copper en $3000 :
  WAIT $2C01 BPLCON0=$1200 ; WAIT $4041 COLOR01=$F00 (changement en cours de ligne $40) ;
  WAIT $50E1 COLOR01=$0F0 (après la partie visible : la ligne $50 reste rouge) ;
  WAIT $FFDF WAIT $2C01 BPLCON0=$0200 (plans éteints à la ligne $12C, pas à la ligne 255) ;
  COPCON = 1 (CDANG) : WAIT $2E01 MOVE $044 (permis) COLOR00=$FF0 ; WAIT $3001 MOVE $03E
  (toujours interdit sur OCS : copper arrêté) COLOR00=$F0F jamais exécuté (custom-chips-v34 §5.8).
Verifica con test/check_copper.py sur le PPM."""
import struct, sys

def mw(addr, val):
    return struct.pack(">HHI", 0x33FC, val & 0xFFFF, addr)

H = bytes.fromhex
B = 0xDFF000
code  = H("4FF90007F000")                          # lea $7F000,sp
code += H("13FC000300BFE201") + H("13FC000200BFE001")   # overlay off
code += H("41F900010000")                          # lea $10000,a0
code += H("303C0C7F")                              # move.w #3199,d0
code += H("20FCFFFFFFFF")                          # loop: move.l #-1,(a0)+
code += H("51C8FFF8")                              # dbf d0,loop
cop = [0x00E0, 0x0001, 0x00E2, 0x0000, 0x0182, 0x000F,   # BPL1PT = $10000, COLOR01 (chaque trame)
       0x0180, 0x0000,                                   # COLOR00 (la trame précédente le change)
       0x2C01, 0xFFFE, 0x0100, 0x1200,
       0x4041, 0xFFFE, 0x0182, 0x0F00,
       0x50E1, 0xFFFE, 0x0182, 0x00F0,
       0xFFDF, 0xFFFE, 0x2C01, 0xFFFE, 0x0100, 0x0200,
       0x2E01, 0xFFFE, 0x0044, 0xFFFF, 0x0180, 0x0FF0,
       0x3001, 0xFFFE, 0x003E, 0x0000, 0x0180, 0x0F0F,
       0xFFFF, 0xFFFE]
for i, wv in enumerate(cop):
    code += mw(0x3000 + 2 * i, wv)
code += mw(B + 0x180, 0x0000) + mw(B + 0x182, 0x000F)
code += mw(B + 0x08E, 0x2C81) + mw(B + 0x090, 0x2CC1)
code += mw(B + 0x092, 0x0038) + mw(B + 0x094, 0x00D0) + mw(B + 0x108, 0x0000)
code += mw(B + 0x100, 0x0200)
code += mw(B + 0x080, 0x0000) + mw(B + 0x082, 0x3000)
code += mw(B + 0x02E, 0x0001)                      # COPCON : CDANG
code += mw(B + 0x088, 0x0000)                      # COPJMP1
code += mw(B + 0x096, 0x8380)                      # DMAEN | BPLEN | COPEN
code += H("60FE")

rom = bytearray(0x40000)
struct.pack_into(">II", rom, 0, 0x00040000, 0x00FC0010)
rom[0x10:0x10 + len(code)] = code
out = sys.argv[1] if len(sys.argv) > 1 else "testrom_copper.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_copper: {len(code)} byte di codice")
