#!/usr/bin/env python3
"""ROM sintetica : bit LOF de VPOSR et longueur de trame. À chaque changement de trame, note en
$1000+ (mots) la dernière ligne de la trame finie et le LOF de la nouvelle : 6 trames avec
BPLCON0 LACE (LOF alterne, 313/312 lignes), 3 sans LACE (LOF figé), puis VPOSW LOF=0 et 3 trames
(312 lignes). Lecture de VPOSR+VHPOSR par un MOVE.L (cohérente). Verifica : test/check_lace.py."""
import sys

def asm(parts):
    """parts : octets, label (str) ou (op, label) ; branches courtes et bsr.w."""
    size = lambda p: 4 if isinstance(p, tuple) and p[0] == "bsr" else 2
    pos, labels = 0, {}
    for p in parts:
        if isinstance(p, str): labels[p] = pos
        elif isinstance(p, tuple): pos += size(p)
        else: pos += len(p)
    out, pos = b"", 0
    for p in parts:
        if isinstance(p, str): continue
        if isinstance(p, tuple):
            d = labels[p[1]] - (pos + 2)
            if p[0] == "bsr":
                out += bytes.fromhex("6100") + (d & 0xFFFF).to_bytes(2, "big"); pos += 4
                continue
            op = {"bra": 0x60, "bne": 0x66, "bcs": 0x65, "bls": 0x63}[p[0]]
            assert -128 <= d <= 127 and d != 0
            out += bytes([op, d & 0xFF]); pos += 2
        else:
            out += p; pos += len(p)
    return out

H = bytes.fromhex
code = asm([
    H("4FF90007F000"),          # lea $7F000,sp
    H("13FC000300BFE201"), H("13FC000200BFE001"),   # overlay off
    H("33FC020400DFF100"),      # BPLCON0 = $0204 (COLOR + LACE)
    H("45F81000"),              # lea $1000.w,a2
    H("7E06"), ("bsr", "frames"),
    H("33FC020000DFF100"),      # BPLCON0 = $0200
    H("7E03"), ("bsr", "frames"),
    H("33FC000000DFF02A"),      # VPOSW = 0 (LOF=0)
    H("7E03"), ("bsr", "frames"),
    H("21FC600DF00D0104"),      # move.l #$600DF00D,$104.w
    "end", ("bra", "end"),

    "frames",
    ("bsr", "readv"),
    H("3400"),                  # move.w d0,d2   (ligne précédente)
    H("7600"),                  # moveq #0,d3    (max)
    "loop",
    ("bsr", "readv"),
    H("B042"),                  # cmp.w d2,d0
    ("bcs", "wrapped"),         # d0 < d2 : nouvelle trame
    H("3400"),                  # move.w d0,d2
    H("B043"),                  # cmp.w d3,d0
    ("bls", "loop"),
    H("3600"),                  # move.w d0,d3
    ("bra", "loop"),
    "wrapped",
    H("34C3"),                  # move.w d3,(a2)+
    H("34C1"),                  # move.w d1,(a2)+
    H("3400"),                  # move.w d0,d2
    H("7600"),                  # moveq #0,d3
    H("5347"),                  # subq.w #1,d7
    ("bne", "loop"),
    H("4E75"),                  # rts

    "readv",                    # d0 = ligne (9 bits), d1 = LOF
    H("203900DFF004"),          # move.l $DFF004,d0
    H("2200"),                  # move.l d0,d1
    H("E088"),                  # lsr.l #8,d0
    H("024001FF"),              # andi.w #$1FF,d0
    H("4841"),                  # swap d1
    H("E359"),                  # rol.w #1,d1
    H("02410001"),              # andi.w #1,d1
    H("4E75"),                  # rts
])

rom = bytearray(0x40000)
rom[0:8] = H("0004000000FC0010")
rom[0x10:0x10 + len(code)] = code
out = sys.argv[1] if len(sys.argv) > 1 else "testrom_lace.bin"
with open(out, "wb") as f:
    f.write(rom)
print(f"testrom_lace: {len(code)} byte di codice")
