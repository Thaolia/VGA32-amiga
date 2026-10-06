#!/usr/bin/env python3
"""ROM test audio: canale 0 suona un'onda quadra (8 word: +127/-128 alternati)
pre-caricata a 0x40000. AUD0PER=200, AUD0VOL=64. Verifica campioni in uscita."""
import sys
rom = bytearray(256*1024)
def w16(o,v): rom[o]=(v>>8)&0xFF; rom[o+1]=v&0xFF
def w32(o,v): w16(o,v>>16); w16(o+2,v&0xFFFF)
w32(0,0x00080000); w32(4,0x00FC0010)
words=[]
def emit(*ws): words.extend(ws)
def W32(v): words.append((v>>16)&0xFFFF); words.append(v&0xFFFF)
emit(0x13FC,0x0003); W32(0x00BFE201)
emit(0x13FC,0x0002); W32(0x00BFE001)
emit(0x41F9); W32(0x00DFF000)
# AUD0LC = 0x40000
emit(0x317C,0x0004,0x0A0); emit(0x317C,0x0000,0x0A2)
emit(0x317C,0x0008,0x0A4)               # AUD0LEN = 8 word
emit(0x317C,0x00C8,0x0A6)               # AUD0PER = 200
emit(0x317C,0x0040,0x0A8)               # AUD0VOL = 64
emit(0x317C,0x8201,0x096)               # DMACON: SET|DMAEN(0x200)|AUD0(0x1)
here=len(words); emit(0x6000)
words[here]=0x6000 | ((here-(here+1))*2 & 0xFF)
p=0x10
for w in words: w16(p,w); p+=2
open(sys.argv[1],'wb').write(rom)
# pattern: 8 word = onda quadra. word alta=+127, bassa=-128 -> 0x7F80 ripetuto
pat = bytearray()
for _ in range(8):
    pat += bytes([0x7F, 0x80])   # byte alto +127, byte basso -128
open(sys.argv[1].replace('.bin','_pat.bin'),'wb').write(pat)
print("testrom_audio + pattern onda quadra")
