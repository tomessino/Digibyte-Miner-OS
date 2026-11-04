#!/usr/bin/env python3
"""Erzeugt die Display-Assets fuer den Design-Sketch.

Eingabe:  tools/design/emblem.py (Roboter, Symbole), tools/design/assets/ (DigiByte-Logo)
          DejaVu-Schriften des Systems
Ausgabe:  firmware/tomessino_dgb/ui_assets*.{h,cpp}
          tools/design/out/bg_<seite>.png (Hintergruende ohne Live-Werte)

Aufruf:   python3 tools/design/gen_assets.py
"""
import math
import pathlib
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import emblem

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "tools/design/assets"
SKETCH = ROOT / "firmware/tomessino_dgb"
OUT = ROOT / "tools/design/out"
FONT_DIR = "/usr/share/fonts/truetype/dejavu/"

W, H = 320, 240
SS = 4  # Supersampling der Hintergruende

NAVY = (2, 10, 30)
LINE = (16, 96, 240)
LINE_DIM = (10, 60, 150)
BRIGHT = (30, 130, 255)
WHITE = (236, 243, 255)
GREEN = (40, 210, 70)


def rgb565(c):
    return ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3)


# ----------------------------------------------------------------
# Zeichenhilfen (Koordinaten immer in Display-Pixeln)
# ----------------------------------------------------------------
class Canvas:
    def __init__(self, top, bottom):
        grad = np.zeros((H * SS, W * SS, 3), dtype=np.float32)
        for i in range(3):
            grad[:, :, i] = np.linspace(top[i], bottom[i], H * SS)[:, None]
        self.img = Image.fromarray(grad.astype(np.uint8), "RGB")

    def _pts(self, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def poly(self, pts, fill=None, outline=None, width=1.0, glow=False):
        pts = self._pts(pts)
        if fill is not None:
            ImageDraw.Draw(self.img).polygon(pts, fill=fill)
        if outline is not None:
            self.path(None, outline, width, glow, closed_pts=pts)

    def path(self, pts, color, width=1.0, glow=False, closed_pts=None):
        p = closed_pts + closed_pts[:1] if closed_pts else self._pts(pts)
        if glow:
            layer = Image.new("RGB", self.img.size, (0, 0, 0))
            ImageDraw.Draw(layer).line(p, fill=color, width=int(width * SS * 2), joint="curve")
            layer = layer.filter(ImageFilter.GaussianBlur(SS * 1.2))
            a = np.asarray(self.img, dtype=np.int16) + np.asarray(layer, dtype=np.int16) // 2
            self.img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")
        ImageDraw.Draw(self.img).line(p, fill=color, width=max(1, int(width * SS)), joint="curve")

    def vgrad_poly(self, pts, top, bottom):
        mask = Image.new("L", self.img.size, 0)
        ImageDraw.Draw(mask).polygon(self._pts(pts), fill=255)
        ys = [p[1] for p in pts]
        y0, y1 = min(ys) * SS, max(ys) * SS
        g = np.zeros((H * SS, W * SS, 3), dtype=np.float32)
        t = np.clip((np.arange(H * SS) - y0) / max(1, y1 - y0), 0, 1)[:, None]
        for i in range(3):
            g[:, :, i] = top[i] + (bottom[i] - top[i]) * t
        self.img.paste(Image.fromarray(g.astype(np.uint8), "RGB"), (0, 0), mask)

    def text(self, xy, s, size, color, bold=False, anchor="ls", italic=False):
        name = "DejaVuSansCondensed" + ("-Bold" if bold else "") + ("-Oblique" if italic and not bold else "") + ("Oblique" if italic and bold else "") + ".ttf"
        f = ImageFont.truetype(FONT_DIR + name, size * SS)
        ImageDraw.Draw(self.img).text((xy[0] * SS, xy[1] * SS), s, font=f, fill=color, anchor=anchor)

    def paste_rgba(self, rgba, x, y):
        """Legt ein bereits in Display-Pixeln vorliegendes RGBA-Bild auf."""
        big = rgba.resize((rgba.width * SS, rgba.height * SS), Image.LANCZOS)
        self.img.paste(big.convert("RGB"), (int(x * SS), int(y * SS)), big.getchannel("A"))

    def world_map(self, color, box=(0, 0, W, H), pitch=5, radius=1.3, fade=None):
        mask = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(mask)
        x0, y0, x1, y1 = box
        for poly in CONTINENTS:
            pts = [(x0 + (lon + 180) / 360 * (x1 - x0), y0 + (82 - lat) / 140 * (y1 - y0)) for lon, lat in poly]
            d.polygon(pts, fill=255)
        m = np.asarray(mask)
        draw = ImageDraw.Draw(self.img)
        for gy in range(int(y0), int(y1), pitch):
            for gx in range(int(x0), int(x1), pitch):
                if 0 <= gy < H and 0 <= gx < W and m[gy, gx]:
                    k = fade(gx, gy) if fade else 1.0
                    c = tuple(int(v * k) for v in color)
                    r = radius * SS
                    draw.ellipse((gx * SS - r, gy * SS - r, gx * SS + r, gy * SS + r), fill=c)

    def finish(self):
        return self.img.resize((W, H), Image.LANCZOS)


def chamfer(x0, y0, x1, y1, c=5, corners="tlbr"):
    """Rechteck mit abgeschraegten Ecken: tl, tr, bl, br einzeln waehlbar als Liste."""
    want = corners if isinstance(corners, (list, tuple)) else ("tl", "tr", "bl", "br")
    p = []
    p += [(x0, y0 + c), (x0 + c, y0)] if "tl" in want else [(x0, y0)]
    p += [(x1 - c, y0), (x1, y0 + c)] if "tr" in want else [(x1, y0)]
    p += [(x1, y1 - c), (x1 - c, y1)] if "br" in want else [(x1, y1)]
    p += [(x0 + c, y1), (x0, y1 - c)] if "bl" in want else [(x0, y1)]
    return p


CONTINENTS = [
    [(-168, 65), (-156, 71), (-125, 70), (-95, 72), (-80, 70), (-62, 60), (-55, 50), (-66, 44), (-75, 35), (-81, 26),
     (-90, 29), (-97, 25), (-95, 18), (-85, 15), (-80, 8), (-85, 10), (-105, 20), (-115, 30), (-124, 40), (-125, 50),
     (-140, 60), (-155, 58)],
    [(-55, 82), (-25, 83), (-20, 72), (-42, 60), (-52, 65)],
    [(-80, 10), (-62, 11), (-50, 0), (-35, -7), (-40, -22), (-55, -35), (-65, -50), (-72, -53), (-73, -35), (-70, -18),
     (-80, -5)],
    [(-10, 36), (-9, 44), (-2, 49), (5, 52), (8, 57), (5, 62), (15, 69), (30, 71), (60, 70), (80, 74), (105, 78),
     (140, 73), (180, 68), (170, 60), (160, 58), (140, 54), (135, 42), (122, 38), (122, 30), (110, 20), (108, 12),
     (103, 2), (98, 10), (92, 22), (80, 8), (72, 20), (58, 25), (50, 30), (56, 18), (44, 12), (35, 30), (28, 37),
     (12, 38), (2, 38)],
    [(-16, 15), (-17, 22), (-8, 34), (10, 37), (32, 31), (43, 12), (51, 11), (40, -5), (40, -16), (33, -27), (20, -35),
     (13, -20), (9, 2), (-8, 5)],
    [(113, -22), (122, -17), (131, -12), (142, -11), (153, -27), (148, -38), (136, -35), (115, -34)],
    [(95, 5), (105, -6), (120, -8), (130, -4), (118, 5), (105, 3)],
    [(130, 32), (141, 36), (142, 44), (138, 38)],
    [(-6, 50), (1, 52), (-3, 58), (-7, 56)],
    [(44, -16), (50, -14), (47, -25), (44, -24)],
    [(167, -46), (178, -38), (174, -41)],
]


# ----------------------------------------------------------------
# Ausschnitte aus den Vorlagen
# ----------------------------------------------------------------
def crop_scaled(path, box, size):
    # Ausschnitte der Designvorlagen liegen einzeln in tools/design/assets
    im = Image.open(ASSETS / f"{path.split('.')[0]}_{box[0]}_{box[1]}_{box[2]}_{box[3]}.png").convert("RGB")
    return im.resize(size, Image.LANCZOS)


def circle_crop(path, box, d):
    im = crop_scaled(path, box, (d * 4, d * 4))
    mask = Image.new("L", (d * 4, d * 4), 0)
    ImageDraw.Draw(mask).ellipse((1, 1, d * 4 - 2, d * 4 - 2), fill=255)
    im.putalpha(mask)
    return im.resize((d, d), Image.LANCZOS)


# ----------------------------------------------------------------
# Hintergruende
# ----------------------------------------------------------------
def footer_lines(c, y=204):
    """Fusszeile wie in den Vorlagen 1 und 5: Mittelfeld mit schraegen Kanten."""
    c.path([(6, y + 32), (84, y + 32), (96, y + 2), (214, y + 2), (224, y + 32), (314, y + 32)], LINE_DIM, 0.8)


def bg_miner():
    c = Canvas((3, 12, 38), (3, 20, 62))
    fade = lambda x, y: 0.95
    c.world_map((6, 44, 120), box=(-8, 22, 328, 172), pitch=5, radius=1.45, fade=fade)
    # Kopfzeile
    c.vgrad_poly([(3, 3), (66, 3), (72, 9), (72, 21), (66, 27), (3, 27)], (10, 80, 215), (5, 42, 140))
    c.poly([(3, 3), (66, 3), (72, 9), (72, 21), (66, 27), (3, 27)], outline=BRIGHT, width=1.0, glow=True)
    c.path([(68, 3), (84, 28), (168, 28)], LINE, 0.9, glow=True)
    c.paste_rgba(emblem.icon("wifi", 15, 14), 177, 7)
    # Linke Seite: Logo, Figur, Schriftzug
    logo = circle_crop("2_miner.jpg", (80, 225, 354, 499), 56)
    c.paste_rgba(logo, 8, 42)
    c.paste_rgba(emblem.robot(72, 63), 66, 31)
    c.text((102, 106), "DIGIBYTE", 12, WHITE, bold=True, italic=True, anchor="ms")
    c.text((102, 119), "MINER", 11, BRIGHT, bold=True, italic=True, anchor="ms")
    # Die Symbole der vier Werte zeichnet die Firmware (MASKS), sie leuchten bei neuen Werten grün.
    # Block-Zähler als Plakette unten rechts am Logo
    c.poly([(56 + 10.5 * math.cos(a / 12 * math.tau), 88 + 10.5 * math.sin(a / 12 * math.tau)) for a in range(12)],
           fill=(5, 42, 140), outline=BRIGHT, width=1.0, glow=True)
    # Rechtes Feld
    c.poly([(170, 31), (308, 31), (316, 38), (316, 126), (312, 130), (232, 130), (226, 134), (178, 134), (170, 128)],
           outline=LINE, width=0.9, glow=True)
    # IP-Feld und Hashrate-Feld
    c.vgrad_poly([(14, 146), (148, 146), (134, 176), (4, 176)], (11, 70, 190), (6, 34, 110))
    c.poly([(3, 142), (152, 142), (150, 146), (135, 180), (3, 180)], outline=LINE, width=0.9, glow=True)
    c.path([(146, 144), (140, 152)], BRIGHT, 1.0)
    c.path([(150, 144), (144, 152)], BRIGHT, 1.0)
    c.poly([(158, 142), (222, 142), (228, 137), (310, 137), (316, 143), (316, 180), (141, 180)], outline=LINE, width=0.9, glow=True)
    # Fuss: drei Kacheln mit Symbol, Titel und Wert (Wert zeichnet die Firmware)
    for x0, x1, ic, label in MINER_TILES:
        pts = chamfer(x0, 187, x1, 236, 5, ("tl", "br"))
        c.vgrad_poly(pts, (6, 44, 122), (4, 22, 64))
        c.poly(pts, outline=LINE, width=0.9, glow=True)
        c.path([(x0 + 4, 202), (x1 - 4, 202)], LINE_DIM, 0.7)
        c.paste_rgba(emblem.icon(ic, 12, 12), x0 + 6, 189)
        c.text((x0 + 22, 199), label, 10, (150, 192, 255), bold=True)
    return c.finish()


MINER_TILES = ((4, 104, "trophy", "BEST DIFF"), (110, 210, "users", "WORKERS"), (216, 316, "bolt", "TOTAL HASH"))
# Symbole der vier Zählerzeilen, Position in ui.cpp
MASKS = (("JOBS", "download"), ("NET", "globe"), ("BEST", "chart"), ("SHARES", "share"))


def bg_clock():
    c = Canvas((2, 8, 26), (2, 8, 26))
    c.world_map((9, 44, 112), box=(-6, 26, 326, 212), pitch=5, radius=1.4)
    return c.finish()


def bg_markets():
    # Farben wie Wetter und Miner, Rahmen wie das Vorhersage-Panel
    c = Canvas((2, 10, 32), (3, 18, 54))
    c.world_map((6, 38, 104), box=(0, 12, 320, 200), pitch=5, radius=1.4)
    c.poly(chamfer(4, 5, 316, 199, 5), outline=LINE, width=0.9, glow=True)
    for i in range(5):
        y = 37 + i * 31
        c.path([(4, y), (316, y)], LINE_DIM, 0.7)
    footer_lines(c)
    return c.finish()


def bg_weather():
    c = Canvas((2, 10, 32), (2, 14, 40))
    c.world_map((5, 28, 78), box=(80, -10, 330, 100), pitch=5, radius=1.1)
    # Kleine Symbole aus Vorlage 1
    rise = emblem.icon("sunrise", 18, 16)
    c.paste_rgba(rise, 3, 64)
    c.paste_rgba(rise, 3, 84)
    c.paste_rgba(emblem.icon("drop", 14, 15), 72, 65)
    c.paste_rgba(emblem.icon("gauge", 15, 15), 72, 85)
    c.path([(241, 86), (241, 100)], LINE_DIM, 0.8)
    # Vorhersage-Panel
    c.poly(chamfer(4, 106, 316, 200, 5), outline=LINE, width=0.9, glow=True)
    for y in (130, 154, 178):
        c.path([(4, y), (316, y)], LINE_DIM, 0.8)
    c.paste_rgba(emblem.icon("pin", 14, 15), 12, 181)
    footer_lines(c)
    return c.finish()


def bg_rank():
    c = Canvas((2, 10, 32), (3, 18, 54))
    c.world_map((6, 38, 104), box=(0, 18, 320, 90), pitch=5, radius=1.1)
    # Titel
    c.paste_rgba(emblem.icon("ladder", 17, 21), 49, 3)
    c.text((72, 21), "LAN RANK LADDER", 18, WHITE, bold=True)
    # Podest
    c.poly(chamfer(4, 28, 106, 80, 6, ("tl",)), outline=LINE, width=0.9, glow=True)
    c.vgrad_poly([(108, 28), (212, 28), (212, 80), (108, 80)], (6, 62, 160), (5, 42, 118))
    c.poly([(108, 28), (212, 28), (212, 80), (108, 80)], outline=BRIGHT, width=0.9, glow=True)
    c.poly(chamfer(214, 28, 316, 80, 6, ("tr",)), outline=LINE, width=0.9, glow=True)
    c.text((55, 44), "#2", 15, WHITE, bold=True, anchor="ms")
    c.text((160, 44), "#1", 15, WHITE, bold=True, anchor="ms")
    c.text((265, 44), "#3", 15, WHITE, bold=True, anchor="ms")
    # Liste
    c.poly(chamfer(4, 84, 316, 204, 5), outline=LINE, width=0.9, glow=True)
    for i in range(6):
        y = 84 + 16 * (i + 1)
        c.path([(4, y), (316, y)], LINE_DIM, 0.7)
        c.text((12, y - 4), f"#{i + 4}", 12, WHITE, bold=True)
    c.vgrad_poly(chamfer(5, 181, 315, 203, 4, ("bl", "br")), (6, 62, 160), (5, 42, 118))
    c.path([(5, 180), (315, 180)], BRIGHT, 0.9)
    c.path([(6, 207), (314, 207)], LINE_DIM, 0.7)
    return c.finish()


def dither565(im):
    """RGB888 -> RGB565 mit geordnetem Dithering gegen Farbbaender in dunklem Blau."""
    a = np.asarray(im.convert("RGB"), dtype=np.float32)
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.5
    t = np.tile(bayer, (H // 4 + 1, W // 4 + 1))[:H, :W]
    r = np.clip(a[:, :, 0] + t * 8, 0, 255).astype(np.uint16) >> 3
    g = np.clip(a[:, :, 1] + t * 4, 0, 255).astype(np.uint16) >> 2
    b = np.clip(a[:, :, 2] + t * 8, 0, 255).astype(np.uint16) >> 3
    return (r << 11) | (g << 5) | b


# ----------------------------------------------------------------
# Schriften
# ----------------------------------------------------------------
ASCII = list(range(32, 127)) + [0xB0]
LATIN1 = list(range(32, 127)) + list(range(0xA0, 0x100))
DIGITS = [ord(c) for c in " -0123456789?"]


def sans_font(name, cname, px, cps=ASCII):
    f = ImageFont.truetype(FONT_DIR + name, px)
    glyphs, data = {}, bytearray()
    for cp in cps:
        ch = chr(cp)
        x0, y0, x1, y1 = f.getbbox(ch, anchor="ls")
        adv = int(round(f.getlength(ch)))
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        off = len(data)
        if w and h:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-x0, -y0), ch, font=f, fill=255, anchor="ls")
            data += im.tobytes()
        glyphs[cp] = (off, w, h, x0, y0, adv)
    cap = -f.getbbox("H", anchor="ls")[1]
    return dict(cname=cname, glyphs=glyphs, data=data, cap=cap)


def seg_polys(w, h, t):
    """Segmentflaechen im Stil der Vorlagen (trapezfoermige Enden)."""
    g = max(0.45, t * 0.16)
    m = h / 2
    s = {}
    s["a"] = [(g, 0), (w - g, 0), (w - g - t, t), (g + t, t)]
    s["d"] = [(g + t, h - t), (w - g - t, h - t), (w - g, h), (g, h)]
    s["f"] = [(0, g), (t, g + t), (t, m - g - t / 2), (0, m - g)]
    s["b"] = [(w, g), (w, m - g), (w - t, m - g - t / 2), (w - t, g + t)]
    s["e"] = [(0, m + g), (t, m + g + t / 2), (t, h - g - t), (0, h - g)]
    s["c"] = [(w, m + g), (w, h - g), (w - t, h - g - t), (w - t, m + g + t / 2)]
    s["g"] = [(g + t / 2, m), (g + t, m - t / 2), (w - g - t, m - t / 2), (w - g - t / 2, m), (w - g - t, m + t / 2), (g + t, m + t / 2)]
    cx = w / 2
    s["g1"] = [(g + t / 2, m), (g + t, m - t / 2), (cx - g, m - t / 2), (cx - g, m + t / 2), (g + t, m + t / 2)]
    s["g2"] = [(cx + g, m - t / 2), (w - g - t, m - t / 2), (w - g - t / 2, m), (w - g - t, m + t / 2), (cx + g, m + t / 2)]
    ht = t * 0.5
    s["i"] = [(cx - ht, t + g), (cx + ht, t + g), (cx + ht, m - t / 2 - g), (cx - ht, m - t / 2 - g)]
    s["l"] = [(cx - ht, m + t / 2 + g), (cx + ht, m + t / 2 + g), (cx + ht, h - t - g), (cx - ht, h - t - g)]
    s["h"] = ("diag", (t + g, t + g), (cx - ht - g, m - t / 2 - g))
    s["j"] = ("diag", (w - t - g, t + g), (cx + ht + g, m - t / 2 - g))
    s["k"] = ("diag", (cx - ht - g, m + t / 2 + g), (t + g, h - t - g))
    s["m"] = ("diag", (cx + ht + g, m + t / 2 + g), (w - t - g, h - t - g))
    return s


SEG_CHARS = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc",
    "8": "abcdefg", "9": "abcdfg", "-": "g",
    "A": "a b c e f g1 g2", "D": "a b c d i l", "E": "a d e f g1", "F": "a e f g1", "H": "b c e f g1 g2",
    "I": "a d i l", "M": "b c e f h j", "N": "b c e f h m", "O": "a b c d e f", "R": "a b e f g1 g2 m",
    "S": "a c d f g1 g2", "T": "a i l", "U": "b c d e f", "W": "b c e f k m", "d": "b c d e g",
    "$": "a c d f g1 g2 i l", "/": "j k",
}


