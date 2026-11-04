// PC-Test der Display-Seiten: rendert dieselben ui.cpp-Seiten wie die Firmware
// in einen Framebuffer, schreibt PPM-Dateien und prueft den Renderer.
// Bauen und Ausfuehren: tools/design/preview.sh
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "ui.h"
#include "net_parse.h"

static uint16_t fb[240][320];
static long pushCount = 0, pushPixels = 0;
static int minX = 9999, minY = 9999, maxX = -1, maxY = -1;
static int outOfBounds = 0;

static void push(int x, int y, int w, int h, const uint16_t* px) {
  pushCount++;
  pushPixels += w * h;
  if (x < 0 || y < 0 || x + w > 320 || y + h > 240) { outOfBounds++; return; }
  if (x < minX) minX = x;
  if (y < minY) minY = y;
  if (x + w - 1 > maxX) maxX = x + w - 1;
  if (y + h - 1 > maxY) maxY = y + h - 1;
  for (int r = 0; r < h; r++) memcpy(fb[y + r] + x, px + (size_t)r * w, (size_t)w * 2);
}

static void savePpm(const char* path) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n320 240\n255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = fb[y][x];
      uint8_t rgb[3] = {(uint8_t)((c >> 8 & 0xF8) | (c >> 13)), (uint8_t)((c >> 3 & 0xFC) | (c >> 9 & 3)),
                        (uint8_t)((c << 3 & 0xF8) | (c >> 2 & 7))};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static void resetStats() { pushCount = pushPixels = 0; minX = minY = 9999; maxX = maxY = -1; }

static int sparkFound = -1;
static SparkSet sparkOut;
static float demoSpark[6][SPARK_N];
static int   demoSparkN[6];

// Beispielwerte im Umfang der Vorlagen. Keine Messwerte vom Geraet.
static void demoFull(UiData& d) {
  memset(&d, 0, sizeof(d));
  strcpy(d.version, "7.5.0");
  d.timeValid = true; strcpy(d.hhmm, "22:10"); d.wday = 6; d.day = 13; d.month = 6; d.year = 2026;
  d.uptime = 3888;
  d.wifi = true; d.rssi = -57; strcpy(d.ip, "192.168.1.50"); strcpy(d.pool, "dgb-stratum.solominer.net");
  d.hashrate = 12340; d.totalHash = 24680; d.workers = 2;
  d.poolDiff = 0.002; d.netDiff = 1206787.95; d.bestSession = 0.965; d.bestAll = 181.5;
  d.sharesOk = 975; d.sharesTotal = 977;
  d.jobs = 1342; d.blockHits = 0; d.pulse[PULSE_SHARES] = 2; d.pulse[PULSE_JOBS] = 1;
  d.btcUsd = 64274; d.btcChg = 1.1;
  static const char* SYM[6] = {"DGB", "ADA", "DOGE", "XEC", "SHIB", "PEPE"};
  for (int i = 0; i < 6; i++) d.coins[i].sym = SYM[i];
  static const double USD[6] = {0.00321703, 0.193579, 0.075787, 0.00000643, 0.00000469, 0.00000499};
  static const double CHG[6] = {-0.45, 0.29, 0.93, -0.92, -1.19, -0.78};
  for (int i = 0; i < 6; i++) { d.coins[i].usd = USD[i]; d.coins[i].chg = CHG[i]; }
  // Verlauf aus der echten Antwort in testdata/markets.json, falls schon geladen
  for (int i = 0; i < 6; i++) { d.coins[i].spark = demoSpark[i]; d.coins[i].sparkN = demoSparkN[i]; }
  d.wxPmValid = true; d.wxPm25 = 6;
  d.wxValid = true; d.wxTemp = 12; d.wxFeels = 10; d.wxPressure = 1013; d.wxWind = 12; d.wxHumidity = 77; d.wxCode = 2;
  d.wxUnit = 'C'; strcpy(d.wxSpeed, "km/h"); strcpy(d.sunrise, "04:51"); strcpy(d.sunset, "21:43");
  const char* dt[3] = {"06-14", "06-15", "06-16"}; const char* rg[3] = {"11 - 17\xC2\xB0""C", "12 - 19\xC2\xB0""C", "8 - 17\xC2\xB0""C"};
  int cd[3] = {61, 3, 73};
  for (int i = 0; i < 3; i++) { strcpy(d.fc[i].date, dt[i]); strcpy(d.fc[i].range, rg[i]); d.fc[i].code = cd[i]; d.fc[i].pmValid = true; d.fc[i].pm25 = 3 + i * 4; d.fc[i].pm10 = 7 + i * 6; }
  strcpy(d.city, "Berlin, Brandenburg, Germany"); strcpy(d.wxUpdated, "13:52");
  const char* nm[9] = {"cyd-keller-rechts", "dgb_worker1", "werkstatt-esp32", "buero", "cyd-05", "garage", "dachboden-2", "cyd-08", "cyd-09"};
  double bd[9] = {40450000, 1190000, 570160, 449780, 354080, 214030, 153910, 126750, 118230};
  d.rankCount = 9; d.selfRank = 1;
  for (int i = 0; i < 9; i++) { strcpy(d.rank[i].name, nm[i]); d.rank[i].best = bd[i]; d.rank[i].self = (i == 1); }
  strcpy(d.rankUpdated, "13:52");
}

