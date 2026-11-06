#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ================================================================
// FARBEN (RGB565) — aus den Design-Vorlagen
// ================================================================
#define U_WHITE  0xEF9F
#define U_BLUE   0x1C3F
#define U_BLUE2  0x3D1F
#define U_LIGHT  0x9DDF
#define U_GREEN  0x2EA8
#define U_RED    0xF9A6
#define U_MUTED  0x8C7F
#define U_DIM    0x4A9A

// ================================================================
// RENDERER
// Zeichnet bandweise: Hintergrund aus dem Flash in den Puffer,
// Text oder Symbol darüber mischen, Band ausgeben.
// ================================================================
#define BAND_H 16
static uint16_t band[320 * BAND_H];
static UiPushFn pushFn = nullptr;
static const uint16_t* curBg = nullptr;

static const uint16_t* const BGS[5] = { ui_bg_miner, ui_bg_clock, ui_bg_markets, ui_bg_weather, ui_bg_rank };

// Zeichenwechsel im Feldtext: \x01 = Zweitfarbe, \x02 = zurück zur Hauptfarbe
#define CC_ALT "\x01"
#define CC_MAIN "\x02"

#define MAX_SLOTS 64
static uint32_t slotHash[MAX_SLOTS];
static int      slotIdx = 0;
static int      lastPage = -1;
int uiClipCount = 0;

void uiSetPush(UiPushFn fn) { pushFn = fn; }
void uiInvalidate() { lastPage = -1; }

static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a8) {
  uint32_t a = (a8 + 4) >> 3;   // 0..32
  uint32_t rb = (((fg & 0xF81F) * a + (bg & 0xF81F) * (32 - a)) >> 5) & 0xF81F;
  uint32_t g  = (((fg & 0x07E0) * a + (bg & 0x07E0) * (32 - a)) >> 5) & 0x07E0;
  return (uint16_t)(rb | g);
}

// Dekodiert ein Zeichen (UTF-8 bis U+00FF). Unbekanntes wird '?'.
static uint8_t nextCp(const char*& s) {
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *s) {
    uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
    return cp < 256 ? (uint8_t)cp : '?';
  }
  while (((uint8_t)*s & 0xC0) == 0x80) s++;
  return '?';
}

static const UiGlyph* glyphOf(const UiFont& f, uint8_t cp) {
  uint8_t i = f.map[cp];
  if (i == 0xFF) i = f.map[(uint8_t)'?'];
  if (i == 0xFF) i = f.map[(uint8_t)' '];
  return i == 0xFF ? nullptr : &f.glyphs[i];
}

static int textWidth(const UiFont& f, const char* s) {
  int w = 0;
  while (*s) {
    uint8_t cp = nextCp(s);
    if (cp == 1 || cp == 2) continue;
    const UiGlyph* g = glyphOf(f, cp);
    if (g) w += g->adv;
  }
  return w;
}

