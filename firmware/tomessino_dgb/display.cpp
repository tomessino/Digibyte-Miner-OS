#include "display.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "mining.h"
#include "net_services.h"
#include "stratum.h"
#include "ui.h"

Page curPage = PAGE_MINER;

TFT_eSPI        tft = TFT_eSPI();
SPIClass        touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

uint32_t tDisplay    = 0;
uint32_t tLastTouch  = 0;  // Screen-Timeout
bool     configMode  = false;
bool     passReset   = false;

static uint32_t tBtnDown    = 0;
static bool     btnWasDown  = false;

// ================================================================
// TOUCH + BUTTON HANDLER
// Tap (Display oder Boot-Button): Seite wechseln / Screen wecken
// Long-Press Boot-Button 3s: Config-Modus
// ================================================================
static uint32_t tLastTap = 0;
static volatile int pageRequest = -1;

void displayRequestPage(int page) { pageRequest = page; }

// Farbtest: Inversion und Farbreihenfolge zur Laufzeit umstellen
static volatile int colorReq = -1;   // Bit 0: invert, Bit 1: BGR, Bit 2: Balken
static bool colorBars = false;

void displayRequestColorTest(int invert, int bgr, int bars) {
  colorReq = (invert ? 1 : 0) | (bgr ? 2 : 0) | (bars ? 4 : 0);
}

static void drawColorBars() {
  static const struct { uint16_t c, t; const char* n; } B[6] = {
    {0xF800, 0xFFFF, "ROT"}, {0x07E0, 0x0000, "GRUEN"}, {0x001F, 0xFFFF, "BLAU"},
    {0xFFFF, 0x0000, "WEISS"}, {0x0000, 0xFFFF, "SCHWARZ"}, {0x0010, 0xFFFF, "DUNKELBLAU"},
  };
  for (int i = 0; i < 6; i++) {
    tft.fillRect(0, i * 40, 320, 40, B[i].c);
    tft.setTextColor(B[i].t, B[i].c);
    tft.setTextSize(2);
    tft.setCursor(8, i * 40 + 12);
    tft.print(B[i].n);
  }
}

void inputTick() {
  uint32_t now = millis();

  // ---- Farbtest per Web (Gerätetest) ----
  int cr = colorReq;
  if (cr >= 0) {
    colorReq = -1;
    tLastTouch = now;
    if (!miner.screenOn) { miner.screenOn = true; analogWrite(PIN_BL, cfg.brightness); }
    tft.invertDisplay(cr & 1);
    tft.writecommand(TFT_MADCTL);
    tft.writedata(TFT_MAD_MX | TFT_MAD_MV | ((cr & 2) ? TFT_MAD_BGR : TFT_MAD_RGB));  // Rotation 1
    colorBars = cr & 4;
    if (colorBars) drawColorBars();
    else { uiInvalidate(); tDisplay = 0; }
  }

  // ---- Seitenwahl per Web (Gerätetest) ----
  int req = pageRequest;
  if (req >= 0) {
    pageRequest = -1;
    if (req < PAGE_COUNT) {
      tLastTouch = now;
      if (!miner.screenOn) {
        miner.screenOn = true;
        analogWrite(PIN_BL, cfg.brightness);
      }
      curPage = (Page)req;
      tDisplay = 0;
      Serial.printf("[SCR] Seite %d per Web\n", req);
    }
  }

  // ---- XPT2046 Touch ----
  if (touch.tirqTouched() && touch.touched()) {
    if (now - tLastTap > 400) {  // Entprell 400ms
      tLastTap = now;
      tLastTouch = now;
      if (!miner.screenOn) {
        miner.screenOn = true;
        analogWrite(PIN_BL, cfg.brightness);
      } else {
        curPage = (Page)((curPage + 1) % PAGE_COUNT);
        tDisplay = 0;  // Sofort neu zeichnen
        Serial.printf("[SCR] Seite %d per Touch\n", (int)curPage);
      }
    }
  }

  // ---- Boot-Button GPIO0 ----
  bool pressed = (digitalRead(PIN_BTN) == LOW);
  if (pressed && !btnWasDown) {
    tBtnDown = now;
    btnWasDown = true;
  }
  if (!pressed && btnWasDown) {
    uint32_t held = now - tBtnDown;
    btnWasDown = false;
    if (held >= BTN_RESET_MS) {
      passReset = true;   // 10s = Dashboard-Passwort löschen
    } else if (held >= BTN_LONG_MS) {
      configMode = true;  // 3s = Config-Modus
    } else if (held >= BTN_TAP_MS) {
      tLastTouch = now;
      if (!miner.screenOn) {
        miner.screenOn = true;
        analogWrite(PIN_BL, cfg.brightness);
      } else {
        curPage = (Page)((curPage + 1) % PAGE_COUNT);
        tDisplay = 0;
        Serial.printf("[SCR] Seite %d per BOOT-Taste\n", (int)curPage);
      }
    }
  }

  // Screen-Timeout (Dimmen)
  if (miner.screenOn && cfg.screenTimeout > 0) {
    if (now - tLastTouch > (uint32_t)cfg.screenTimeout * 1000) {
      miner.screenOn = false;
      analogWrite(PIN_BL, 15);
      Serial.printf("[SCR] gedimmt nach %u s\n", cfg.screenTimeout);
    }
  }
}