// So sieht es wirklich aus, bevor Daten da sind: alles leer oder Null.
static void demoEmpty(UiData& d) {
  memset(&d, 0, sizeof(d));
  strcpy(d.version, "7.5.0");
  strcpy(d.hhmm, "--:--");
  strcpy(d.pool, "");
  static const char* SYM[6] = {"DGB", "ADA", "DOGE", "XEC", "SHIB", "PEPE"};
  for (int i = 0; i < 6; i++) d.coins[i].sym = SYM[i];
  d.wxUnit = 'C'; strcpy(d.wxSpeed, "km/h"); strcpy(d.sunrise, "--:--"); strcpy(d.sunset, "--:--");
  strcpy(d.city, "Standort wird ermittelt..."); strcpy(d.wxUpdated, "--:--"); strcpy(d.rankUpdated, "--:--");
  d.rankCount = 1; strcpy(d.rank[0].name, "dgb_worker1"); d.rank[0].self = true;
}

// Grenzfall: die breitesten realistischen Werte. Darf nirgends abgeschnitten werden.
static void demoWide(UiData& d) {
  demoFull(d);
  d.uptime = 999u * 86400u + 86399u; d.rssi = -100;
  strcpy(d.ip, "192.168.100.255");
  d.hashrate = 999000u; d.totalHash = 999000000u; d.workers = 13;
  d.poolDiff = 123456.7; d.netDiff = 999.9e12; d.bestSession = 999.9e9; d.bestAll = 999.9e12;
  d.sharesOk = 123456; d.sharesTotal = 123456;
  d.jobs = 9999999; d.blockHits = 1234;
  d.btcUsd = 123456; d.btcChg = -12.34;
  d.coins[0].usd = 0.00000123; d.coins[0].chg = -100.0;
  d.wxTemp = -18; d.wxFeels = -24; d.wxPressure = 1045; d.wxWind = 120; d.wxHumidity = 100; d.wxPm25 = 999; d.wxPmValid = true;
  for (int i = 0; i < 3; i++) { strcpy(d.fc[i].range, "-18 - -12\xC2\xB0""C"); d.fc[i].pmValid = true; d.fc[i].pm25 = 999; d.fc[i].pm10 = 999; }
  strcpy(d.city, "Berlin, Brandenburg, Germany");
  d.rankCount = 13; d.selfRank = 12;
  for (int i = 0; i < 13; i++) { strcpy(d.rank[i].name, "tomessino-worker-mit-langem-namen"); d.rank[i].best = 999.99e12; d.rank[i].self = i == 12; }
}

static int fails = 0;
static void check(const char* name, bool ok) {
  printf("%-52s %s\n", name, ok ? "OK" : "FEHLER");
  if (!ok) fails++;
}

static char* readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("Fehlt: %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char* b = (char*)malloc((size_t)n + 1);
  size_t got = fread(b, 1, (size_t)n, f); b[got] = 0;
  fclose(f);
  return b;
}

