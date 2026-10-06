#!/usr/bin/env python3
"""ROM sintetica 256KB per validare la testbench SENZA il Kickstart.
Verifica: fetch vettori in overlay, spegnimento overlay via CIA-A,
scrittura/lettura Chip RAM, rilevamento loop stretto.
Opcode assemblati a mano e verificabili su qualsiasi reference 68000.
"""
import struct, sys

code = bytes.fromhex(
    "13FC 0003 00BF E201"   # move.b #3,$BFE201.l   ; DDRA: PA0,PA1 output
    "13FC 0002 00BF E001"   # move.b #2,$BFE001.l   ; PRA: PA0=0 -> overlay OFF
    "33FC 1234 0000 0100"   # move.w #$1234,$100.l  ; scrittura in Chip RAM
    "3039 0000 0100"        # move.w $100.l,d0      ; rilettura
    "60FE"                  # bra.s *               ; loop stretto (test stallo)
    .replace(" ", ""))

rom = bytearray(0x40000)
struct.pack_into(">II", rom, 0, 0x00040000, 0x00FC0010)  # SSP, PC iniziali
rom[0x10:0x10 + len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "testrom.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom: {len(rom)} byte, entry 0xFC0010, {len(code)} byte di codice")
