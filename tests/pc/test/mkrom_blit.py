#!/usr/bin/env python3
"""ROM sintetica per validare il blitter offline. Quattro blit:
  T1 copia A->D (LF=F0), 3 word x 2 righe, modulo dest 2
  T2 shift ASH=4 con riporto tra word (LF=F0), 1 riga x 3 word
  T3 minterm EA (D = A|C con B=FFFF) con FWM/LWM parziali
  T4 linea 45° (8 px) + T5 linea bassa dx=6,dy=2 (7 px), raster 40 byte
Verifica con test/check_blit.py (implementazione indipendente in Python)."""
import struct, sys

def mw(addr, val):
    return struct.pack(">HHHH", 0x33FC, val & 0xFFFF, (addr >> 16) & 0xFFFF, addr & 0xFFFF)

code  = bytes.fromhex("13FC000300BFE201")   # overlay off
code += bytes.fromhex("13FC000200BFE001")

# sorgente a 0x3000: FFFF 0F0F 3C3C / 1111 2222 3333
for i, wv in enumerate([0xFFFF, 0x0F0F, 0x3C3C, 0x1111, 0x2222, 0x3333]):
    code += mw(0x3000 + i * 2, wv)

B = 0xDFF000
# T1: copia 3x2, dest 0x4000, dmod=2 (dest scala 8 byte/riga)
code += mw(B+0x040, 0x09F0)  # CON0: USEA|USED, LF=F0 (D=A)
code += mw(B+0x042, 0x0000)
code += mw(B+0x044, 0xFFFF) + mw(B+0x046, 0xFFFF)
code += mw(B+0x050, 0x0000) + mw(B+0x052, 0x3000)   # APT
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x4000)   # DPT
code += mw(B+0x064, 0x0000) + mw(B+0x066, 0x0002)   # AMOD, DMOD
code += mw(B+0x058, (2 << 6) | 3)                   # h=2 w=3

# T2: shift ASH=4, src=3000 (FFFF 0F0F 3C3C), dest 0x4100, 1x3
code += mw(B+0x040, 0x49F0)  # ASH=4, USEA|USED, LF=F0
code += mw(B+0x050, 0x0000) + mw(B+0x052, 0x3000)
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x4100)
code += mw(B+0x064, 0x0000) + mw(B+0x066, 0x0000)
code += mw(B+0x058, (1 << 6) | 3)

# T3: D = A|C (LF=EA, B=FFFF costante), FWM=00FF LWM=FF00, dest 0x4200 pre-caricata
code += mw(0x4200, 0x5050) + mw(0x4202, 0x0505)
code += mw(B+0x072, 0xFFFF)                          # BLTBDAT
code += mw(B+0x040, 0x0BEA)  # USEA|USEC|USED, LF=EA
code += mw(B+0x044, 0x00FF) + mw(B+0x046, 0xFF00)
code += mw(B+0x050, 0x0000) + mw(B+0x052, 0x3000)
code += mw(B+0x048, 0x0000) + mw(B+0x04A, 0x4200)   # CPT
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x4200)
code += mw(B+0x058, (1 << 6) | 2)

# T4: linea 45° da (0,0), len 8, raster 0x5000 largo 40 byte
code += mw(B+0x074, 0x8000)                          # BLTADAT
code += mw(B+0x044, 0xFFFF)
code += mw(B+0x072, 0xFFFF)                          # texture piena
code += mw(B+0x040, 0x0BEA)  # ASH=0 (x=0), USEA|USEC|USED, LF=EA
code += mw(B+0x042, 0x0001)  # LINE, sign=0 (acc iniziale 16 > 0)
code += mw(B+0x048, 0x0000) + mw(B+0x04A, 0x5000)
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x5000)
code += mw(B+0x052, 0x0010)                          # BLTAPTL = 4*8-2*8 = 16
code += mw(B+0x064, 0x0000)                          # AMOD = 4*(dy-dx) = 0
code += mw(B+0x062, 0x0020)                          # BMOD = 4*dy = 32
code += mw(B+0x060, 0x0028)                          # CMOD = 40
code += mw(B+0x058, (8 << 6) | 2)

# T5: linea bassa (0,0)->(6,2), len 7, raster 0x6000
code += mw(B+0x042, 0x0051)  # LINE | SIGN | SUD (dx>dy: asse maggiore X)
code += mw(B+0x048, 0x0000) + mw(B+0x04A, 0x6000)
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x6000)
code += mw(B+0x052, 0xFFFC)                          # BLTAPTL = 4*2-2*6 = -4
code += mw(B+0x064, 0xFFF0)                          # AMOD = 4*(2-6) = -16
code += mw(B+0x062, 0x0008)                          # BMOD = 4*2 = 8
code += mw(B+0x058, (7 << 6) | 2)

# T6: linea ripida (0,0)->(2,6), maggiore Y (SUD=0), raster 0x7000
code += mw(B+0x042, 0x0041)  # LINE | SIGN, SUD=0 (dy>dx: asse maggiore Y)
code += mw(B+0x048, 0x0000) + mw(B+0x04A, 0x7000)
code += mw(B+0x054, 0x0000) + mw(B+0x056, 0x7000)
code += mw(B+0x052, 0xFFFC)                          # BLTAPTL = 4*2-2*6 = -4
code += mw(B+0x064, 0xFFF0)                          # AMOD = 4*(2-6) = -16
code += mw(B+0x062, 0x0008)                          # BMOD = 4*2 = 8
code += mw(B+0x058, (7 << 6) | 2)

code += bytes.fromhex("60FE")

rom = bytearray(0x40000)
struct.pack_into(">II", rom, 0, 0x00040000, 0x00FC0010)
rom[0x10:0x10 + len(code)] = code
out = sys.argv[1] if len(sys.argv) > 1 else "testrom_blit.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_blit: {len(code)} byte di codice")