// Echte API-Antworten auswerten. check_parse.py rechnet die Erwartung unabhaengig nach.
static void parseTests(const char* dir) {
  char path[512];
  snprintf(path, sizeof(path), "%s/parse.txt", dir);
  FILE* o = fopen(path, "w");

  WatchEntry wl[WATCH_MAX], one[WATCH_MAX];
  int wn = parseWatchlist(WATCH_DEFAULT, wl, WATCH_MAX);
  check("Watchlist: Standard hat 5 Coins", wn == 5 && !strcmp(wl[3].id, "shiba-inu") && !strcmp(wl[3].sym, "SHIB"));
  check("Watchlist: Symbol wird gross", parseWatchlist("bitcoin-cash:bch", one, WATCH_MAX) == 1 && !strcmp(one[0].sym, "BCH"));
  check("Watchlist: leer ist gueltig", parseWatchlist("", one, WATCH_MAX) == 0);
  check("Watchlist: ohne Doppelpunkt ungueltig", parseWatchlist("cardano", one, WATCH_MAX) == -1);
  check("Watchlist: Symbol zu lang ungueltig", parseWatchlist("cardano:ABCDEF", one, WATCH_MAX) == -1);
  check("Watchlist: Grossbuchstaben in der ID ungueltig", parseWatchlist("Cardano:ADA", one, WATCH_MAX) == -1);
  check("Watchlist: Sonderzeichen in der ID ungueltig", parseWatchlist("a&b=c:ADA", one, WATCH_MAX) == -1);
  check("Watchlist: Komma am Ende ungueltig", parseWatchlist("cardano:ADA,", one, WATCH_MAX) == -1);
  check("Watchlist: mehr als 5 ungueltig", parseWatchlist("a:A,b:B,c:C,d:D,e:E,f:F", one, WATCH_MAX) == -1);
  char ids[220];
  check("Abfrage-IDs", buildPriceIds(ids, sizeof(ids), wl, wn) > 0 && !strcmp(ids, "digibyte,bitcoin,cardano,dogecoin,ecash,shiba-inu,pepe"));
  check("Abfrage-IDs: zu kleiner Puffer", buildPriceIds(ids, 20, wl, wn) == 0);
  CoinQuote q[COIN_COUNT];
  int found = parsePrices(readFile("tools/design/testdata/prices.json"), wl, wn, q);
  check("Kurse: alle 7 Coins gefunden", found == COIN_COUNT);
  for (int i = 0; i < COIN_COUNT; i++) fprintf(o, "coin %d %.10g %.10g\n", q[i].ok, q[i].usd, q[i].chg);

  AirData a;
  check("Luft: Antwort auswertbar", parseAirQuality(readFile("tools/design/testdata/air.json"), a));
  fprintf(o, "now %d %d %d\n", a.nowOk, a.pm25Now, a.pm10Now);
  for (int i = 0; i < 3; i++) fprintf(o, "day %d %d %d\n", a.day[i].ok, a.day[i].pm25, a.day[i].pm10);
  // Verlauf: Reihenfolge wie die Coins der Watchlist
  check("Charts: alle 7 Verlaeufe gefunden", sparkFound == COIN_COUNT);
  for (int i = 0; i < COIN_COUNT; i++) {
    fprintf(o, "spark %d", sparkOut.n[i]);
    for (int k = 0; k < sparkOut.n[i]; k++) fprintf(o, " %.9g", sparkOut.pts[i][k]);
    fprintf(o, "\n");
  }
  fclose(o);
  // Gleiche Antwort in einem Stück: gleiches Ergebnis
  {
    WatchEntry w2[WATCH_MAX]; int n2 = parseWatchlist(WATCH_DEFAULT, w2, WATCH_MAX);
    static SparkSet one;
    SparkParser p1(w2, n2, one);
    const char* js = readFile("tools/design/testdata/markets.json");
    p1.feed(js, strlen(js));
    check("Charts: Stueckelung aendert nichts", p1.found() == COIN_COUNT && !memcmp(&one, &sparkOut, sizeof(one)));
    static SparkSet bad;
    SparkParser p2(w2, n2, bad);
    const char* err = "{\"status\":{\"error_code\":429,\"error_message\":\"x\"}}";
    p2.feed(err, strlen(err));
    check("Charts: 429-Antwort = 0 Verlaeufe", p2.found() == 0);
    SparkParser p3(w2, n2, bad);
    const char* nul = "[{\"id\":\"cardano\",\"sparkline_in_7d\":{\"price\":[1,null,3]}},{\"id\":\"unbekannt\",\"sparkline_in_7d\":{\"price\":[5,6]}}]";
    p3.feed(nul, strlen(nul));
    check("Charts: null uebersprungen, fremde ID ignoriert", p3.found() == 1 && bad.n[COIN_WATCH] == 2 && bad.pts[COIN_WATCH][1] == 3.0f);
    // bitcoin fest und zusaetzlich in der Watchlist: die Antwort enthaelt es nur einmal
    WatchEntry w3[WATCH_MAX]; int n3 = parseWatchlist("bitcoin:BTC,cardano:ADA", w3, WATCH_MAX);
    static SparkSet dup;
    SparkParser p4(w3, n3, dup);
    const char* one2 = "[{\"id\":\"bitcoin\",\"sparkline_in_7d\":{\"price\":[1,2]}}]";
    p4.feed(one2, strlen(one2));
    check("Charts: doppelte ID fuellt beide Plaetze", p4.found() == 2 && dup.n[COIN_BTC] == 2 && dup.n[COIN_WATCH] == 2);
  }

  // Fehlerfaelle
  check("Kurse: kaputtes JSON = -1", parsePrices("{\"digibyte\":", wl, wn, q) == -1);
  check("Kurse: 429-Antwort = 0 Coins", parsePrices("{\"status\":{\"error_code\":429,\"error_message\":\"x\"}}", wl, wn, q) == 0);
  check("Kurse: fehlender Coin bleibt ungueltig", parsePrices("{\"pepe\":{\"usd\":1e-05}}", wl, wn, q) == 1 && q[COIN_WATCH + 4].ok && !q[COIN_DGB].ok);
  check("Luft: kaputtes JSON = false", !parseAirQuality("{\"hourly\":", a));
  check("Luft: Fehlerantwort = false", !parseAirQuality("{\"error\":true,\"reason\":\"x\"}", a));
  // 24 Stunden heute, morgen nur null, uebermorgen 12x 2 und 12x null, Tag 3 fehlt
  char hours[1200]; size_t l = 0;
  for (int i = 0; i < 60; i++) {
    const char* v = i < 24 ? "9" : i < 48 ? "null" : "2";
    l += (size_t)snprintf(hours + l, sizeof(hours) - l, "%s%s", i ? "," : "", v);
  }
  char js[2600];
  snprintf(js, sizeof(js), "{\"current\":{\"pm2_5\":null,\"pm10\":1},\"hourly\":{\"pm2_5\":[%s],\"pm10\":[%s]}}", hours, hours);
  bool ok = parseAirQuality(js, a);
  check("Luft: null-Stunden und fehlende Tage", ok && !a.nowOk && !a.day[0].ok && a.day[1].ok && a.day[1].pm25 == 2 && !a.day[2].ok);
}

