#pragma once
#include <stdint.h>
#include "ui_assets.h"

// ================================================================
// UI-KERN — ohne Arduino-Abhängigkeit, damit dieselben Seiten auch
// auf dem PC laufen (tools/design/host_preview.cpp).
// Ablauf: Hintergrund einmal beim Seitenwechsel, danach nur Felder,
// deren Inhalt sich geändert hat. Kein fillScreen, kein Flackern.
// ================================================================
typedef void (*UiPushFn)(int x, int y, int w, int h, const uint16_t* px);
void uiSetPush(UiPushFn fn);

enum UiPageId { UI_MINER=0, UI_CLOCK=1, UI_MARKETS=2, UI_WEATHER=3, UI_RANK=4 };

#define UI_MAX_RANK 13

struct UiRankRow {
  char   name[34];
  double best;
  bool   self;
};

struct UiForecast {
  char  date[6];
  char  range[20];   // "11 - 17°C"
  int   code;
  bool  pmValid;
  int   pm25;
  int   pm10;
};

struct UiCoin {
  const char* sym;
  double usd;        // <= 0: keine Daten
  double chg;
  const float* spark;  // 7-Tage-Verlauf, älteste zuerst; nullptr: keiner
  int    sparkN;
};

// Leuchtstufe der Zählersymbole: 0 weiß, 1 halb, 2 voll grün
enum UiPulseRow { PULSE_JOBS=0, PULSE_NET=1, PULSE_BEST=2, PULSE_SHARES=3 };

// Alle Werte, die die Seiten anzeigen. display.cpp füllt die Struktur.
struct UiData {
  uint8_t  page;
  char     version[16];
  // Zeit
  bool     timeValid;
  char     hhmm[6];
  int      wday, day, month, year;
  uint32_t uptime;
  // Netz
  bool     wifi;
  int      rssi;
  char     ip[16];
  char     pool[86];
  // Miner
  uint32_t hashrate;
  uint32_t totalHash;
  int      workers;
  double   poolDiff;
  uint32_t jobs;
  uint32_t blockHits;
  uint8_t  pulse[4];    // UiPulseRow
  double   netDiff;     // 0: noch kein Job
  double   bestSession;
  double   bestAll;
  uint32_t sharesOk;
  uint32_t sharesTotal;
  // Kurse
  double   btcUsd, btcChg;
  UiCoin   coins[6];
  bool     marketChart; // Kursseite: true = 7-Tage-Charts
  // Wetter
  bool     wxValid;
  float    wxTemp, wxFeels, wxPressure, wxWind;
  int      wxHumidity, wxCode;
  bool     wxPmValid;
  int      wxPm25;
  char     wxUnit, wxSpeed[6];
  char     sunrise[6], sunset[6];
  UiForecast fc[3];
  char     city[48];
  char     wxUpdated[6];
  // Rang
  int        rankCount;
  int        selfRank;
  UiRankRow  rank[UI_MAX_RANK];
  char       rankUpdated[6];
};

void   uiDraw(const UiData& d);
void   uiInvalidate();            // nach Fremdzeichnen: alles neu aufbauen
int    uiWeatherIconFor(int wmoCode);

// Nur für den PC-Test: Anzahl Felder, deren Text nicht in die Box passte.
extern int uiClipCount;
