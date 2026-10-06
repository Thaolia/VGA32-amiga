#!/usr/bin/env python3
"""Verifica Paula: onda quadra su canale 0, ampiezza e stereo corretti."""
import struct, wave, sys
w = wave.open(sys.argv[1],'rb')
n = w.getnframes(); frames = w.readframes(n)
L = [struct.unpack('<h', frames[i*4:i*4+2])[0] for i in range(n)]
R = [struct.unpack('<h', frames[i*4+2:i*4+4])[0] for i in range(n)]
trans = sum(1 for i in range(1,n) if (L[i]>0)!=(L[i-1]>0))
expected = round(44100 * 3 / 50)  # Makefile esegue 3 frame PAL: 3 * 20 ms
checks=[("durata 3 frame PAL corretta", abs(n-expected) <= 8),
        ("canale sx attivo", max(L)>15000 and min(L)<-15000),
        ("canale dx muto (0,3 sx / 1,2 dx)", all(x==0 for x in R)),
        ("onda quadra (transizioni)", trans>50),
        ("ampiezza max +16256", 16000<max(L)<16500),
        ("ampiezza min -16384", -16500<min(L)<-16000)]
print(f"campioni WAV: {n}, attesi circa: {expected}")
fail=0
for nm,c in checks:
    print(f"{'OK ' if c else 'FAIL'} {nm}"); 
    if not c: fail+=1
sys.exit(1 if fail else 0)
