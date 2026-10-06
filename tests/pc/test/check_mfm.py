#!/usr/bin/env python3
"""Decoder MFM AmigaDOS indipendente: verifica la rivoluzione contro l'ADF.
Scritto dalla specifica del formato, NON dall'encoder (lezione ottanti)."""
import struct, sys

rev = open(sys.argv[1], 'rb').read()
adf = open(sys.argv[2], 'rb').read()
track = int(sys.argv[3])

bits = ''.join(f'{b:08b}' for b in rev) * 2   # x2: gestisce il wrap della rivoluzione

def deinterleave(mfmbits, nbytes):
    """da bit MFM (clock+dato alternati) estrae i bit dati, poi ricompone odd/even"""
    data_bits = mfmbits[1::2]                  # scarto i clock (posizioni pari)
    half = nbytes * 4                          # bit per meta' (dispari o pari)
    odd, even = data_bits[:half], data_bits[half:half*2]
    out = bytearray()
    for i in range(0, half, 4):
        byte = 0
        for j in range(4):
            byte = (byte << 2) | (int(odd[i+j]) << 1) | int(even[i+j])
        out.append(byte)
    return bytes(out)

def cksum(mfmbits):
    """XOR dei longword MFM & 0x55555555"""
    c = 0
    for i in range(0, len(mfmbits), 32):
        c ^= int(mfmbits[i:i+32], 2) & 0x55555555
    return c

SYNC = '0100010010001001' * 2                  # 4489 4489
found = {}
i = bits.find(SYNC)
while i >= 0 and len(found) < 11:
    p = i + 32                                 # dopo il sync
    info = deinterleave(bits[p:p+64], 4);           p_info = p; p += 64
    label = deinterleave(bits[p:p+256], 16);        p += 256
    hck  = struct.unpack(">I", deinterleave(bits[p:p+64], 4))[0];  p += 64
    dck  = struct.unpack(">I", deinterleave(bits[p:p+64], 4))[0];  p += 64
    data = deinterleave(bits[p:p+8192], 512)
    fmt, trk, sec, tog = info
    if fmt == 0xFF and trk == track and sec < 11 and sec not in found:
        hck_calc = cksum(bits[p_info:p_info+320])          # info+label
        dck_calc = cksum(bits[p:p+8192])
        ref = adf[(track*11+sec)*512:(track*11+sec+1)*512]
        found[sec] = (hck == hck_calc, dck == dck_calc, data == ref, tog)
    i = bits.find(SYNC, i + 32)

ok = len(found) == 11
print(f"settori trovati: {len(found)}/11")
for s in sorted(found):
    h, d, m, tog = found[s]
    good = h and d and m and tog == 11 - s
    ok &= good
    print(f"{'OK ' if good else 'FAIL'} settore {s:2d}: hck={'ok' if h else 'BAD'} "
          f"dck={'ok' if d else 'BAD'} dati={'ok' if m else 'DIVERSI'} fino_al_gap={tog}")
sys.exit(0 if ok else 1)
