#!/usr/bin/env python3
"""Eigene Grafiken des Projekts, komplett per Code gezeichnet (nur Pillow).

Der Roboter mit Chip-Körper erscheint auf der Miner-Seite und im Dashboard,
die Symbole auf den Display-Seiten. Alles liefert RGBA-Bilder mit transparentem Grund.
"""
import math

from PIL import Image, ImageDraw

WHITE = (236, 243, 255, 255)
BLUE = (30, 130, 255, 255)
BLUE_DARK = (14, 72, 170, 255)
INNER = (4, 18, 46, 255)
K = 8  # Supersampling


class Pen:
    """Zeichnet in einem Raster aus bw x bh Einheiten, das auf w x h Pixel abgebildet wird."""

    def __init__(self, w, h, bw, bh):
        self.w, self.h = w, h
        self.sx, self.sy = w * K / bw, h * K / bh
        self.s = min(self.sx, self.sy)
        self.im = Image.new("RGBA", (w * K, h * K), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def _p(self, x, y):
        return (x * self.sx, y * self.sy)

    def line(self, pts, width, color=WHITE):
        p = [self._p(*q) for q in pts]
        wd = max(1, int(width * self.s))
        self.d.line(p, fill=color, width=wd, joint="curve")
        r = wd / 2
        for x, y in (p[0], p[-1]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=color)

    def rrect(self, x0, y0, x1, y1, r, fill=None, outline=None, width=2):
        self.d.rounded_rectangle((*self._p(x0, y0), *self._p(x1, y1)), radius=r * self.s, fill=fill,
                                 outline=outline, width=max(1, int(width * self.s)))

    def circle(self, x, y, r, fill=None, outline=None, width=2):
        cx, cy = self._p(x, y)
        rr = r * self.s
        self.d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=fill, outline=outline, width=max(1, int(width * self.s)))

    def poly(self, pts, fill):
        self.d.polygon([self._p(*q) for q in pts], fill=fill)

    def arc(self, x, y, r, a0, a1, width, color=WHITE):
        cx, cy = self._p(x, y)
        rr = r * self.s
        self.d.arc((cx - rr, cy - rr, cx + rr, cy + rr), a0, a1, fill=color, width=max(1, int(width * self.s)))

    def done(self):
        return self.im.resize((self.w, self.h), Image.LANCZOS)


def robot(w, h):
    """Roboter mit Mikrochip als Körper, hält eine Spitzhacke aufrecht neben sich."""
    p = Pen(w, h, 100, 88)
    # Spitzhacke hinter der Hand
    p.line([(80, 84), (80, 12)], 3.4)
    p.arc(80, 32, 23, 218, 322, 4.2)
    p.rrect(76.5, 7, 83.5, 15, 1.5, fill=BLUE)
    # Antenne
    p.line([(42, 15), (42, 6)], 2.6)
    p.circle(42, 4.5, 3, fill=BLUE)
    # Kopf mit Visier
    p.rrect(23, 14, 61, 38, 6, fill=INNER, outline=WHITE, width=2.8)
    p.rrect(28, 20, 56, 32, 4, fill=BLUE_DARK, outline=BLUE, width=1.6)
    p.circle(35.5, 26, 2.8, fill=WHITE)
    p.circle(48.5, 26, 2.8, fill=WHITE)
    p.line([(19, 23), (19, 30)], 3)
    p.line([(65, 23), (65, 30)], 3)
    # Hals
    p.rrect(37, 38, 47, 43, 1, fill=WHITE)
    # Arme, rechts zur Hacke
    p.line([(21, 50), (9, 60), (9, 68)], 3.4)
    p.circle(9, 70.5, 3.6, fill=INNER, outline=WHITE, width=2.2)
    p.line([(63, 50), (72, 56), (80, 50)], 3.4)
    p.circle(80, 50, 4, fill=INNER, outline=WHITE, width=2.4)
    # Pins des Chips
    for y in (50, 57, 64, 71):
        p.line([(15, y), (21, y)], 2.2)
        p.line([(64, y), (68, y)], 2.2)
    # Körper: Chip
    p.rrect(20, 43, 64, 78, 3.5, fill=INNER, outline=WHITE, width=2.8)
    p.rrect(31, 50, 53, 71, 2.5, fill=BLUE_DARK, outline=BLUE, width=1.8)
    p.circle(26, 49, 1.8, fill=BLUE)
    # Leiterbahnen auf dem Kern
    for pts in ([(36, 56), (42, 56), (42, 61)], [(48, 66), (42, 66), (42, 61)], [(42, 61), (48, 61)]):
        p.line(pts, 1.8)
    p.circle(36, 56, 1.8, fill=WHITE)
    p.circle(48, 66, 1.8, fill=WHITE)
    p.circle(48, 61, 1.8, fill=WHITE)
    # Füße
    p.rrect(25, 79, 38, 85, 2, fill=WHITE)
    p.rrect(46, 79, 59, 85, 2, fill=WHITE)
    return p.done()