def seg_font(cname, h):
    # Klein: dünne Segmente, sonst laufen die Ziffern zu. Groß: kräftig wie in Vorlage 3.
    t = 1.5 if h <= 14 else h * 0.115 if h <= 30 else h * 0.14 if h <= 44 else h * 0.16
    w = 7 if h <= 12 else round(h * 0.60)
    gap = max(1, round(h * 0.11))
    k = 8
    glyphs, data = {}, bytearray()

    def add(cp, im, gw):
        off = len(data)
        small = im.resize((gw, h), Image.LANCZOS)
        data.extend(small.tobytes())
        glyphs[cp] = (off, gw, h, 0, -h, gw + gap)

    polys = seg_polys(w * k, h * k, t * k)
    for ch, segs in SEG_CHARS.items():
        im = Image.new("L", (w * k, h * k), 0)
        d = ImageDraw.Draw(im)
        for s in (segs.split() if " " in segs else list(segs)):
            p = polys[s]
            if p[0] == "diag":
                d.line([p[1], p[2]], fill=255, width=int(t * k * 0.8))
            else:
                d.polygon(p, fill=255)
        add(ord(ch), im, w)
    # Die 1 ist schmal wie in den Vorlagen
    w1 = max(3, int(math.ceil(t * 1.4)))
    im = Image.new("L", (w1 * k, h * k), 0)
    d = ImageDraw.Draw(im)
    p1 = seg_polys(w1 * k, h * k, t * k)
    d.polygon(p1["b"], fill=255)
    d.polygon(p1["c"], fill=255)
    add(ord("1"), im, w1)
    # Doppelpunkt und Punkt sind schmaler
    cw = max(3, int(round(t * 1.5)))
    for ch, ys in ((":", (0.32, 0.68)), (".", (0.92,))):
        im = Image.new("L", (cw * k, h * k), 0)
        d = ImageDraw.Draw(im)
        r = t * k * 0.62
        cx = cw * k / 2
        for fy in ys:
            d.rectangle((cx - r, fy * h * k - r, cx + r, fy * h * k + r), fill=255)
        add(ord(ch), im, cw)
    glyphs[32] = (0, 0, 0, 0, 0, w // 2 + gap)
    return dict(cname=cname, glyphs=glyphs, data=data, cap=h)


# ----------------------------------------------------------------
# Wetter-Symbole
# ----------------------------------------------------------------
ICON_NAMES = ["CLEAR", "PARTLY", "CLOUD", "FOG", "RAIN", "SNOW", "THUNDER"]
SUN = (28, 120, 255, 255)
CLOUD = (222, 232, 250, 255)
DROP = (40, 130, 255, 255)


def draw_cloud(d, x, y, s, fill=CLOUD):
    """Wolke, x/y = linke obere Ecke, s = Breite."""
    h = s * 0.62
    d.ellipse((x, y + h * 0.42, x + s * 0.42, y + h), fill=fill)
    d.ellipse((x + s * 0.60, y + h * 0.36, x + s, y + h), fill=fill)
    d.ellipse((x + s * 0.20, y, x + s * 0.72, y + h * 0.84), fill=fill)
    d.rectangle((x + s * 0.2, y + h * 0.62, x + s * 0.8, y + h), fill=fill)


def draw_sun(d, cx, cy, r, fill=SUN):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
    for i in range(8):
        a = i * math.pi / 4
        d.line([(cx + math.cos(a) * r * 1.3, cy + math.sin(a) * r * 1.3),
                (cx + math.cos(a) * r * 1.75, cy + math.sin(a) * r * 1.75)], fill=fill, width=max(1, int(r * 0.26)))


def weather_icon(kind, w, h):
    k = 6
    W_, H_ = w * k, h * k
    im = Image.new("RGBA", (W_, H_), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    lw = max(k, int(W_ * 0.04))
    if kind == "CLEAR":
        draw_sun(d, W_ / 2, H_ / 2, H_ * 0.27)
    elif kind == "PARTLY":
        draw_sun(d, W_ * 0.36, H_ * 0.36, H_ * 0.2)
        # dunkler Rand trennt Wolke und Sonne
        draw_cloud(d, W_ * 0.28, H_ * 0.32, W_ * 0.72, fill=(0, 0, 0, 0))
        draw_cloud(d, W_ * 0.30, H_ * 0.36, W_ * 0.68)
    elif kind == "CLOUD":
        draw_cloud(d, W_ * 0.08, H_ * 0.14, W_ * 0.84)
    else:
        draw_cloud(d, W_ * 0.14, 0, W_ * 0.72)
        base = H_ * 0.72
        xs = [W_ * f for f in (0.28, 0.44, 0.60, 0.76)]
        if kind == "RAIN":
            for x in xs:
                d.line([(x, base), (x - W_ * 0.05, H_ * 0.96)], fill=DROP, width=lw)
        elif kind == "SNOW":
            r = H_ * 0.055
            for i, x in enumerate(xs):
                y = base + (H_ * 0.12 if i % 2 else 0) + r
                d.ellipse((x - r, y - r, x + r, y + r), fill=DROP)
        elif kind == "FOG":
            for i in range(3):
                y = H_ * (0.70 + i * 0.12)
                d.line([(W_ * 0.14, y), (W_ * 0.86, y)], fill=DROP, width=lw)
        elif kind == "THUNDER":
            d.polygon([(W_ * 0.54, base - H_ * .04), (W_ * 0.38, H_ * 0.84), (W_ * 0.50, H_ * 0.84), (W_ * 0.44, H_), (W_ * 0.66, H_ * 0.76), (W_ * 0.54, H_ * 0.76), (W_ * 0.62, base - H_ * .04)], fill=DROP)
    return im.resize((w, h), Image.LANCZOS)


# ----------------------------------------------------------------
# Ausgabe
# ----------------------------------------------------------------
def carr(values, per=20, fmt="0x{:02X}"):
    vals = list(values)
    lines = []
    for i in range(0, len(vals), per):
        lines.append(",".join(fmt.format(v) for v in vals[i:i + per]))
    return ",\n".join(lines)


def main():
    OUT.mkdir(exist_ok=True)
    pages = dict(miner=bg_miner(), clock=bg_clock(), markets=bg_markets(), weather=bg_weather(), rank=bg_rank())

    hdr = [
        "// ERZEUGT von tools/design/gen_assets.py, nicht von Hand aendern.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct UiGlyph { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; };",
        "struct UiFont  { const UiGlyph* glyphs; const uint8_t* data; const uint8_t* map; uint8_t cap; };",
        "struct UiIcon  { uint8_t w, h; const uint16_t* rgb; const uint8_t* a; };",
        "",
    ]
    bg = ['#include "ui_assets.h"', ""]
    for name, im in pages.items():
        im.save(OUT / f"bg_{name}.png")
        px = dither565(im).flatten()
        hdr.append(f"extern const uint16_t ui_bg_{name}[{W * H}];")
        bg.append(f"const uint16_t ui_bg_{name}[{W * H}] = {{\n{carr(px, 24, '0x{:04X}')}\n}};\n")
    (SKETCH / "ui_assets_bg.cpp").write_text("\n".join(bg))

    fonts = [
        sans_font("DejaVuSansCondensed.ttf", "F_S11", 11, LATIN1),
        sans_font("DejaVuSansCondensed.ttf", "F_S13", 13, LATIN1),
        sans_font("DejaVuSansCondensed.ttf", "F_S15", 15),
        sans_font("DejaVuSansCondensed.ttf", "F_S18", 18),
        sans_font("DejaVuSansCondensed.ttf", "F_S38", 38, DIGITS),
        sans_font("DejaVuSansCondensed-Bold.ttf", "F_B15", 15),
        seg_font("F_SEG12", 12),
        seg_font("F_SEG14", 14),
        seg_font("F_SEG18", 18),
        seg_font("F_SEG26", 26),
        seg_font("F_SEG30", 30),
        seg_font("F_SEG44", 44),
        seg_font("F_SEG90", 90),
    ]
    src = ['#include "ui_assets.h"', ""]
    hdr.append("")
    total = 0
    for f in fonts:
        n = f["cname"]
        cps = sorted(f["glyphs"])
        cmap = [0xFF] * 256
        for i, cp in enumerate(cps):
            cmap[cp] = i
        total += len(f["data"])
        src.append(f"static const uint8_t {n}_data[] = {{\n{carr(f['data'])}\n}};")
        src.append(f"static const uint8_t {n}_map[256] = {{\n{carr(cmap)}\n}};")
        rows = ",".join("{%d,%d,%d,%d,%d,%d}" % f["glyphs"][cp] for cp in cps)
        src.append(f"static const UiGlyph {n}_glyphs[] = {{{rows}}};")
        src.append(f"const UiFont {n} = {{{n}_glyphs,{n}_data,{n}_map,{f['cap']}}};\n")
        hdr.append(f"extern const UiFont {n};")

    hdr.append("")
    hdr.append("enum UiWeatherIcon { " + ", ".join(f"ICON_{k}" for k in ICON_NAMES) + " };")
    for label, (w, h) in (("BIG", (66, 54)), ("SMALL", (28, 21))):
        rows = []
        for kind in ICON_NAMES:
            im = weather_icon(kind, w, h)
            a = np.asarray(im, dtype=np.uint8)
            rgb = [rgb565(tuple(int(v) for v in p[:3])) for p in a.reshape(-1, 4)]
            alpha = a[:, :, 3].flatten()
            n = f"icon_{label}_{kind}"
            src.append(f"static const uint16_t {n}_rgb[] = {{\n{carr(rgb, 24, '0x{:04X}')}\n}};")
            src.append(f"static const uint8_t {n}_a[] = {{\n{carr(alpha)}\n}};")
            rows.append(f"{{{w},{h},{n}_rgb,{n}_a}}")
            total += len(rgb) * 3
        src.append(f"const UiIcon ui_icons_{label.lower()}[{len(ICON_NAMES)}] = {{{','.join(rows)}}};\n")
        hdr.append(f"extern const UiIcon ui_icons_{label.lower()}[{len(ICON_NAMES)}];")
    hdr.append("")
    hdr.append("struct UiMask  { uint8_t w, h; const uint8_t* a; };")
    hdr.append("enum UiMaskId { " + ", ".join(f"MASK_{k}" for k, _ in MASKS) + " };")
    rows = []
    for key, name in MASKS:
        a = np.asarray(emblem.mask(name, 20, 20), dtype=np.uint8).flatten()
        src.append(f"static const uint8_t mask_{key}[] = {{\n{carr(a)}\n}};")
        rows.append(f"{{20,20,mask_{key}}}")
        total += len(a)
    src.append(f"const UiMask ui_masks[{len(MASKS)}] = {{{','.join(rows)}}};\n")
    hdr.append(f"extern const UiMask ui_masks[{len(MASKS)}];")
    (SKETCH / "ui_assets_fonts.cpp").write_text("\n".join(src))
    (SKETCH / "ui_assets.h").write_text("\n".join(hdr) + "\n")
    print(f"Hintergruende: {len(pages)} x {W * H * 2} Byte, Schriften+Symbole: {total} Byte")


if __name__ == "__main__":
    main()