// 7-Tage-Verlauf aus der echten Antwort, in zufällig großen Stücken wie aus dem Netz
static void loadSparks() {
  WatchEntry wl[WATCH_MAX];
  int wn = parseWatchlist(WATCH_DEFAULT, wl, WATCH_MAX);
  const char* js = readFile("tools/design/testdata/markets.json");
  size_t len = strlen(js), pos = 0;
  SparkParser p(wl, wn, sparkOut);
  srand(7);
  while (pos < len) {
    size_t n = 1 + (size_t)(rand() % 700);
    if (pos + n > len) n = len - pos;
    p.feed(js + pos, n);
    pos += n;
  }
  sparkFound = p.found();
  static const int idx[6] = {COIN_DGB, COIN_WATCH, COIN_WATCH + 1, COIN_WATCH + 2, COIN_WATCH + 3, COIN_WATCH + 4};
  for (int i = 0; i < 6; i++) {
    demoSparkN[i] = sparkOut.n[idx[i]];
    memcpy(demoSpark[i], sparkOut.pts[idx[i]], sizeof(demoSpark[i]));
  }
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  loadSparks();
  static const char* NAMES[5] = {"miner", "clock", "markets", "weather", "rank"};
  uiSetPush(push);
  static UiData d;
  char path[512];

  // 1. Alle Seiten voll und leer rendern
  static const char* VAR[3] = {"voll", "leer", "breit"};
  for (int variant = 0; variant < 3; variant++) {
    for (int p = 0; p < 5; p++) {
      if (variant == 0) demoFull(d); else if (variant == 1) demoEmpty(d); else demoWide(d);
      d.page = p;
      uiClipCount = 0;
      uiDraw(d);
      snprintf(path, sizeof(path), "%s/%s_%s.ppm", dir, NAMES[p], VAR[variant]);
      savePpm(path);
      char label[80];
      snprintf(label, sizeof(label), "%s %s: kein Text abgeschnitten", NAMES[p], VAR[variant]);
      check(label, uiClipCount == 0);
    }
  }
  for (int variant = 0; variant < 3; variant++) {
    if (variant == 0) demoFull(d); else if (variant == 1) demoEmpty(d); else demoWide(d);
    d.page = UI_MARKETS; d.marketChart = true;
    uiClipCount = 0;
    uiDraw(d);
    snprintf(path, sizeof(path), "%s/chart_%s.ppm", dir, VAR[variant]);
    savePpm(path);
    char label[80];
    snprintf(label, sizeof(label), "markets chart %s: kein Text abgeschnitten", VAR[variant]);
    check(label, uiClipCount == 0);
  }
  check("Alle Ausgaben innerhalb 320x240", outOfBounds == 0);

  // Ansichtswechsel Kurse -> Charts: ganzer Bildschirm neu
  demoFull(d); d.page = UI_MARKETS; uiDraw(d);
  resetStats(); d.marketChart = true; uiDraw(d);
  check("Kurse -> Charts: Hintergrund vollstaendig", minX == 0 && minY == 0 && maxX == 319 && maxY == 239);

  // Leuchten: nur das Symbol wird neu ausgegeben
  demoFull(d); d.page = UI_MINER; uiDraw(d);
  resetStats(); d.pulse[PULSE_NET] = 2; uiDraw(d);
  check("miner: Leuchten gibt nur das Symbol aus (20x20)", pushPixels == 400 && minX == 144 && minY == 59);

  // 2. Gleiche Daten: nichts darf neu gezeichnet werden
  for (int p = 0; p < 5; p++) {
    demoFull(d); d.page = p; uiDraw(d);
    resetStats(); uiDraw(d);
    char label[80]; snprintf(label, sizeof(label), "%s: gleiche Daten = 0 Ausgaben", NAMES[p]);
    check(label, pushCount == 0);
  }

  // 3. Nur Hashrate aendert sich: nur deren Feld wird ausgegeben
  demoFull(d); d.page = UI_MINER; uiDraw(d);
  resetStats(); d.hashrate = 13340; uiDraw(d);
  printf("  Hashrate-Aenderung: %ld Pixel, Box x%d-%d y%d-%d\n", pushPixels, minX, maxX, minY, maxY);
  check("miner: nur Hashrate-Feld neu (< 5 % der Flaeche)", pushCount > 0 && pushPixels < 320 * 240 / 20);

  // 4. Seitenwechsel: ganzer Bildschirm
  resetStats(); d.page = UI_CLOCK; uiDraw(d);
  check("Seitenwechsel: Hintergrund vollstaendig", minX == 0 && minY == 0 && maxX == 319 && maxY == 239);

  // 6. Umlaut und Text laenger als Box: darf nicht aus der Box laufen
  demoFull(d); d.page = UI_WEATHER; uiDraw(d);
  snprintf(d.city, sizeof(d.city), "K\xC3\xB6ln, Nordrhein-Westfalen, Deutschland usw.");
  resetStats(); uiDraw(d);
  check("Zu langer Standort bleibt in seiner Box", maxX <= 319 && outOfBounds == 0);

  parseTests(dir);

  printf("%s\n", fails ? "TESTS FEHLGESCHLAGEN" : "ALLE TESTS OK");
  return fails ? 1 : 0;
}