def _icon_download(p):
    p.line([(50, 12), (50, 58)], 11)
    p.poly([(26, 42), (74, 42), (50, 72)], WHITE)
    p.line([(12, 66), (12, 86), (88, 86), (88, 66)], 10)


def _icon_globe(p):
    p.circle(50, 50, 40, fill=BLUE_DARK, outline=WHITE, width=7)
    p.line([(11, 50), (89, 50)], 5)
    p.arc(50, 50, 26, 90, 270, 5)
    p.arc(50, 50, 26, 270, 450, 5)
    p.line([(50, 11), (50, 89)], 5)


def _icon_chart(p):
    for x, y in ((18, 58), (40, 38), (62, 50), (84, 16)):
        p.line([(x, 80), (x, y)], 11)
    p.line([(6, 92), (94, 92)], 7)


def _icon_share(p):
    # Share ging an den Pool: Pfeil nach oben aus dem Chip
    p.line([(50, 58), (50, 18)], 10)
    p.poly([(28, 34), (72, 34), (50, 6)], WHITE)
    p.rrect(20, 62, 80, 92, 6, fill=WHITE)


def _icon_wifi(p):
    for r, wd in ((46, 10), (30, 10), (14, 10)):
        p.arc(50, 82, r, 225, 315, wd, BLUE)
    p.circle(50, 82, 7, fill=BLUE)


def _icon_sun_low(p):
    p.d.pieslice((*p._p(26, 34), *p._p(74, 82)), 180, 360, fill=BLUE)
    for a in (180, 225, 270, 315, 360):
        r = math.radians(a)
        p.line([(50 + math.cos(r) * 32, 58 + math.sin(r) * 32), (50 + math.cos(r) * 44, 58 + math.sin(r) * 44)], 7, BLUE)
    p.line([(8, 68), (92, 68)], 7, BLUE)


def _icon_drop(p):
    p.poly([(50, 6), (22, 56), (78, 56)], WHITE)
    p.circle(50, 62, 30, fill=WHITE)


def _icon_gauge(p):
    p.circle(50, 50, 40, outline=WHITE, width=9)
    p.line([(50, 50), (70, 28)], 8)
    p.circle(50, 50, 7, fill=WHITE)


def _icon_pin(p):
    p.poly([(24, 50), (76, 50), (50, 94)], BLUE)
    p.circle(50, 36, 30, fill=BLUE)
    p.circle(50, 36, 12, fill=INNER)


def _icon_ladder(p):
    p.line([(34, 94), (50, 6)], 8, BLUE)
    p.line([(62, 94), (78, 6)], 8, BLUE)
    for t in (0.15, 0.38, 0.61, 0.84):
        p.line([(50 - 16 * t, 6 + 88 * t), (78 - 16 * t, 6 + 88 * t)], 7, BLUE)


_ICONS = {"download": _icon_download, "globe": _icon_globe, "chart": _icon_chart, "share": _icon_share, "wifi": _icon_wifi,
          "sunrise": _icon_sun_low, "drop": _icon_drop, "gauge": _icon_gauge, "pin": _icon_pin, "ladder": _icon_ladder}


def icon(name, w, h):
    p = Pen(w, h, 100, 100)
    _ICONS[name](p)
    return p.done()
