#pragma once
#include <stdbool.h>

// ================================================================
// AUSWERTUNG DER API-ANTWORTEN — ohne Arduino-Abhängigkeit, damit sie
// auf dem PC gegen echte Antworten läuft (tools/design/testdata/).
// ================================================================
#include <stddef.h>

// DGB und BTC sind fest, dazu bis zu 5 frei wählbare Coins (Plan 4.1)
#define WATCH_MAX     5
#define COIN_COUNT    (2 + WATCH_MAX)
#define COIN_FIXED_IDS "digibyte,bitcoin"
#define WATCH_DEFAULT "cardano:ADA,dogecoin:DOGE,ecash:XEC,shiba-inu:SHIB,pepe:PEPE"
enum CoinIdx { COIN_DGB=0, COIN_BTC=1, COIN_WATCH=2 };

struct WatchEntry {
  char id[32];    // CoinGecko-ID, klein, z. B. shiba-inu
  char sym[6];    // Anzeige, groß, höchstens 5 Zeichen wegen der Feldbreite
};

// Format "id:SYM,id:SYM". Rückgabe: Anzahl, -1 wenn etwas ungültig ist.
int parseWatchlist(const char* s, WatchEntry* out, int maxN);
// Schreibt "digibyte,bitcoin,id,id". Rückgabe: Länge, 0 wenn zu lang.
size_t buildPriceIds(char* out, size_t n, const WatchEntry* w, int count);

struct CoinQuote {
  bool  ok;
  float usd;
  float chg;
};

// out: [0] DGB, [1] BTC, ab [2] die Watchlist. Rückgabe: Anzahl gefundener Coins, -1 bei ungültigem JSON.
int parsePrices(const char* json, const WatchEntry* w, int count, CoinQuote out[COIN_COUNT]);

struct AirDay {
  bool ok;
  int  pm25;
  int  pm10;
};

struct AirData {
  bool   nowOk;
  int    pm25Now;
  int    pm10Now;
  AirDay day[3];   // morgen bis in drei Tagen, wie weather.forecast
};

// Tageswerte sind der gerundete Mittelwert der vorhandenen Stunden.
// Erwartet hourly ab heute 00:00 Ortszeit (timezone=auto).
bool parseAirQuality(const char* json, AirData& out);
