#!/usr/bin/env python3
"""Verifica encoding JOY1DAT: replica la logica C e controlla che
le direzioni siano decodificabili come farebbe un gioco Amiga."""
def joy1dat(up,down,left,right):
    x1 = 1 if right else 0
    x9 = 1 if left else 0
    x0 = x1 ^ (1 if down else 0)
    x8 = x9 ^ (1 if up else 0)
    return (x9<<9)|(x8<<8)|(x1<<1)|x0

def decode(v):
    """come decodifica un gioco: right=bit1, left=bit9, down=bit1^bit0, up=bit9^bit8"""
    b0,b1 = v&1,(v>>1)&1
    b8,b9 = (v>>8)&1,(v>>9)&1
    return {'up':b9^b8,'down':b1^b0,'left':b9,'right':b1}

tests = [
    ("fermo",     (0,0,0,0)),
    ("su",        (1,0,0,0)),
    ("giu",       (0,1,0,0)),
    ("sinistra",  (0,0,1,0)),
    ("destra",    (0,0,0,1)),
    ("su+destra", (1,0,0,1)),
    ("giu+sinistra",(0,1,1,0)),
]
fail=0
for nome,(u,d,l,r) in tests:
    v = joy1dat(u,d,l,r)
    dec = decode(v)
    want = {'up':u,'down':d,'left':l,'right':r}
    ok = dec==want
    print(f"{'OK ' if ok else 'FAIL'} {nome}: JOY1DAT={v:04X} decode={dec}")
    if not ok: fail+=1
import sys; sys.exit(1 if fail else 0)
