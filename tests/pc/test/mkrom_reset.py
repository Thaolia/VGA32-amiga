#!/usr/bin/env python3
"""ROM sintetica : instruction RESET (reboot à chaud). Comme le Kickstart, $1111 puis JMP au début
du code en $2 (visible en $2 sous l'overlay). Chaque passage note DDRA de CIA-A (lu avant de retirer
l'overlay) en $110+n et incrémente le compteur $100 en chip RAM :
  passage 1 : RESET depuis la ROM puis MOVE.L $4,A0 / JMP (A0) (chemin de reboot de la ROM) ;
  passage 2 : RESET / JMP (A0) en chip RAM avec A0 = 2 (reboot logiciel, file de préchargement) ;
  passage 3 : $600DF00D en $104, boucle.
Verifica con test/check_reset.py."""
import struct, sys

def asm(parts):
    """parts : octets ou ('bra', label) / ('beq', label) / label (str). Branches courtes."""
    pos, labels = 0, {}
    for p in parts:
        if isinstance(p, str): labels[p] = pos
        elif isinstance(p, tuple): pos += 2
        else: pos += len(p)
    out, pos = b"", 0
    for p in parts:
        if isinstance(p, str): continue
        if isinstance(p, tuple):
            op = {"bra": 0x60, "beq": 0x67}[p[0]]
            d = labels[p[1]] - (pos + 2)
            assert -128 <= d <= 127 and d != 0
            out += bytes([op, d & 0xFF]); pos += 2
        else:
            out += p; pos += len(p)
    return out

H = bytes.fromhex
code = asm([
    H("4FF90007F000"),        # lea $7F000,sp
    H("123900BFE201"),        # move.b $BFE201,d1   (DDRA avant de retirer l'overlay)
    H("13FC000300BFE201"),    # move.b #3,$BFE201
    H("13FC000200BFE001"),    # move.b #2,$BFE001   (OVL=0)
    H("52B80100"),            # addq.l #1,$100.w
    H("20380100"),            # move.l $100.w,d0
    H("43F80110"),            # lea $110.w,a1
    H("13810000"),            # move.b d1,0(a1,d0.w)
    H("0C8000000001"), ("beq", "p1"),   # cmpi.l #1,d0
    H("0C8000000002"), ("beq", "p2"),   # cmpi.l #2,d0
    H("21FC600DF00D0104"),    # move.l #$600DF00D,$104.w
    "end", ("bra", "end"),
    "p1",
    H("4E70"),                # reset
    H("20780004"),            # move.l $4.w,a0
    H("4ED0"),                # jmp (a0)
    "p2",
    H("21FC4E704ED00200"),    # move.l #$4E704ED0,$200.w   (reset / jmp (a0))
    H("307C0002"),            # movea.w #2,a0
    H("4EF80200"),            # jmp $200.w
])

rom = bytearray(0x40000)
rom[0:8] = H("11114EF900FC0010")  # $1111, JMP $FC0010 ; vecteurs : SSP=$11114EF9, PC=$FC0010
rom[0x10:0x10 + len(code)] = code
out = sys.argv[1] if len(sys.argv) > 1 else "testrom_reset.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_reset: {len(code)} byte di codice")
