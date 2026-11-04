#!/usr/bin/env python3
"""Baut aus den PPM-Ausgaben des PC-Tests PNGs und eine Uebersicht."""
import pathlib
from PIL import Image

OUT = pathlib.Path(__file__).resolve().parents[2] / "tools/design/out"
PAGES = ["miner", "clock", "markets", "weather", "rank"]
SCALE = 2
CW, CH = 320 * SCALE, 240 * SCALE
GAP = 8

for name in PAGES:
    for v in ("voll", "leer", "breit"):
        Image.open(OUT / f"{name}_{v}.ppm").save(OUT / f"{name}_{v}.png")

for v in ("voll", "leer", "breit"):
    Image.open(OUT / f"chart_{v}.ppm").save(OUT / f"chart_{v}.png")

sheet = Image.new("RGB", (CW * 2 + GAP, (CH + GAP) * len(PAGES) - GAP), (40, 40, 40))
for i, name in enumerate(PAGES):
    for j, v in enumerate(("voll", "leer")):
        im = Image.open(OUT / f"{name}_{v}.png").resize((CW, CH), Image.NEAREST)
        sheet.paste(im, ((CW + GAP) * j, i * (CH + GAP)))
sheet.save(OUT / "uebersicht.png")