static uint32_t fnv(const char* s, uint32_t h) {
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

// Prüft den Merker des Felds. true: Inhalt unverändert, nichts zu tun.
static bool slotSame(uint32_t h) {
  if (slotIdx >= MAX_SLOTS) return false;
  if (h == 0) h = 1;
  bool same = (slotHash[slotIdx] == h);
  slotHash[slotIdx] = h;
  return same;
}

enum Align { L, C, R };

// Text in Box (x,y,w,h), vertikal zentriert auf die Großbuchstabenhöhe.
static void fld(int x, int y, int w, int h, const UiFont& f, uint16_t col, Align a,
                const char* text, uint16_t col2 = U_WHITE) {
  uint32_t hash = fnv(text, 2166136261u) ^ (((uint32_t)col << 16) | col2);
  bool same = slotSame(hash);
  slotIdx++;
  if (same || !pushFn) return;

  int tw = textWidth(f, text);
  if (tw > w) {
    uiClipCount++;
#ifdef UI_HOST
    printf("  ZU BREIT: \"%s\" %d > %d px bei x=%d y=%d\n", text, tw, w, x, y);
#endif
  }
  int penStart = (a == L) ? x : (a == C) ? x + (w - tw) / 2 : x + w - tw;
  int base = y + (h + f.cap) / 2;

  for (int by = y; by < y + h; by += BAND_H) {
    int bh = (y + h - by < BAND_H) ? y + h - by : BAND_H;
    for (int r = 0; r < bh; r++)
      memcpy(&band[r * w], &curBg[(by + r) * 320 + x], (size_t)w * 2);

    int pen = penStart;
    uint16_t cur = col;
    const char* s = text;
    while (*s) {
      uint8_t cp = nextCp(s);
      if (cp == 1) { cur = col2; continue; }
      if (cp == 2) { cur = col;  continue; }
      const UiGlyph* g = glyphOf(f, cp);
      if (!g) continue;
      int gx = pen + g->x, gy = base + g->y;
      for (int r = 0; r < g->h; r++) {
        int py = gy + r;
        if (py < by || py >= by + bh) continue;
        const uint8_t* src = f.data + g->off + (uint32_t)r * g->w;
        uint16_t* dst = &band[(py - by) * w];
        for (int c = 0; c < g->w; c++) {
          int px = gx + c - x;
          if (px < 0 || px >= w) continue;
          uint8_t al = src[c];
          if (al) dst[px] = blend565(cur, dst[px], al);
        }
      }
      pen += g->adv;
    }
    pushFn(x, by, w, bh, band);
  }
}

static void iconFld(int x, int y, const UiIcon& ic) {
  uint32_t hash = (uint32_t)(uintptr_t)ic.rgb;
  bool same = slotSame(hash);
  slotIdx++;
  if (same || !pushFn) return;
  for (int by = 0; by < ic.h; by += BAND_H) {
    int bh = (ic.h - by < BAND_H) ? ic.h - by : BAND_H;
    for (int r = 0; r < bh; r++) {
      const uint16_t* bgRow = &curBg[(y + by + r) * 320 + x];
      const uint16_t* rgb = &ic.rgb[(by + r) * ic.w];
      const uint8_t*  al  = &ic.a[(by + r) * ic.w];
      uint16_t* dst = &band[r * ic.w];
      for (int c = 0; c < ic.w; c++) dst[c] = al[c] ? blend565(rgb[c], bgRow[c], al[c]) : bgRow[c];
    }
    pushFn(x, y + by, ic.w, bh, band);
  }
}

// Einfarbiges Symbol aus einer Deckkraft-Maske, Farbe zur Laufzeit
static void maskFld(int x, int y, const UiMask& m, uint16_t col) {
  uint32_t hash = (uint32_t)(uintptr_t)m.a ^ ((uint32_t)col << 8);
  bool same = slotSame(hash);
  slotIdx++;
  if (same || !pushFn) return;
  for (int by = 0; by < m.h; by += BAND_H) {
    int bh = (m.h - by < BAND_H) ? m.h - by : BAND_H;
    for (int r = 0; r < bh; r++) {
      const uint16_t* bgRow = &curBg[(y + by + r) * 320 + x];
      const uint8_t*  al  = &m.a[(by + r) * m.w];
      uint16_t* dst = &band[r * m.w];
      for (int c = 0; c < m.w; c++) dst[c] = al[c] ? blend565(col, bgRow[c], al[c]) : bgRow[c];
    }
    pushFn(x, y + by, m.w, bh, band);
  }
}

// Verlaufslinie mit schwacher Fläche darunter. Skaliert auf Minimum und Maximum der Punkte.
static void chartFld(int x, int y, int w, int h, const float* pts, int n, uint16_t col, bool area) {
  uint32_t hash = (2166136261u ^ col) + area;
  for (int i = 0; i < n; i++) { uint32_t v; memcpy(&v, &pts[i], 4); hash = (hash ^ v) * 16777619u; }
  hash ^= (uint32_t)n;
  bool same = slotSame(hash);
  slotIdx++;
  if (same || !pushFn) return;

  float lo = pts[0], hi = pts[0];
  for (int i = 1; i < n; i++) { if (pts[i] < lo) lo = pts[i]; if (pts[i] > hi) hi = pts[i]; }
  const float pad = 2.0f;
  float span = hi - lo;
  static float ly[320];
  for (int c = 0; c < w; c++) {
    float t = (n > 1 && w > 1) ? (float)c * (n - 1) / (w - 1) : 0;
    int i = (int)t; if (i >= n - 1) i = n > 1 ? n - 2 : 0;
    float f = t - i;
    float v = n > 1 ? pts[i] + (pts[i + 1] - pts[i]) * f : pts[0];
    float k = span > 0 ? (v - lo) / span : 0.5f;
    ly[c] = y + pad + (1.0f - k) * (h - 1 - 2 * pad);
  }
  for (int by = y; by < y + h; by += BAND_H) {
    int bh = (y + h - by < BAND_H) ? y + h - by : BAND_H;
    for (int r = 0; r < bh; r++) {
      const uint16_t* bgRow = &curBg[(by + r) * 320 + x];
      uint16_t* dst = &band[r * w];
      float py = by + r + 0.5f;
      for (int c = 0; c < w; c++) {
        float a = ly[c], b = c ? ly[c - 1] : ly[c];
        float top = a < b ? a : b, bot = a < b ? b : a;
        float d = py < top ? top - py : py > bot ? py - bot : 0;
        float cov = 1.3f - d;                       // Linie ca. 2 px, weiche Kante
        uint16_t px = bgRow[c];
        if (area && py > a) px = blend565(col, px, (uint8_t)(70 - 50 * (py - a) / h));   // Fläche, nach unten schwächer
        if (cov > 0) px = blend565(col, px, cov >= 1 ? 255 : (uint8_t)(cov * 255));
        dst[c] = px;
      }
    }
    pushFn(x, by, w, bh, band);
  }
}

static void paintBackground(int page) {
  curBg = BGS[page];
  for (int y = 0; y < 240; y += BAND_H) {
    int bh = (240 - y < BAND_H) ? 240 - y : BAND_H;
    memcpy(band, &curBg[y * 320], (size_t)bh * 320 * 2);
    if (pushFn) pushFn(0, y, 320, bh, band);
  }
  memset(slotHash, 0, sizeof(slotHash));
}

// ================================================================
// FORMATE
// ================================================================
static void fmtDiff(double v, char* out, size_t n, int dec) {
  static const char SUF[] = "KMGTP";
  if (v < 0) v = 0;
  if (v >= 1000.0) {
    int i = -1;
    while (v >= 1000.0 && i < 4) { v /= 1000.0; i++; }
    snprintf(out, n, "%.*f%c", dec, v, SUF[i]);
  } else if (v == 0.0)    snprintf(out, n, "0");
  else if (v < 0.001)     snprintf(out, n, "%.5f", v);
  else if (v < 10.0)      snprintf(out, n, "%.3f", v);
  else if (v == floor(v)) snprintf(out, n, "%.0f", v);
  else                    snprintf(out, n, "%.1f", v);
}

// Ab 1 MH/s mit Nachkommastellen: Zwei Miner mit je 960 kH/s sind "1.92", nicht "1".
// Die Schwellen liegen dort, wo die Rundung eine Stelle mehr ergäbe: 9 999 999 wäre sonst "10.00".
static void fmtHash(uint32_t h, char* val, size_t n, const char** unit) {
  if (h >= 99950000u)      { snprintf(val, n, "%.0f", h / 1e6); *unit = "MH/s"; }
  else if (h >= 9995000u)  { snprintf(val, n, "%.1f", h / 1e6); *unit = "MH/s"; }
  else if (h >= 1000000u)  { snprintf(val, n, "%.2f", h / 1e6); *unit = "MH/s"; }
  else if (h >= 1000u) { snprintf(val, n, "%u", (unsigned)(h / 1000u));    *unit = "kH/s"; }
  else                 { snprintf(val, n, "%u", (unsigned)h);              *unit = "H/s";  }
}

static void fmtPrice(double v, char* out, size_t n) {
  if (v <= 0)         snprintf(out, n, "--.-----");
  else if (v >= 1000) snprintf(out, n, "%.0f", v);
  else if (v >= 1)    snprintf(out, n, "%.3f", v);
  else if (v >= 0.001) snprintf(out, n, "%.5f", v);
  else                snprintf(out, n, "%.8f", v);
}

// "abcdefghijklmnop" -> "abcde...lmnop" wie die Adressen in der Vorlage
static void shorten(const char* in, char* out, size_t n, int keep) {
  size_t len = strlen(in);
  if ((int)len <= keep * 2 + 3) { snprintf(out, n, "%s", in); return; }
  snprintf(out, n, "%.*s...%s", keep, in, in + len - keep);
}

int uiWeatherIconFor(int c) {
  if (c == 0) return ICON_CLEAR;
  if (c <= 2) return ICON_PARTLY;
  if (c == 3) return ICON_CLOUD;
  if (c == 45 || c == 48) return ICON_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return ICON_SNOW;
  if (c >= 95) return ICON_THUNDER;
  if (c >= 51) return ICON_RAIN;
  return ICON_CLOUD;
}

// ================================================================
// FUSSZEILE der Seiten Kurse, Wetter, Rang
// ================================================================
static void footer(const UiData& d, const char* right, uint16_t rightCol, uint16_t rightCol2) {
  char v[20], hr[12]; const char* unit;
  snprintf(v, sizeof(v), "v%s", d.version);
  fld(4, 214, 92, 18, F_S13, U_WHITE, L, v);
  fmtHash(d.hashrate, hr, sizeof(hr), &unit);
  fld(96, 208, 76, 28, F_SEG26, U_BLUE, R, hr);
  fld(176, 214, 40, 18, F_S13, U_WHITE, L, unit);
  fld(222, 214, 94, 18, F_S13, rightCol, R, right, rightCol2);
}

// ================================================================
// SEITE 1: MINER — Vorlage 2_miner.jpg
// ================================================================
static void pageMiner(const UiData& d) {
  char b[32], hr[12];
  const char* unit;
  fld(6, 5, 62, 20, F_SEG18, U_WHITE, C, d.timeValid ? d.hhmm : "--:--");

  uint32_t u = d.uptime;
  snprintf(b, sizeof(b), "%03ud%02u:%02u:%02u", (unsigned)(u / 86400), (unsigned)((u / 3600) % 24),
           (unsigned)((u / 60) % 60), (unsigned)(u % 60));
  fld(80, 7, 95, 16, F_SEG12, U_WHITE, L, b);

  if (d.wifi) snprintf(b, sizeof(b), "%d", d.rssi); else snprintf(b, sizeof(b), "--");
  fld(195, 7, 28, 16, F_SEG12, U_WHITE, R, b);
  // BTC-Kurs so groß wie die Uhrzeit, damit er lesbar ist
  if (d.btcUsd > 0) snprintf(b, sizeof(b), "$%.0f", d.btcUsd); else snprintf(b, sizeof(b), "$--");
  fld(224, 5, 92, 20, F_SEG18, U_GREEN, R, b);

  snprintf(b, sizeof(b), "%s", d.version);
  fld(4, 104, 64, 16, F_S13, U_WHITE, C, b);

  // Gefundene Blöcke in der Plakette am Logo
  if (d.blockHits > 99) snprintf(b, sizeof(b), "99+"); else snprintf(b, sizeof(b), "%u", (unsigned)d.blockHits);
  fld(46, 81, 21, 14, F_S11, d.blockHits ? U_GREEN : U_WHITE, C, b);

  // Symbole der Zählerzeilen: weiß, bei neuem Wert kurz grün
  for (int i = 0; i < 4; i++) {
    uint16_t col = d.pulse[i] >= 2 ? U_GREEN : d.pulse[i] == 1 ? blend565(U_GREEN, U_WHITE, 128) : U_WHITE;
    maskFld(144, 34 + i * 25, ui_masks[i], col);
  }

  char a[14], c[14];
  snprintf(a, sizeof(a), "%u", (unsigned)d.jobs);
  fld(176, 33, 136, 23, F_S18, U_WHITE, L, a);
  if (d.netDiff > 0) fmtDiff(d.netDiff, a, sizeof(a), 1); else snprintf(a, sizeof(a), "--");
  fld(176, 58, 136, 23, F_S18, U_WHITE, L, a);
  fmtDiff(d.bestSession, a, sizeof(a), 1);
  fmtDiff(d.bestAll, c, sizeof(c), 1);
  snprintf(b, sizeof(b), "%s / %s", a, c);
  fld(176, 83, 136, 23, F_S18, U_WHITE, L, b);
  // Shares: abgelehnt / angenommen, abgelehnte rot, sobald es welche gibt
  snprintf(b, sizeof(b), CC_ALT "%u" CC_MAIN " / %u", (unsigned)d.sharesRej, (unsigned)d.sharesOk);
  fld(176, 107, 136, 21, F_S18, U_WHITE, L, b, d.sharesRej ? U_RED : U_WHITE);

  fld(10, 151, 124, 22, F_S15, U_WHITE, C, d.wifi ? d.ip : "---.---.---.---");

  fmtHash(d.hashrate, hr, sizeof(hr), &unit);
  fld(158, 146, 96, 30, F_SEG30, U_BLUE, R, hr);
  fld(258, 152, 54, 22, F_B15, U_WHITE, L, unit);

  // Fuß: drei Kacheln, Titel und Symbole liegen im Hintergrund
  fmtDiff(d.bestAll, a, sizeof(a), 2);
  fld(8, 205, 92, 28, F_SEG18, U_BLUE, C, a);
  snprintf(b, sizeof(b), "%d", d.workers);
  fld(114, 205, 92, 28, F_SEG18, U_BLUE, C, b);
  fmtHash(d.totalHash, hr, sizeof(hr), &unit);
  fld(220, 205, 54, 28, F_SEG18, U_BLUE, R, hr);
  fld(277, 210, 37, 18, F_S13, U_WHITE, L, unit);
}

// ================================================================
// SEITE 2: UHR — Vorlage 3_uhr.jpg
// Oben links: BTC-Kurs mit 24h-Änderung, grün oder rot nach Trend.
// ================================================================
static void pageClock(const UiData& d) {
  static const char* WD[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  char b[32];
  bool btc = d.btcUsd > 0;
  uint16_t trend = d.btcChg < 0 ? U_RED : U_GREEN;
  if (btc) snprintf(b, sizeof(b), "$%.0f", d.btcUsd); else snprintf(b, sizeof(b), "$--");
  fld(4, 6, 124, 30, F_SEG26, btc ? trend : U_BLUE, L, b);
  if (btc) snprintf(b, sizeof(b), "(%.2f%%)", d.btcChg); else snprintf(b, sizeof(b), "(--)");
  fld(130, 14, 60, 18, F_S13, btc ? trend : U_WHITE, L, b);

  char hr[12]; const char* unit;
  fmtHash(d.hashrate, hr, sizeof(hr), &unit);
  fld(192, 6, 86, 30, F_SEG26, U_BLUE, R, hr);
  fld(280, 14, 36, 18, F_S13, U_WHITE, L, unit);

  fld(0, 58, 320, 96, F_SEG90, U_WHITE, C, d.timeValid ? d.hhmm : "--:--");

  if (d.timeValid) snprintf(b, sizeof(b), "%s  %02d-%02d-%04d", WD[d.wday % 7], d.day, d.month, d.year);
  else snprintf(b, sizeof(b), "---  --------");
  fld(0, 164, 320, 24, F_SEG18, U_WHITE, C, b);

  snprintf(b, sizeof(b), "%s", d.version);
  fld(6, 214, 92, 20, F_S15, U_WHITE, L, b);
  fld(100, 214, 214, 20, F_S15, U_WHITE, R, d.pool);
}

// ================================================================
// SEITE 3: KURSE — Vorlage 4_kurse.jpg
// ================================================================
static void pageMarkets(const UiData& d) {
  char b[24], p[20];
  for (int i = 0; i < 6; i++) {
    int y = 10 + i * 31;
    const UiCoin& co = d.coins[i];
    bool has = co.usd > 0;
    if (!d.marketChart) {
      fld(10, y, 62, 22, F_S18, U_WHITE, L, co.sym);
      fmtPrice(co.usd, p, sizeof(p));
      snprintf(b, sizeof(b), "$ " CC_ALT "%s", p);
      fld(74, y, 136, 22, F_S18, U_WHITE, L, b, U_BLUE2);
      if (has) snprintf(b, sizeof(b), "%+.2f%%", co.chg); else snprintf(b, sizeof(b), "--");
      fld(212, y, 100, 22, F_S18, !has ? U_MUTED : co.chg < 0 ? U_RED : U_GREEN, R, b);
      continue;
    }
    // Chart-Ansicht: Symbol, 7-Tage-Linie, Preis und 7-Tage-Änderung
    fld(10, y, 54, 22, F_S15, U_WHITE, L, co.sym);
    bool sp = co.spark && co.sparkN > 1 && co.spark[0] > 0;
    float chg7 = sp ? (co.spark[co.sparkN - 1] / co.spark[0] - 1.0f) * 100.0f : 0;
    uint16_t col = !sp ? U_DIM : chg7 < 0 ? U_RED : U_GREEN;
    static const float FLAT[2] = {1, 1};
    chartFld(64, y - 1, 150, 25, sp ? co.spark : FLAT, sp ? co.sparkN : 2, col, sp);
    fmtPrice(co.usd, p, sizeof(p));
    snprintf(b, sizeof(b), "$%s", p);
    fld(218, y - 3, 94, 14, F_S13, U_BLUE2, R, b);
    if (sp) snprintf(b, sizeof(b), "7T %+.1f%%", chg7); else snprintf(b, sizeof(b), "7T --");
    fld(218, y + 12, 94, 14, F_S13, col == U_DIM ? U_MUTED : col, R, b);
  }
  char pg[16];
  if (d.marketChart) snprintf(pg, sizeof(pg), "7T-Chart %d/5", d.page + 1);
  else snprintf(pg, sizeof(pg), "%d/5", d.page + 1);
  footer(d, pg, U_WHITE, U_WHITE);
}

// ================================================================
// SEITE 4: WETTER — Vorlage 1_wetter.jpg
// "$12k m³/s" aus der Vorlage ist sinnlos, dort steht der Wind.
// ================================================================
static void pageWeather(const UiData& d) {
  char b[40];
  char hh[3] = "--", mm[3] = "--";
  if (d.timeValid) { hh[0]=d.hhmm[0]; hh[1]=d.hhmm[1]; mm[0]=d.hhmm[3]; mm[1]=d.hhmm[4]; }
  fld(2, 6, 64, 50, F_SEG44, U_WHITE, R, hh);
  fld(70, 6, 64, 50, F_SEG44, U_BLUE, L, mm);

  iconFld(136, 3, ui_icons_big[uiWeatherIconFor(d.wxValid ? d.wxCode : 3)]);

  if (d.wxValid) snprintf(b, sizeof(b), "%.0f", d.wxTemp); else snprintf(b, sizeof(b), "--");
  fld(204, 6, 76, 50, F_S38, U_WHITE, R, b);
  snprintf(b, sizeof(b), "\xC2\xB0%c", d.wxUnit);
  fld(284, 10, 32, 20, F_S15, U_WHITE, L, b);

  fld(24, 62, 44, 20, F_S15, U_BLUE, L, d.sunrise);
  fld(24, 82, 44, 20, F_S15, U_BLUE, L, d.sunset);
  if (d.wxValid) snprintf(b, sizeof(b), "%d%%", d.wxHumidity); else snprintf(b, sizeof(b), "--%%");
  fld(90, 62, 66, 20, F_S15, U_WHITE, L, b);
  if (d.wxValid) snprintf(b, sizeof(b), "%.0fhPa", d.wxPressure); else snprintf(b, sizeof(b), "--hPa");
  fld(90, 82, 66, 20, F_S15, U_WHITE, L, b);

  if (d.wxValid) snprintf(b, sizeof(b), "Feels like " CC_ALT "%.0f\xC2\xB0", d.wxFeels);
  else snprintf(b, sizeof(b), "Feels like " CC_ALT "--");
  fld(160, 62, 156, 20, F_S15, U_WHITE, R, b, U_BLUE);
  if (d.wxValid) snprintf(b, sizeof(b), "%.0f %s", d.wxWind, d.wxSpeed); else snprintf(b, sizeof(b), "-- %s", d.wxSpeed);
  fld(158, 82, 56, 20, F_S13, U_WHITE, R, b);
  // Feinstaub mit Einheit, sonst liest sich der Wert wie eine Ziffer ohne Bedeutung
  if (d.wxPmValid) snprintf(b, sizeof(b), "PM2.5 " CC_ALT "%d" CC_MAIN "\xC2\xB5g/m\xC2\xB3", d.wxPm25);
  else snprintf(b, sizeof(b), "PM2.5 " CC_ALT "--");
  fld(216, 82, 100, 20, F_S13, U_WHITE, R, b, U_BLUE);

  for (int i = 0; i < 3; i++) {
    int y = 107 + i * 24;
    const UiForecast& f = d.fc[i];
    fld(10, y, 50, 22, F_S15, U_BLUE, L, d.wxValid ? f.date : "--");
    iconFld(62, y + 1, ui_icons_small[uiWeatherIconFor(d.wxValid ? f.code : 3)]);
    if (f.pmValid) snprintf(b, sizeof(b), "PM2.5/10 (" CC_ALT "%d/%d" CC_MAIN ")", f.pm25, f.pm10);
    else snprintf(b, sizeof(b), "PM2.5/10 (" CC_ALT "--/--" CC_MAIN ")");
    fld(96, y, 116, 22, F_S13, U_WHITE, L, b, U_GREEN);
    fld(214, y, 96, 22, F_S15, U_BLUE, R, d.wxValid ? f.range : "--");
  }
  fld(30, 180, 280, 19, F_S13, U_WHITE, L, d.city);

  snprintf(b, sizeof(b), "Updated: " CC_ALT "%s", d.wxUpdated);
  footer(d, b, U_BLUE, U_WHITE);
}

// ================================================================
// SEITE 5: RANG — Vorlage 5_rank.jpg
// Die Daten kommen nur aus dem eigenen LAN (M7), daher "LAN" im Titel
// statt "GLOBAL". Sortierung: All-Time Best Diff.
// ================================================================
static void pageRank(const UiData& d) {
  char nm[24], val[14];
  // Podest: Reihenfolge der Boxen #2, #1, #3
  static const int order[3] = {1, 0, 2};
  static const int bx[3] = {6, 110, 216};
  for (int k = 0; k < 3; k++) {
    int r = order[k];
    if (r < d.rankCount) {
      shorten(d.rank[r].name, nm, sizeof(nm), 5);
      fmtDiff(d.rank[r].best, val, sizeof(val), 2);
    } else { snprintf(nm, sizeof(nm), "---"); snprintf(val, sizeof(val), "--"); }
    uint16_t nc = (r < d.rankCount && d.rank[r].self) ? U_LIGHT : U_WHITE;
    fld(bx[k], 46, 98, 15, F_S11, nc, C, nm);
    fld(bx[k], 61, 98, 17, F_S15, r == 0 ? U_LIGHT : U_WHITE, C, val);
  }
  for (int i = 0; i < 6; i++) {
    int y = 85 + i * 16, r = i + 3;
    if (r < d.rankCount) {
      shorten(d.rank[r].name, nm, sizeof(nm), 5);
      fmtDiff(d.rank[r].best, val, sizeof(val), 2);
    } else { snprintf(nm, sizeof(nm), "---"); snprintf(val, sizeof(val), "--"); }
    fld(52, y, 170, 15, F_S13, (r < d.rankCount && d.rank[r].self) ? U_BLUE : U_WHITE, L, nm);
    fld(236, y, 74, 15, F_S13, U_BLUE, R, val);
  }
  // Eigene Zeile, feste Position in der Vorlage
  char rk[14]; snprintf(rk, sizeof(rk), "#%d", d.selfRank + 1);
  fld(8, 183, 42, 19, F_B15, U_WHITE, L, rk);
  const UiRankRow* me = d.rankCount > d.selfRank ? &d.rank[d.selfRank] : nullptr;
  shorten(me ? me->name : "---", nm, sizeof(nm), 4);
  fld(52, 183, 84, 19, F_S13, U_WHITE, L, nm);
  fmtDiff(me ? me->best : 0, val, sizeof(val), 2);
  fld(236, 183, 74, 19, F_S15, U_WHITE, R, val);
  fld(136, 183, 98, 19, F_S13, U_LIGHT, C, "You Are Here");

  char b[24]; snprintf(b, sizeof(b), "Updated: " CC_ALT "%s", d.rankUpdated);
  footer(d, b, U_BLUE, U_WHITE);
}

// ================================================================
// EINSTIEG
// ================================================================
void uiDraw(const UiData& d) {
  int p = d.page % 5;
  int key = p | ((p == UI_MARKETS && d.marketChart) ? 8 : 0);   // andere Ansicht: Felder liegen anders
  if (key != lastPage) { paintBackground(p); lastPage = key; }
  slotIdx = 0;
  switch (p) {
    case UI_MINER:   pageMiner(d);   break;
    case UI_CLOCK:   pageClock(d);   break;
    case UI_MARKETS: pageMarkets(d); break;
    case UI_WEATHER: pageWeather(d); break;
    case UI_RANK:    pageRank(d);    break;
  }
}
