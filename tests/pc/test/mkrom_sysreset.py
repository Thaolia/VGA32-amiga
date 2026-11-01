#!/usr/bin/env python3
"""ROM sintetica : /RESET système du reset clavier (a500 --sysreset 1, mem_system_reset).
Passage 1 : INTENAR en $110, puis INTENA = $C020 et DMACON = $8200, boucle.
Passage 2 (après le reset à la trame 1) : INTENAR en $112, DMACONR en $114, DDRA de CIA-A lu
avant de le réécrire en $116, $600DF00D en $104 ; compteur de passages en $100 (RAM conservée).
Verifica con test/check_sysreset.py."""
import sys
sys.path.insert(0, __import__("os").path.dirname(__file__))
from mkrom_regs import asm, H

code = asm([
    H("4FF90007F000"),             # lea $7F000,sp
    H("123900BFE201"),             # move.b $BFE201,d1   (DDRA avant réécriture)
    H("13FC000300BFE201"),         # move.b #3,$BFE201
    H("13FC000200BFE001"),         # move.b #2,$BFE001   (OVL=0)
    H("52B80100"),                 # addq.l #1,$100.w
    H("20380100"),                 # move.l $100.w,d0
    H("0C8000000001"), ("beq", "p1"),
    H("31F900DFF01C0112"),         # move.w $DFF01C,$112.w
    H("31F900DFF0020114"),         # move.w $DFF002,$114.w
    H("11C10116"),                 # move.b d1,$116.w
    H("21FC600DF00D0104"),         # move.l #$600DF00D,$104.w
    "end", ("bra", "end"),
    "p1",
    H("31F900DFF01C0110"),         # move.w $DFF01C,$110.w
    H("33FCC02000DFF09A"),         # move.w #$C020,$DFF09A
    H("33FC820000DFF096"),         # move.w #$8200,$DFF096
    "p1e", ("bra", "p1e"),
])

rom = bytearray(0x40000)
rom[0:8] = H("11114EF900FC0010")
rom[0x10:0x10 + len(code)] = code
with open(sys.argv[1] if len(sys.argv) > 1 else "testrom_sysreset.bin", "wb") as f:
    f.write(rom)
print(f"testrom_sysreset: {len(code)} byte di codice")
