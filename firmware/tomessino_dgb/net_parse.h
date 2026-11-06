#pragma once
#include <stdbool.h>

// ================================================================
// AUSWERTUNG DER API-ANTWORTEN — ohne Arduino-Abhängigkeit, damit sie
// auf dem PC gegen echte Antworten läuft (tools/design/testdata/).
// ================================================================
#include <stddef.h>
#include <stdint.h>

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

// ================================================================
// WORKER-NAME
// Pools trennen Miner über "Wallet.Name". Steht so ein Name schon in der Wallet,
// gilt er, sonst der Name aus den Einstellungen, leer: def.
// ================================================================
#define WORKER_MAX 32
// 1 bis WORKER_MAX Zeichen aus A-Z, a-z, 0-9, _ und -
bool validWorkerName(const char* s);
// Andere Zeichen werden "_": Der Name geht als JSON-Text an die anderen Miner im LAN.
void workerNameFor(const char* wallet, const char* worker, const char* def, char* out, size_t n);
// Benutzer für den Pool. Eine Wallet mit Namen bleibt, wie sie eingegeben ist.
void poolUserFor(const char* wallet, const char* name, char* out, size_t n);

// ================================================================
// 7-TAGE-VERLAUF (CoinGecko /coins/markets?sparkline=true)
// Die Antwort hat rund 30 KB. Der Parser liest sie Zeichen für Zeichen,
// ohne sie zu puffern, und behält je Coin SPARK_N gemittelte Punkte.
// ================================================================
#define SPARK_N   48
#define SPARK_RAW 200

struct SparkSet {
  float   pts[COIN_COUNT][SPARK_N];
  uint8_t n[COIN_COUNT];   // 0: kein Verlauf
};

class SparkParser {
 public:
  SparkParser(const WatchEntry* w, int count, SparkSet& out);
  void feed(const char* s, size_t len);
  int  found() const { return found_; }
 private:
  uint16_t slotsOf(const char* id) const;   // Bitmaske: eine ID kann mehrfach in der Liste stehen
  void endArray();
  const WatchEntry* w_;
  int count_;
  SparkSet& out_;
  char tail_[10];
  int  mode_;             // 0 suchen, 1 ID lesen, 2 Zahlen lesen
  char tok_[40];
  int  tokLen_;
  uint16_t cur_;
  float raw_[SPARK_RAW];
  int  rawN_;
  int  found_;
};

