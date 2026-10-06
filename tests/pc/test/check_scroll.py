#!/usr/bin/env python3
"""Verifica scroll BPLCON1=3: pattern spostato di 3px lores (6 fb) a riga 44."""
import sys
d=open(sys.argv[1],'rb').read()
idx=d.index(b'255\n')+4
hdr=d[:idx].split(); w=int(hdr[1])
def px(x,y):
    o=idx+(y*w+x)*3; return (d[o],d[o+1],d[o+2])
B=(255,255,255); N=(0,0,0); y=44
checks=[("x=0 scrollato via",px(0,y),N),("x=4 nero",px(4,y),N),
        ("x=6 bianco post-scroll",px(6,y),B),("x=36 bianco",px(36,y),B),("x=40 nero",px(40,y),N)]
fail=0
for n,g,wt in checks:
    ok=g==wt; print(f"{'OK ' if ok else 'FAIL'} {n}: {g}")
    if not ok: fail+=1
sys.exit(1 if fail else 0)
