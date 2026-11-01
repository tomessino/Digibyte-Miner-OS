#include "net_parse.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

int parseWatchlist(const char* s, WatchEntry* out, int maxN) {
  int n = 0;
  while (*s) {
    if (n >= maxN) return -1;
    const char* colon = strchr(s, ':');
    if (!colon) return -1;
    const char* end = strchr(colon, ',');
    if (!end) end = colon + strlen(colon);
    size_t idLen = (size_t)(colon - s), symLen = (size_t)(end - colon - 1);
    if (idLen < 1 || idLen >= sizeof(out[n].id) || symLen < 1 || symLen >= sizeof(out[n].sym)) return -1;
    for (size_t i = 0; i < idLen; i++) {
      char c = s[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return -1;
      out[n].id[i] = c;
    }
    out[n].id[idLen] = 0;
    for (size_t i = 0; i < symLen; i++) {
      char c = colon[1 + i];
      if (c >= 'a' && c <= 'z') c = (char)(c - 32);
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return -1;
      out[n].sym[i] = c;
    }
    out[n].sym[symLen] = 0;
    n++;
    if (!*end) break;
    s = end + 1;
    if (!*s) return -1;   // Komma am Ende
  }
  return n;
}

size_t buildPriceIds(char* out, size_t n, const WatchEntry* w, int count) {
  size_t len = strlen(COIN_FIXED_IDS);
  if (len + 1 > n) return 0;
  memcpy(out, COIN_FIXED_IDS, len + 1);
  for (int i = 0; i < count; i++) {
    size_t l = strlen(w[i].id);
    if (len + 1 + l + 1 > n) return 0;
    out[len++] = ',';
    memcpy(out + len, w[i].id, l + 1);
    len += l;
  }
  return len;
}

int parsePrices(const char* json, const WatchEntry* w, int count, CoinQuote out[COIN_COUNT]) {
  memset(out, 0, sizeof(CoinQuote) * COIN_COUNT);
  JsonDocument doc;
  if (deserializeJson(doc, json)) return -1;
  int found = 0;
  for (int i = 0; i < 2 + count && i < COIN_COUNT; i++) {
    const char* id = i == COIN_DGB ? "digibyte" : i == COIN_BTC ? "bitcoin" : w[i - COIN_WATCH].id;
    JsonVariant usd = doc[id]["usd"];
    if (usd.isNull()) continue;
    out[i].ok  = true;
    out[i].usd = usd.as<float>();
    out[i].chg = doc[id]["usd_24h_change"] | 0.0f;
    found++;
  }
  return found;
}

// Mittelwert der Stunden [from, from+24), null-Werte werden übersprungen.
static bool dayMean(JsonArray a, int from, int& out) {
  double sum = 0;
  int n = 0;
  for (int i = from; i < from + 24 && i < (int)a.size(); i++) {
    if (a[i].isNull()) continue;
    sum += a[i].as<double>();
    n++;
  }
  if (!n) return false;
  out = (int)floor(sum / n + 0.5);
  return true;
}

bool parseAirQuality(const char* json, AirData& out) {
  memset(&out, 0, sizeof(out));
  // Die 96 Zeitstempel braucht niemand, das spart über 1 KB im Dokument.
  JsonDocument filter;
  filter["current"]["pm2_5"] = true;
  filter["current"]["pm10"] = true;
  filter["hourly"]["pm2_5"] = true;
  filter["hourly"]["pm10"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;

  JsonObject cur = doc["current"];
  if (!cur["pm2_5"].isNull() && !cur["pm10"].isNull()) {
    out.nowOk = true;
    out.pm25Now = (int)floor(cur["pm2_5"].as<double>() + 0.5);
    out.pm10Now = (int)floor(cur["pm10"].as<double>() + 0.5);
  }
  JsonArray p25 = doc["hourly"]["pm2_5"];
  JsonArray p10 = doc["hourly"]["pm10"];
  bool any = out.nowOk;
  for (int d = 0; d < 3; d++) {
    int from = 24 * (d + 1);
    out.day[d].ok = dayMean(p25, from, out.day[d].pm25) && dayMean(p10, from, out.day[d].pm10);
    any = any || out.day[d].ok;
  }
  return any;
}
