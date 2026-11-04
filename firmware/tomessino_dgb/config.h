#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ================================================================
// FIRMWARE IDENTITÄT
// ================================================================
#define FW_NAME      "Tomessino-DGB"
#define FW_VERSION   "7.6.0"
#define FW_FULL      FW_NAME " v" FW_VERSION
#define COIN_NAME    "DigiByte"
#define COIN_TICK    "DGB"
#define COIN_COLOR   0x033F   // DGB Blau ~#0066CC in RGB565

// ================================================================
// HARDWARE PINS (CYD ESP32-2432S028R)
// ================================================================
#define PIN_BL       21
#define PIN_BTN      0        // Boot-Button = GPIO0 (Seite wechseln / Long-Press Config)
#define PIN_LED_R    4        // RGB-LED auf der Rückseite, Low = an
#define PIN_LED_G    16
#define PIN_LED_B    17
#define BTN_LONG_MS  3000     // 3s Long-Press → Config-Modus
#define BTN_RESET_MS 10000    // 10s Long-Press → Dashboard-Passwort löschen
#define BTN_TAP_MS   50       // Entprell

// XPT2046 Touch (VSPI - separater Bus vom Display!)
#define TOUCH_CS     33
#define TOUCH_CLK    25
#define TOUCH_MOSI   32
#define TOUCH_MISO   39
#define TOUCH_IRQ    36

// ================================================================
// WIFI / PORTAL
// ================================================================
#define AP_SSID      "DGB-Miner-Setup"
#define AP_PASS      ""
#define AP_TIMEOUT   180

// ================================================================
// MINING DEFAULTS
// ================================================================
// Pool 1 ist am Gerät geprüft (Shares akzeptiert, Diff 0,001 auf Wunsch).
// Pool 2 ist als Ausweichpool eingetragen, aber ungeprüft.
#define DEF_POOL1_HOST  "dgb-stratum.solominer.net"
#define DEF_POOL1_PORT  3333
#define DEF_POOL2_HOST  "eu.digi.hmpool.io"
#define DEF_POOL2_PORT  3337
#define DEF_WORKER      "dgb_worker1"
#define DEF_WORKER_PW   "x"
#define DEF_WALLET      ""
#define DEF_CPU_MHZ     240
#define DEF_BRIGHTNESS  200
#define DEF_SCREEN_TO   60    // Sekunden bis Dimmen

// ================================================================
// NTP
// ================================================================
#define NTP_HOST     "pool.ntp.org"
#define NTP_OFFSET   0        // Zeitzone kommt aus cfg.timezone (E7)
#define NTP_INTV     60000

// ================================================================
// CONFIG
// ================================================================
struct Config {
  char     wallet[130]    = DEF_WALLET;
  char     pool1Host[66]  = DEF_POOL1_HOST;
  uint16_t pool1Port      = DEF_POOL1_PORT;
  char     pool2Host[66]  = DEF_POOL2_HOST;
  uint16_t pool2Port      = DEF_POOL2_PORT;
  char     worker[34]     = DEF_WORKER;
  char     workerPw[34]   = DEF_WORKER_PW;
  uint16_t cpuMhz         = DEF_CPU_MHZ;
  uint8_t  brightness     = DEF_BRIGHTNESS;
  uint16_t screenTimeout  = DEF_SCREEN_TO;
  float    timezone       = 1.0f;   // UTC+1 Default (DE)
  bool     saveStatus     = true;   // Best-Diff & Block-Count persistent speichern
  // Wetter (Open-Meteo)
  float    weatherLat     = 52.5200f;   // Default: Berlin, Standort kommt normal aus der IP
  float    weatherLon     = 13.4050f;
  char     weatherCity[48]= "Berlin, Germany";
  bool     weatherAutoLoc = true;       // true: per IP-Geolocation ermitteln
  char     weatherTempUnit[4]  = "C";   // "C" oder "F"
  char     weatherSpeedUnit[6] = "kmh"; // "kmh" oder "mph"
  uint16_t weatherIntervalMin  = 15;    // Update-Intervall in Minuten
  char     watchlist[120] = "cardano:ADA,dogecoin:DOGE,ecash:XEC,shiba-inu:SHIB,pepe:PEPE";
  char     webPass[33]    = "";         // leer: Dashboard ohne Passwort
  bool     weatherGeo     = false;      // Ortsname geändert: Koordinaten beim nächsten Abruf suchen
  uint8_t  ledMode        = 2;          // RGB-LED hinten: 0 aus, 1 feste Farbe, 2 Farbwechsel
  uint32_t ledColor       = 0x0066CC;   // feste Farbe, 0xRRGGBB
  uint8_t  ledBright      = 30;         // 0–100 %
  uint8_t  marketView     = 2;          // Kursseite: 0 Kurse, 1 Charts, 2 abwechselnd
};

// Koordinaten außerhalb von Breite ±90 / Länge ±180 liefern bei Open-Meteo einen Fehler
inline bool validLatLon(float lat, float lon) {
  return lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f && !(lat == 0.0f && lon == 0.0f);
}
extern Config cfg;

// ================================================================
// MUTEX — schützt logMsg()/prices/weather vor gleichzeitigem Zugriff
// von priceTaskFn/weatherTaskFn (Core 0) und dem Hauptloop (Core 1).
// String-Objekte und structs sind nicht thread-safe in Arduino.
// ================================================================
extern SemaphoreHandle_t stateMutex;

// ================================================================
// LOG RING
// ================================================================
#define LOG_CAP 60
extern String logRing[LOG_CAP];
extern int    logHead;

void logMsg(const char* lvl, const char* msg);
void loadConfig();
void saveConfig();
void saveBestDiff();
