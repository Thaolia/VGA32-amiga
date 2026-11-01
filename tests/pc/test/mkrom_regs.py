#!/usr/bin/env python3
"""ROM sintetica : registres lus par la ROM et les logiciels (analyses KickSlopStart du 2026-11-01).
Résultats en chip RAM, vérifiés par test/check_regs.py :
  $200  max de l'octet bas de VHPOSR sur ~1000 lectures (position horizontale, < $E4)
  $202  SERDATR au repos (TBE, TSRE, RXD) ; $204 INTREQR après écriture de SERDAT (TBE, bit 0) ;
  $206  SERDATR après l'écriture
  $208  long écrit en $80010 relu en $10 ; $20C long écrit en $180014 relu en $14 (miroir chip 512 Ko)
  $210  DMACONR après un blit à résultat nul (BZERO, bit 13) ; $212 après un blit non nul
  $214/$216/$218  POTGOR avec POTGO = $0800 (OUTLY, DATLY bas), $0C00 (DATLY haut), $0000 (entrée)
  $220  CIA-A PRA avec DF0 sélectionné, lecteur vide (/WPRO actif, bit 3 à 0 : FLOPPY_SPEC D9)
  $21C  $600DF00D = fin atteinte"""
import sys

def asm(parts):
    """parts : octets, label (str) ou (op, label) avec op = bra/beq/bls (8 bits) ou dbra_dN."""
    def size(p): return 4 if p[0].startswith("dbra") else 2
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
            if p[0].startswith("dbra"):
                out += (0x51C8 | int(p[0][-1])).to_bytes(2, "big") + (d & 0xFFFF).to_bytes(2, "big")
            else:
                assert -128 <= d <= 127 and d != 0
                out += bytes([{"bra": 0x60, "beq": 0x67, "bls": 0x63}[p[0]], d & 0xFF])
            pos += size(p)
        else:
            out += p; pos += len(p)
    return out

H = bytes.fromhex
def movew_imm(v, a): return H("33FC") + v.to_bytes(2, "big") + a.to_bytes(4, "big")   # move.w #v,a.l
def movew_to(a, d):  return H("31F9") + a.to_bytes(4, "big") + d.to_bytes(2, "big")   # move.w a.l,d.w
def movel_imm(v, a): return H("23FC") + v.to_bytes(4, "big") + a.to_bytes(4, "big")   # move.l #v,a.l
code = asm([
    H("4FF90007F000"),             # lea $7F000,sp
    H("13FC000300BFE201"),         # move.b #3,$BFE201
    H("13FC000200BFE001"),         # move.b #2,$BFE001   (OVL=0)
    # VHPOSR : max de l'octet bas
    H("7600"),                     # moveq #0,d3
    H("383C03E7"),                 # move.w #999,d4
    "hp", H("123900DFF007"),       # move.b $DFF007,d1
    H("B203"),                     # cmp.b d3,d1
    ("bls", "hs"),
    H("1601"),                     # move.b d1,d3
    "hs", ("dbra_d4", "hp"),
    H("11C30200"),                 # move.b d3,$200.w
    # port série
    movew_imm(0x0001, 0xDFF09C),   # INTREQ : efface TBE
    movew_to(0xDFF018, 0x202),
    movew_imm(0x0141, 0xDFF030),   # SERDAT
    H("383C00C8"), "sd", ("dbra_d4", "sd"),
    movew_to(0xDFF01E, 0x204),
    movew_to(0xDFF018, 0x206),
    # miroir de la chip RAM
    movel_imm(0xCAFEBABE, 0x80010),
    H("21F800100208"),             # move.l $10.w,$208.w
    movel_imm(0x0BADF00D, 0x180014),
    H("21F80014020C"),             # move.l $14.w,$20C.w
    # BZERO : D = A (BLTADAT constant), 1 ligne x 4 mots
    movew_imm(0x8240, 0xDFF096),   # DMAEN | BLTEN
    movew_imm(0x01F0, 0xDFF040), movew_imm(0x0000, 0xDFF042),
    movew_imm(0xFFFF, 0xDFF044), movew_imm(0xFFFF, 0xDFF046),
    movew_imm(0x0000, 0xDFF066), movew_imm(0x0000, 0xDFF074),
    movel_imm(0x3000, 0xDFF054), movew_imm(0x0044, 0xDFF058),
    movew_to(0xDFF002, 0x210),
    movew_imm(0x1234, 0xDFF074),
    movel_imm(0x3000, 0xDFF054), movew_imm(0x0044, 0xDFF058),
    movew_to(0xDFF002, 0x212),
    # POTGO / POTGOR
    movew_imm(0x0800, 0xDFF034), movew_to(0xDFF016, 0x214),
    movew_imm(0x0C00, 0xDFF034), movew_to(0xDFF016, 0x216),
    movew_imm(0x0000, 0xDFF034), movew_to(0xDFF016, 0x218),
    # /WPRO lecteur vide
    H("13FC00FF00BFD300"),         # move.b #$FF,$BFD300  (DDRB)
    H("13FC007700BFD100"),         # move.b #$77,$BFD100  (/MTR et /SEL0 bas)
    H("11F900BFE0010220"),         # move.b $BFE001,$220.w
    H("13FC00FF00BFD100"),         # move.b #$FF,$BFD100  (désélection)
    H("21FC600DF00D021C"),         # move.l #$600DF00D,$21C.w
    "end", ("bra", "end"),
])

if __name__ == "__main__":   # asm et H sont réutilisés par mkrom_sysreset.py
    rom = bytearray(0x40000)
    rom[0:8] = H("11114EF900FC0010")  # $1111, JMP $FC0010 ; vecteurs : SSP=$11114EF9, PC=$FC0010
    rom[0x10:0x10 + len(code)] = code
    out = sys.argv[1] if len(sys.argv) > 1 else "testrom_regs.bin"
    with open(out, "wb") as f:
        f.write(rom)
    print(f"testrom_regs: {len(code)} byte di codice")