// ================================================================
// SPLASH + WIFI-SETUP SCREEN
// ================================================================
void tftSplash() {
  tft.fillScreen(C_BG);
  tft.fillRect(0,0,320,50,C_BG2);
  // DGB Logo (einfaches Polygon)
  tft.fillCircle(30,25,18,C_ACCENT);
  tft.fillCircle(30,25,12,C_BG2);
  tft.setTextColor(C_ACCENT,C_BG2); tft.setTextSize(1);
  tft.setCursor(22,22); tft.print("DGB");
  // Titel
  tft.setTextColor(C_TEXT,C_BG2); tft.setTextSize(2);
  tft.setCursor(58,8); tft.print("Tomessino-DGB");
  tft.setTextColor(C_MUTED,C_BG2); tft.setTextSize(1);
  tft.setCursor(58,30); tft.print("v" FW_VERSION " DigiByte SHA-256d");
  tft.drawFastHLine(0,50,320,C_ACCENT);
}

void tftWifiSetup() {
  tftSplash();
  tft.setTextColor(C_YELLOW,C_BG); tft.setTextSize(1);
  tft.setCursor(4,60); tft.print("WiFi-Einrichtung");
  tft.setTextColor(C_TEXT,C_BG);
  tft.setCursor(4,76);  tft.print("1. WLAN verbinden:");
  tft.setTextColor(C_GREEN,C_BG);
  tft.setCursor(14,90); tft.print("\"" AP_SSID "\"");
  tft.setTextColor(C_TEXT,C_BG);
  tft.setCursor(4,106); tft.print("2. Browser: 192.168.4.1");
  tft.setTextColor(C_MUTED,C_BG);
  tft.setCursor(4,122); tft.print("Timeout: " + String(AP_TIMEOUT) + "s");
}

void tftConnected() {
  tftSplash();
  tft.setTextColor(C_GREEN,C_BG); tft.setCursor(4,60); tft.print("WLAN verbunden!");
  tft.setTextColor(C_TEXT,C_BG);
  tft.setCursor(4,76); tft.print("SSID: "+WiFi.SSID());
  tft.setCursor(4,90); tft.print("IP:   "+WiFi.localIP().toString());
  tft.setTextColor(C_ACCENT,C_BG);
  tft.setCursor(4,106); tft.print("http://"+WiFi.localIP().toString());
  tft.setTextColor(C_MUTED,C_BG);
  tft.setCursor(4,124); tft.print("Tap Boot-Button = Seite wechseln");
  tft.setCursor(4,138); tft.print("3s = Config, 10s = Passwort loeschen");
}

