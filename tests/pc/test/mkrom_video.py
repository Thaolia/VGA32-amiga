#!/usr/bin/env python3
"""ROM sintetica per validare il renderer (copper + denise) SENZA Kickstart.
Costruisce via CPU:
  - copper list a 0x1000:  MOVE COLOR00=rosso, MOVE COLOR01=bianco,
                           WAIT linea 0x96 (150), MOVE COLOR00=verde, fine lista
  - bitplane 1 a 0x2000 con una word 0xFFFF (16 pixel accesi a inizio riga 44)
  - finestra video standard KS (DIWSTRT 2C81 / DIWSTOP F4C1), 1 bitplane
  - DMACON: DMAEN+BPLEN+COPEN

Immagine attesa (320x312):
  righe 0..149   sfondo ROSSO   (0F00)  — riga 44: primi 16 px BIANCHI (0FFF)
  righe 150..311 sfondo VERDE   (00F0)
"""
import struct, sys

def mw(addr, val):   # move.w #val, addr.l
    return struct.pack(">HHHH", 0x33FC, val & 0xFFFF, (addr >> 16) & 0xFFFF, addr & 0xFFFF)

code = b""
# overlay off (come testrom base)
code += bytes.fromhex("13FC000300BFE201")          # move.b #3,$BFE201 (DDRA)
code += bytes.fromhex("13FC000200BFE001")          # move.b #2,$BFE001 (PRA: OVL off)

# copper list a 0x1000
cl = [0x00E0, 0x0000,   # MOVE BPL1PTH = 0      (ricarica puntatore ogni frame,
      0x00E2, 0x2000,   # MOVE BPL1PTL = 0x2000  come su hardware reale)
      0x0180, 0x0F00,   # MOVE COLOR00, rosso
      0x0182, 0x0FFF,   # MOVE COLOR01, bianco
      0x9601, 0xFFFE,   # WAIT v=0x96 (150), ve=FF
      0x0180, 0x00F0,   # MOVE COLOR00, verde
      0xFFFF, 0xFFFE]   # fine lista
for i, w in enumerate(cl):
    code += mw(0x1000 + i * 2, w)

# bitplane: una word accesa a inizio del piano
code += mw(0x2000, 0xFFFF)

# registri video
code += mw(0xDFF08E, 0x2C81)   # DIWSTRT
code += mw(0xDFF090, 0xF4C1)   # DIWSTOP
code += mw(0xDFF100, 0x1200)   # BPLCON0: 1 bitplane, color on
code += mw(0xDFF108, 0x0000)   # BPL1MOD
code += mw(0xDFF0E0, 0x0000)   # BPL1PTH
code += mw(0xDFF0E2, 0x2000)   # BPL1PTL
code += mw(0xDFF080, 0x0000)   # COP1LCH
code += mw(0xDFF082, 0x1000)   # COP1LCL
code += mw(0xDFF096, 0x8380)   # DMACON: SET | DMAEN | BPLEN | COPEN

code += bytes.fromhex("60FE")  # bra.s * (fine: loop)

rom = bytearray(0x40000)
struct.pack_into(">II", rom, 0, 0x00040000, 0x00FC0010)
rom[0x10:0x10 + len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "testrom_video.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_video: entry 0xFC0010, {len(code)} byte di codice")
