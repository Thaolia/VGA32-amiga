#!/usr/bin/env python3
"""ROM test scroll v2: pattern PRE-CARICATO in chip ram via --ramload.
La ROM fa solo overlay-off + setup registri con BPLCON1=3."""
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
emit(0x317C,0x2C81,0x08E); emit(0x317C,0xF4C1,0x090)
emit(0x317C,0x0038,0x092); emit(0x317C,0x00D0,0x094)
emit(0x317C,0x1200,0x100)
emit(0x317C,0x0003,0x102)               # BPLCON1 = 3 (scroll 3px lores)
emit(0x317C,0x0000,0x108); emit(0x317C,0x0000,0x10A)
emit(0x317C,0x0005,0x0E0); emit(0x317C,0x0000,0x0E2)
emit(0x317C,0x0000,0x180); emit(0x317C,0x0FFF,0x182)
emit(0x317C,0x8300,0x096)
here=len(words); emit(0x6000)
words[here]=0x6000 | ((here-(here+1))*2 & 0xFF)
p=0x10
for w in words: w16(p,w); p+=2
open(sys.argv[1],'wb').write(rom)

# genero anche il pattern: 256 righe x 40 byte, prima word FFFF resto 0
pat = bytearray(256*40)
for y in range(256):
    pat[y*40] = 0xFF; pat[y*40+1] = 0xFF
open(sys.argv[1].replace('.bin','_pat.bin'),'wb').write(pat)
print(f"testrom_scroll + pattern")