// ================================================================
// TFT UPDATE — füllt UiData aus dem Firmware-Zustand, ui.cpp zeichnet.
// ================================================================
static void uiPushToTft(int x, int y, int w, int h, const uint16_t* px) {
  tft.pushImage(x, y, w, h, const_cast<uint16_t*>(px));  // RAM-Variante, px liegt im Puffer
}

// Uhrzeit eines vergangenen Ereignisses (millis-Wert) als "HH:MM"
static void timeOfEvent(uint32_t tEvent, char* out, size_t n) {
  if (!tEvent || !miner.ntpOk) { strlcpy(out, "--:--", n); return; }
  time_t t = (time_t)(ntpCli.getEpochTime() + (long)(cfg.timezone * 3600)) - (millis() - tEvent) / 1000;
  struct tm* ti = localtime(&t);
  snprintf(out, n, "%02d:%02d", ti->tm_hour, ti->tm_min);
}

static void fillRank(UiData& d) {
  int n = 0;
  uint32_t lastSeen = 0, total = miner.hashrate;
  strlcpy(d.rank[n].name, strlen(cfg.worker) ? cfg.worker : "self", sizeof(d.rank[n].name));
  d.rank[n].best = miner.bestDiffAllTime;
  d.rank[n].self = true;
  n++;
  for (int i = 0; i < swarmPeerCount && n < UI_MAX_RANK; i++) {
    if (!swarmPeers[i].active) continue;
    strlcpy(d.rank[n].name, swarmPeers[i].workerName, sizeof(d.rank[n].name));
    d.rank[n].best = swarmPeers[i].bestDiffAllTime;
    d.rank[n].self = false;
    total += swarmPeers[i].hashrate;
    if (swarmPeers[i].lastSeen > lastSeen) lastSeen = swarmPeers[i].lastSeen;
    n++;
  }
  // Absteigend nach All-Time Best Diff (n ist klein)
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1 - i; j++) {
      if (d.rank[j].best < d.rank[j+1].best) {
        UiRankRow tmp = d.rank[j]; d.rank[j] = d.rank[j+1]; d.rank[j+1] = tmp;
      }
    }
  }
  d.rankCount = n;
  d.selfRank = 0;
  for (int i = 0; i < n; i++) if (d.rank[i].self) { d.selfRank = i; break; }
  d.workers = n;
  d.totalHash = total;
  timeOfEvent(lastSeen, d.rankUpdated, sizeof(d.rankUpdated));
}

// ================================================================
// LEUCHTEN der Zählersymbole auf der Miner-Seite
// Bei neuem Wert 350 ms grün, dann 450 ms halb, dann wieder weiß.
// ================================================================
static uint32_t pulseAt[4];
static const uint32_t PULSE_FULL = 350, PULSE_END = 800;

static uint8_t pulseLevel(int i, uint32_t now) {
  if (!pulseAt[i]) return 0;
  uint32_t e = now - pulseAt[i];
  return e < PULSE_FULL ? 2 : e < PULSE_END ? 1 : 0;
}

// ================================================================
// RGB-LED auf der Rückseite (Low = an, daher 255 - Wert)
// ================================================================
void ledInit() {
  analogWrite(PIN_LED_R, 255);
  analogWrite(PIN_LED_G, 255);
  analogWrite(PIN_LED_B, 255);
}

void ledTick(uint32_t now) {
  static uint32_t tLed = 0;
  if (now - tLed < 40) return;
  tLed = now;
  uint8_t r = 0, g = 0, b = 0;
  if (cfg.ledMode == 1) {
    r = cfg.ledColor >> 16; g = cfg.ledColor >> 8; b = cfg.ledColor;
  } else if (cfg.ledMode == 2) {
    // Farbkreis in rund 14 Sekunden
    uint32_t h = (now / 40) % 360, x = (h % 60) * 255 / 60;
    switch (h / 60) {
      case 0: r = 255;     g = x;       break;
      case 1: r = 255 - x; g = 255;     break;
      case 2: g = 255;     b = x;       break;
      case 3: g = 255 - x; b = 255;     break;
      case 4: r = x;       b = 255;     break;
      default: r = 255;    b = 255 - x; break;
    }
  }
  uint32_t k = cfg.ledBright;   // 0–100
  analogWrite(PIN_LED_R, 255 - r * k / 100);
  analogWrite(PIN_LED_G, 255 - g * k / 100);
  analogWrite(PIN_LED_B, 255 - b * k / 100);
}

