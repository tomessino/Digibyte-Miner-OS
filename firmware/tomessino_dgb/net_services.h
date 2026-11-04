#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "net_parse.h"

// ================================================================
// SWARM PEERS — echte lokale Bestenliste über UDP-Broadcast/Listen
// Jedes Tomessino-Gerät im selben LAN sendet alle 5s seinen Status
// auf Port 55123 und hört gleichzeitig auf Pakete anderer Geräte.
// ================================================================
#define MAX_SWARM_PEERS 12
#define PEER_TIMEOUT_MS 30000   // Peer gilt nach 30s Funkstille als offline

struct SwarmPeer {
  bool     active        = false;
  char     ip[16]        = "";
  char     workerName[34]= "";
  uint32_t hashrate      = 0;
  uint32_t sharesOk      = 0;
  uint32_t sharesRej     = 0;
  double   bestDiffAllTime = 0;
  uint32_t blockHits     = 0;
  int32_t  jobs          = -1;    // -1: Peer sendet keine Jobs (vor 7.6.0)
  uint32_t uptime        = 0;
  float    tempC         = 0;
  uint32_t lastSeen      = 0;     // millis() beim letzten Empfang
};
extern SwarmPeer swarmPeers[MAX_SWARM_PEERS];
extern int        swarmPeerCount;

// ================================================================
// PREIS-DATEN
// ================================================================
struct PriceData {
  float dgbUsd   = 0.0f;
  float dgbChg   = 0.0f;
  float btcUsd   = 0.0f;
  float btcChg   = 0.0f;
  float altUsd[WATCH_MAX] = {};  // Watchlist (0 = noch kein Wert)
  float altChg[WATCH_MAX] = {};
  uint32_t fetchedAt = 0;
};
extern PriceData prices;
extern WatchEntry watchlist[WATCH_MAX];
extern SparkSet   sparks;        // 7-Tage-Verlauf, unter stateMutex
extern uint32_t   sparksAt;      // millis() des letzten Abrufs, 0 = noch keiner
extern int        watchCount;
void loadWatchlist();   // aus cfg.watchlist, nach loadConfig()
void weatherInit();     // nach loadConfig(): Stadtname bei festem Standort sofort anzeigen

// ================================================================
// WETTER-DATEN (Open-Meteo, kostenlos, kein API-Key nötig)
// ================================================================
struct ForecastDay {
  char  date[6]       = "--";    // "MM-DD"
  float tempMin       = 0;
  float tempMax       = 0;
  int   weatherCode   = 0;
  bool  pmValid       = false;   // Open-Meteo Air Quality, Tagesmittel
  int   pm25          = 0;
  int   pm10          = 0;
};

struct WeatherData {
  bool      valid        = false;
  float     tempC        = 0.0f;
  float     feelsLikeC   = 0.0f;
  int       humidity     = 0;
  float     pressureHpa  = 0.0f;
  float     windKmh      = 0.0f;
  int       weatherCode  = 0;
  char      sunrise[6]   = "--:--";
  char      sunset[6]    = "--:--";
  bool      pmValid      = false;
  int       pm25         = 0;
  int       pm10         = 0;
  ForecastDay forecast[3];
  char      cityName[48] = "Standort wird ermittelt...";
  float     lat          = 0.0f;
  float     lon          = 0.0f;
  bool      locationSet  = false;
  uint32_t  fetchedAt    = 0;
};
extern WeatherData weather;

extern WiFiUDP         swarmUDP;
extern NTPClient       ntpCli;

String timeStr();
String dateStr();
void fetchPrices();
void fetchWeather();
const char* weatherCodeDesc(int code);
void swarmBroadcast();
void swarmListen();
void swarmPrune();
