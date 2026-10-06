#!/usr/bin/env python3
"""Converte PPM (P6) in BMP 24bit senza dipendenze — apribile da Windows."""
import struct, sys

src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".bmp"
with open(src, "rb") as f:
    assert f.readline().strip() == b"P6"
    w, h = map(int, f.readline().split()); f.readline()
    rgb = f.read()

pad = (4 - (w * 3) % 4) % 4
rowsz = w * 3 + pad
img = bytearray()
for y in range(h - 1, -1, -1):            # BMP: righe dal basso
    row = rgb[y * w * 3:(y + 1) * w * 3]
    img += bytes(row[i:i + 3][::-1] for i in range(0, len(row), 3)).replace(b"", b"") if False else b"".join(row[i + 2:i + 3] + row[i + 1:i + 2] + row[i:i + 1] for i in range(0, len(row), 3))
    img += b"\x00" * pad

hdr = struct.pack("<2sIHHI", b"BM", 54 + len(img), 0, 0, 54)
dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(img), 2835, 2835, 0, 0)
with open(dst, "wb") as f:
    f.write(hdr + dib + img)
print(f"scritto {dst} ({w}x{h})")