void displayWatch(uint32_t now) {
  static uint32_t lastJobs, lastShares, lastNet;
  static double lastBest = -1;
  static bool init = false;
  uint32_t net = job.valid ? (uint32_t)job.netDiff : 0;
  double best = miner.bestDiff + miner.bestDiffAllTime;
  if (!init) { lastJobs = miner.jobs; lastShares = miner.sharesOk; lastNet = net; lastBest = best; init = true; }
  bool hit = false;
  if (miner.jobs != lastJobs)       { lastJobs = miner.jobs;       pulseAt[PULSE_JOBS] = now | 1;   hit = true; }
  if (net != lastNet)               { lastNet = net;               pulseAt[PULSE_NET] = now | 1;    hit = true; }
  if (best != lastBest)             { lastBest = best;             pulseAt[PULSE_BEST] = now | 1;   hit = true; }
  if (miner.sharesOk != lastShares) { lastShares = miner.sharesOk; pulseAt[PULSE_SHARES] = now | 1; hit = true; }
  if (curPage == PAGE_PRICE && cfg.marketView == 2) {
    static uint32_t lastView;
    uint32_t v = (now / 10000) % 2;
    if (v != lastView) { lastView = v; tDisplay = 0; }
  }
  if (curPage != PAGE_MINER) return;
  // Zeichnen bei Beginn und bei jedem Stufenwechsel, nicht erst im 3-s-Takt
  static uint8_t shown[4];
  bool change = hit;
  for (int i = 0; i < 4; i++) {
    uint8_t lv = pulseLevel(i, now);
    if (lv != shown[i]) { shown[i] = lv; change = true; }
    if (!lv) pulseAt[i] = 0;
  }
  if (change) tDisplay = 0;
}

