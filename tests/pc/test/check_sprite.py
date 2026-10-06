#!/usr/bin/env python3
"""Verifica sprite: rosso a fb (100-131, 100-103), nero attorno."""
import sys
d = open(sys.argv[1],'rb').read()
idx = d.index(b'255\n')+4
hdr = d[:idx].split(); w,h = int(hdr[1]), int(hdr[2])
def px(x,y):
    o = idx + (y*w+x)*3; return (d[o],d[o+1],d[o+2])
R=(255,0,0); N=(0,0,0)
checks=[("sprite sx",px(100,100),R),("sprite dx",px(131,103),R),
        ("sprite centro",px(115,101),R),("fuori sx",px(98,100),N),
        ("fuori dx",px(133,100),N),("sopra",px(115,99),N),("sotto",px(115,104),N)]
fail=0
for n,g,wt in checks:
    ok = g==wt
    print(f"{'OK ' if ok else 'FAIL'} {n}: {g} atteso {wt}")
    if not ok: fail+=1
sys.exit(1 if fail else 0)