void tftUpdate() {
  // Auch gedimmt weiterzeichnen, sonst stünden Uhr und Werte still.
  if (colorBars) return;

  static bool ready = false;
  if (!ready) { tft.setSwapBytes(true); uiSetPush(uiPushToTft); ready = true; }

  // Kurse und Wetter schreiben Tasks auf Core 0. Ohne Mutex: letzte Kopie weiterverwenden.
  static PriceData   px;
  static WeatherData wx;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    px = prices;
    wx = weather;
    xSemaphoreGive(stateMutex);
  }

  static UiData d;
  d.page = (uint8_t)curPage;
  strlcpy(d.version, FW_VERSION, sizeof(d.version));

  d.timeValid = miner.ntpOk;
  strlcpy(d.hhmm, timeStr().c_str(), sizeof(d.hhmm));
  if (miner.ntpOk) {
    time_t ts = (time_t)(ntpCli.getEpochTime() + (long)(cfg.timezone * 3600));
    struct tm* ti = localtime(&ts);
    d.wday = ti->tm_wday; d.day = ti->tm_mday; d.month = ti->tm_mon + 1; d.year = ti->tm_year + 1900;
  }
  d.uptime = miner.uptime;

  d.wifi = (WiFi.status() == WL_CONNECTED);
  d.rssi = WiFi.RSSI();
  strlcpy(d.ip, WiFi.localIP().toString().c_str(), sizeof(d.ip));
  strlcpy(d.pool, miner.poolDisp, sizeof(d.pool));
  char* colon = strchr(d.pool, ':');   // nur der Host, wie in der Vorlage
  if (colon) *colon = 0;

  d.hashrate    = miner.hashrate;
  d.poolDiff    = miner.difficultyF;
  d.jobs        = miner.jobs;
  d.blockHits   = miner.blockHits;
  for (int i = 0; i < 4; i++) d.pulse[i] = pulseLevel(i, millis());
  d.netDiff     = job.valid ? job.netDiff : 0;
  d.bestSession = miner.bestDiff;
  d.bestAll     = miner.bestDiffAllTime;
  d.sharesOk    = miner.sharesOk;
  d.sharesTotal = miner.sharesOk + miner.sharesRej;

  d.btcUsd = px.btcUsd; d.btcChg = px.btcChg;
  d.coins[0].sym = "DGB"; d.coins[0].usd = px.dgbUsd; d.coins[0].chg = px.dgbChg;
  for (int i = 1; i < 6; i++) {
    bool has = i - 1 < watchCount;
    d.coins[i].sym = has ? watchlist[i-1].sym : "---";
    d.coins[i].usd = has ? px.altUsd[i-1] : 0;
    d.coins[i].chg = has ? px.altChg[i-1] : 0;
  }
  // 7-Tage-Verlauf: Kopie unter dem Mutex, die Zeiger zeigen in diese Kopie
  static SparkSet sp;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    sp = sparks;
    xSemaphoreGive(stateMutex);
  }
  for (int i = 0; i < 6; i++) {
    int k = i == 0 ? COIN_DGB : COIN_WATCH + i - 1;
    bool has = i == 0 || i - 1 < watchCount;
    d.coins[i].spark = has && sp.n[k] > 1 ? sp.pts[k] : nullptr;
    d.coins[i].sparkN = has ? sp.n[k] : 0;
  }
  // Kursseite: Kurse, Charts oder alle 10 Sekunden im Wechsel
  d.marketChart = cfg.marketView == 1 || (cfg.marketView == 2 && (millis() / 10000) % 2 == 1);

  bool isF = strcmp(cfg.weatherTempUnit, "F") == 0;
  d.wxValid = wx.valid;
  d.wxTemp = wx.tempC; d.wxFeels = wx.feelsLikeC; d.wxPressure = wx.pressureHpa; d.wxWind = wx.windKmh;
  d.wxHumidity = wx.humidity; d.wxCode = wx.weatherCode;
  d.wxPmValid = wx.pmValid; d.wxPm25 = wx.pm25;
  d.wxUnit = isF ? 'F' : 'C';
  strlcpy(d.wxSpeed, strcmp(cfg.weatherSpeedUnit, "mph") == 0 ? "mph" : "km/h", sizeof(d.wxSpeed));
  strlcpy(d.sunrise, wx.sunrise, sizeof(d.sunrise));
  strlcpy(d.sunset, wx.sunset, sizeof(d.sunset));
  for (int i = 0; i < 3; i++) {
    strlcpy(d.fc[i].date, wx.forecast[i].date, sizeof(d.fc[i].date));
    snprintf(d.fc[i].range, sizeof(d.fc[i].range), "%.0f - %.0f\xC2\xB0%c",
             wx.forecast[i].tempMin, wx.forecast[i].tempMax, d.wxUnit);
    d.fc[i].code = wx.forecast[i].weatherCode;
    d.fc[i].pmValid = wx.forecast[i].pmValid;
    d.fc[i].pm25 = wx.forecast[i].pm25;
    d.fc[i].pm10 = wx.forecast[i].pm10;
  }
  strlcpy(d.city, wx.cityName, sizeof(d.city));
  timeOfEvent(wx.fetchedAt, d.wxUpdated, sizeof(d.wxUpdated));

  fillRank(d);

  // Zeichendauer nur seriell, das Log-Ring-JSON ist schon knapp (E6)
  static int shownPage = -1;
  uint32_t t0 = micros();
  uiDraw(d);
  uint32_t us = micros() - t0;
  if ((int)d.page != shownPage || us >= 10000) {
    Serial.printf("[DISP] page=%d ms=%lu\n", (int)d.page, (unsigned long)(us / 1000));
    shownPage = d.page;
  }
}
