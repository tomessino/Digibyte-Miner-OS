#include "net_services.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "config.h"
#include "mining.h"
#include "net_parse.h"
#include <new>
#include <Preferences.h>


WiFiUDP         ntpUDP;
NTPClient       ntpCli(ntpUDP, NTP_HOST, NTP_OFFSET, NTP_INTV);
WiFiUDP         swarmUDP;

PriceData prices;
WatchEntry watchlist[WATCH_MAX];
SparkSet   sparks;
uint32_t   sparksAt = 0;
int        watchCount = 0;
WeatherData weather;
SwarmPeer swarmPeers[MAX_SWARM_PEERS];
int        swarmPeerCount = 0;

// ================================================================
// ZEIT / DATUM (NTP-basiert, DE-Format)
// ================================================================
String timeStr() {
  if (!miner.ntpOk) return "--:--:--";
  time_t t = (time_t)(ntpCli.getEpochTime() + (long)(cfg.timezone*3600));
  struct tm* ti = localtime(&t);
  char b[10]; snprintf(b,sizeof(b),"%02d:%02d:%02d",ti->tm_hour,ti->tm_min,ti->tm_sec);
  return String(b);
}

String dateStr() {
  if (!miner.ntpOk) return "--.--.----";
  time_t t = (time_t)(ntpCli.getEpochTime() + (long)(cfg.timezone*3600));
  struct tm* ti = localtime(&t);
  char b[14]; snprintf(b,sizeof(b),"%02d.%02d.%04d",ti->tm_mday,ti->tm_mon+1,ti->tm_year+1900);
  return String(b);
}

// ================================================================
// PREIS-ABRUF — HTTPS non-blocking (CoinGecko Port 443)
// ================================================================
static bool priceTaskRunning=false;
alignas(SparkParser) static uint8_t parserMem[sizeof(SparkParser)];   // statt Task-Stack, der Parser hat ~1 KB

void loadWatchlist() {
  int n = parseWatchlist(cfg.watchlist, watchlist, WATCH_MAX);
  if (n < 0) {
    logMsg("PRICE","Watchlist ungültig, nutze Standard");
    n = parseWatchlist(WATCH_DEFAULT, watchlist, WATCH_MAX);
  }
  watchCount = n;
}

// ================================================================
// HTTP-ABRUF — HTTP/1.0, damit die Antwort ohne Chunked-Encoding kommt.
// Wartet nie blockierend: readStringUntil() lief bis zu 8 s ohne die CPU
// abzugeben, auf Kern 0 lief dann der Task-Watchdog ab (Neustartschleife
// bei ungewohnten Antworten, z. B. unpassenden Koordinaten).
// ================================================================
struct HttpSink { virtual bool write(const char* d, size_t n) = 0; virtual ~HttpSink() {} };

struct StringSink : HttpSink {
  String& s; size_t max;
  StringSink(String& str, size_t m) : s(str), max(m) {}
  bool write(const char* d, size_t n) override {
    if (s.length() + n > max) return false;
    s.concat(d, n);
    return true;
  }
};

struct SparkSink : HttpSink {
  SparkParser& p;
  explicit SparkSink(SparkParser& parser) : p(parser) {}
  bool write(const char* d, size_t n) override { p.feed(d, n); return true; }
};

static int readHttp(Client& client, const char* host, const char* path, HttpSink& sink, size_t* bytes = nullptr) {
  client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Tomessino-DGB/" FW_VERSION "\r\nConnection: close\r\n\r\n", path, host);
  int status = 0; bool inBody = false; char line[160]; size_t ll = 0; char buf[512];
  long contentLen = -1; size_t total = 0; int idle = 0;
  uint32_t t = millis();
  while (millis() - t < 12000) {
    if (inBody && contentLen >= 0 && (long)total >= contentLen) break;
    int avail = client.available();
    if (avail <= 0) {
      // Nach dem Verbindungsende noch kurz nachsehen, ob TLS noch Daten gepuffert hat
      if (!client.connected() && ++idle > 25) break;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }
    idle = 0;
    if (!inBody) {
      int c = client.read();
      if (c < 0) { vTaskDelay(1); continue; }
      if (c == '\n') {
        line[ll] = 0;
        if (!status && !strncmp(line, "HTTP/", 5) && ll >= 12) status = atoi(line + 9);
        if (!strncasecmp(line, "Content-Length:", 15)) contentLen = atol(line + 15);
        if (ll == 0 || (ll == 1 && line[0] == '\r')) inBody = true;
        ll = 0;
      } else if (ll < sizeof(line) - 1) line[ll++] = (char)c;
      continue;
    }
    int n = client.read((uint8_t*)buf, sizeof(buf));
    if (n <= 0) { vTaskDelay(1); continue; }
    total += n;
    if (!sink.write(buf, n)) { client.stop(); return -2; }
  }
  client.stop();
  if (bytes) *bytes = total;
  if (contentLen >= 0 && (long)total < contentLen) return -3;   // unvollständig
  return status;
}

static int httpsGet(const char* host, const char* path, String& body, size_t maxLen) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);
  if (!client.connect(host, 443)) return -1;
  StringSink sink(body, maxLen);
  return readHttp(client, host, path, sink);
}

static int httpGetPlain(const char* host, const char* path, String& body, size_t maxLen) {
  WiFiClient client;
  client.setTimeout(6000);
  if (!client.connect(host, 80)) return -1;
  StringSink sink(body, maxLen);
  return readHttp(client, host, path, sink);
}

// Speicherstand nach TLS-Abrufen, für den Gerätetest
static void logTaskMem(const char* name) {
  Serial.printf("[MEM] %s heap=%u minheap=%u stackfrei=%u\n", name,
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

// Eigene Funktion: vTaskDelete kehrt nicht zurück, lokale Objekte im Task-Körper
// würden nie aufgeräumt. Am Gerät kostete das rund 6 KB Heap je Wetter-Abruf.
static void priceFetch() {
  String body;
  char ids[220], path[320];
  if (!buildPriceIds(ids,sizeof(ids),watchlist,watchCount)) { logMsg("PRICE","Watchlist zu lang"); return; }
  snprintf(path,sizeof(path),"/api/v3/simple/price?ids=%s&vs_currencies=usd&include_24hr_change=true",ids);
  int st=httpsGet("api.coingecko.com",path,body,4096);
  if (st==200) {
    CoinQuote q[COIN_COUNT];
    int found=parsePrices(body.c_str(),watchlist,watchCount,q);
    if (found>=0) {
      // Fehlt ein Coin in der Antwort, bleibt sein letzter Wert stehen.
      if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (q[COIN_DGB].ok) { prices.dgbUsd=q[COIN_DGB].usd; prices.dgbChg=q[COIN_DGB].chg; }
        if (q[COIN_BTC].ok) { prices.btcUsd=q[COIN_BTC].usd; prices.btcChg=q[COIN_BTC].chg; }
        for (int i=0;i<watchCount;i++) {
          const CoinQuote& c=q[COIN_WATCH+i];
          if (c.ok) { prices.altUsd[i]=c.usd; prices.altChg[i]=c.chg; }
        }
        if (found>0) prices.fetchedAt=millis();
        xSemaphoreGive(stateMutex);
      }
      logMsg("PRICE",(String("DGB=$")+prices.dgbUsd+" BTC=$"+(int)prices.btcUsd+" Coins "+found+"/"+(2+watchCount)).c_str());
    } else logMsg("PRICE","JSON parse err");
  } else if (st==-1) logMsg("PRICE","HTTPS fehlgeschlagen");
  else logMsg("PRICE",(String("HTTP-Fehler ")+st).c_str());

  // 7-Tage-Verlauf: alle 30 Minuten, nach einem Fehler frühestens nach 10 Minuten.
  // Die Antwort hat rund 30 KB und wird nur durchgereicht, nicht gepuffert.
  static uint32_t lastTry = 0;
  if (lastTry && millis() - lastTry < (sparksAt ? 1800000UL : 600000UL)) return;
  lastTry = millis();
  body = String();
  snprintf(path, sizeof(path), "/api/v3/coins/markets?vs_currency=usd&ids=%s&sparkline=true", ids);
  static SparkSet fresh;
  SparkParser* parser = new (parserMem) SparkParser(watchlist, watchCount, fresh);
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);
  if (!client.connect("api.coingecko.com", 443)) { logMsg("PRICE", "Chart-Abruf: HTTPS fehlgeschlagen"); return; }
  SparkSink sink(*parser);
  size_t got = 0;
  int cs = readHttp(client, "api.coingecko.com", path, sink, &got);
  if (cs == 200 && parser->found() > 0) {
    if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      sparks = fresh;
      sparksAt = millis();
      xSemaphoreGive(stateMutex);
    }
    logMsg("PRICE", (String("Charts ") + parser->found() + "/" + (2 + watchCount) + " (" + got + " B)").c_str());
  } else {
    logMsg("PRICE", (String("Chart-Abruf fehlgeschlagen: ") + cs + " (" + got + " B)").c_str());
  }
}

void priceTaskFn(void* param) {
  priceFetch();
  logTaskMem("price");
  priceTaskRunning=false; vTaskDelete(NULL);
}

void fetchPrices() {
  if (WiFi.status()!=WL_CONNECTED||priceTaskRunning) return;
  priceTaskRunning=true;
  logMsg("PRICE","Abruf...");
  xTaskCreatePinnedToCore(priceTaskFn,"price",12288,NULL,1,NULL,0);
}

// ================================================================
void swarmBroadcast() {
  if (WiFi.status()!=WL_CONNECTED) return;
  char buf[420];
  snprintf(buf,sizeof(buf),
    "{\"fw\":\"%s\",\"coin\":\"DGB\",\"worker\":\"%s\",\"hr\":%lu,\"ok\":%lu,\"rej\":%lu,"
    "\"diff\":%.6f,\"bestDiffAT\":%.4f,\"blocks\":%lu,\"jobs\":%lu,"
    "\"pool\":\"%s\",\"ip\":\"%s\",\"up\":%lu,\"temp\":%.1f}",
    FW_FULL, effWorker, miner.hashrate, miner.sharesOk, miner.sharesRej,
    miner.difficultyF, miner.bestDiffAllTime, miner.blockHits, (unsigned long)miner.jobs,
    miner.poolDisp,
    WiFi.localIP().toString().c_str(), miner.uptime, miner.tempC);
  swarmUDP.beginPacket("255.255.255.255",55123);
  swarmUDP.print(buf);
  swarmUDP.endPacket();
}

// Hört auf UDP-Pakete anderer Tomessino-Geräte im LAN und pflegt
// die lokale swarmPeers-Liste (kein blocking, parst nur was da ist).
void swarmListen() {
  int packetSize = swarmUDP.parsePacket();
  if (packetSize <= 0) return;

  char buf[440];
  int len = swarmUDP.read(buf, sizeof(buf)-1);
  if (len <= 0) return;
  buf[len] = '\0';

  IPAddress remoteIp = swarmUDP.remoteIP();
  String remoteIpStr = remoteIp.toString();

  // Eigene Broadcasts ignorieren (gleiche IP wie wir selbst)
  if (WiFi.status()==WL_CONNECTED && remoteIpStr == WiFi.localIP().toString()) return;

  JsonDocument doc;
  if (deserializeJson(doc, buf)) return;  // kein gültiges JSON, verwerfen

  const char* coin = doc["coin"] | "";
  if (strcmp(coin, "DGB") != 0) return;   // nur DGB-Tomessino-Geräte zählen

  // Vorhandenen Peer per IP finden, sonst neuen Slot suchen
  int idx = -1;
  for (int i = 0; i < swarmPeerCount; i++) {
    if (strcmp(swarmPeers[i].ip, remoteIpStr.c_str()) == 0) { idx = i; break; }
  }
  if (idx < 0) {
    if (swarmPeerCount < MAX_SWARM_PEERS) { idx = swarmPeerCount++; }
    else {
      // Liste voll: ältesten (am längsten nicht gesehenen) Peer ersetzen
      uint32_t oldest = 0xFFFFFFFF; int oldestIdx = 0;
      for (int i = 0; i < MAX_SWARM_PEERS; i++) {
        if (swarmPeers[i].lastSeen < oldest) { oldest = swarmPeers[i].lastSeen; oldestIdx = i; }
      }
      idx = oldestIdx;
    }
  }

  SwarmPeer& p = swarmPeers[idx];
  p.active = true;
  strlcpy(p.ip, remoteIpStr.c_str(), sizeof(p.ip));
  strlcpy(p.workerName, doc["worker"] | "?", sizeof(p.workerName));
  p.hashrate         = doc["hr"]        | 0;
  p.sharesOk         = doc["ok"]        | 0;
  p.sharesRej        = doc["rej"]       | 0;
  p.bestDiffAllTime  = doc["bestDiffAT"]| 0.0;
  p.blockHits        = doc["blocks"]    | 0;
  p.jobs             = doc["jobs"]      | -1L;   // ältere Firmware sendet keine Jobs
  p.uptime           = doc["up"]        | 0;
  p.tempC            = doc["temp"]      | 0.0f;
  p.lastSeen         = millis();
}

// Markiert Peers als inaktiv, die länger als PEER_TIMEOUT_MS nicht gesendet haben
void swarmPrune() {
  uint32_t now = millis();
  for (int i = 0; i < swarmPeerCount; i++) {
    if (swarmPeers[i].active && (now - swarmPeers[i].lastSeen > PEER_TIMEOUT_MS)) {
      swarmPeers[i].active = false;
    }
  }
}

// ================================================================
// WETTER-ABRUF (Open-Meteo, kostenlos, kein API-Key)
// Zwei Schritte, beide non-blocking als FreeRTOS-Task auf Core 0:
//  1. Falls weatherAutoLoc: IP-Geolocation via ip-api.com (HTTP, kein TLS nötig)
//  2. Open-Meteo Forecast-API mit lat/lon (HTTPS)
// ================================================================
static bool weatherTaskRunning = false;

// Open-Meteo "weathercode" → grobe Klartext-Beschreibung (nur fürs Log)
const char* weatherCodeDesc(int code) {
  if (code == 0) return "Klar";
  if (code <= 3) return "Bewoelkt";
  if (code <= 48) return "Nebel";
  if (code <= 67) return "Regen";
  if (code <= 77) return "Schnee";
  if (code <= 82) return "Schauer";
  if (code <= 99) return "Gewitter";
  return "?";
}

static void weatherFetch() {
  // ── Schritt 1: Standort per IP ermitteln (nur wenn AutoLoc aktiv) ──
  if (cfg.weatherAutoLoc) {
    String body;
    int st = httpGetPlain("ip-api.com", "/json/?fields=lat,lon,city,country", body, 1024);
    if (st == 200) {
      JsonDocument geoDoc;
      if (!deserializeJson(geoDoc, body)) {
        float glat = geoDoc["lat"] | 0.0f;
        float glon = geoDoc["lon"] | 0.0f;
        const char* gcity = geoDoc["city"] | "";
        const char* gcountry = geoDoc["country"] | "";
        if (glat != 0.0f || glon != 0.0f) {
          if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            cfg.weatherLat = glat;
            cfg.weatherLon = glon;
            snprintf(cfg.weatherCity, sizeof(cfg.weatherCity), "%s, %s", gcity, gcountry);
            xSemaphoreGive(stateMutex);
          }
          logMsg("WEATHER", (String("Standort: ") + cfg.weatherCity).c_str());
        }
      }
    } else {
      logMsg("WEATHER", (String("Standort-Abfrage fehlgeschlagen: ") + st).c_str());
    }
  } else if (cfg.weatherGeo) {
    // Fester Ort, nur der Name wurde geändert: Koordinaten bei Open-Meteo suchen
    char path[200];
    int pl = snprintf(path, sizeof(path), "/v1/search?count=1&language=de&format=json&name=");
    for (const char* c = cfg.weatherCity; *c && *c != ',' && pl < (int)sizeof(path) - 4; c++) {
      unsigned char u = (unsigned char)*c;
      if (isalnum(u) || u == '-' || u == '.') path[pl++] = u;
      else pl += snprintf(path + pl, sizeof(path) - pl, "%%%02X", u);
    }
    path[pl] = 0;
    String body;
    int st = httpsGet("geocoding-api.open-meteo.com", path, body, 4096);
    JsonDocument geoDoc;
    if (st == 200 && !deserializeJson(geoDoc, body)) {
      JsonObject r0 = geoDoc["results"][0];
      float glat = r0["latitude"] | 999.0f, glon = r0["longitude"] | 999.0f;
      if (validLatLon(glat, glon)) {
        cfg.weatherLat = glat;
        cfg.weatherLon = glon;
        logMsg("WEATHER", (String("Ort gefunden: ") + (const char*)(r0["name"] | "") + " " + String(glat, 2) + "/" + String(glon, 2)).c_str());
      } else {
        logMsg("WEATHER", "Ort nicht gefunden, Koordinaten bleiben");
      }
      cfg.weatherGeo = false;               // nicht bei jedem Abruf erneut suchen
      Preferences p;
      p.begin("tomDGB2", false);
      p.putFloat("wLat", cfg.weatherLat);
      p.putFloat("wLon", cfg.weatherLon);
      p.putBool("wGeo", false);
      p.end();
    } else {
      logMsg("WEATHER", (String("Ortssuche fehlgeschlagen: ") + st).c_str());
    }
  }

  strlcpy(weather.cityName, cfg.weatherCity, sizeof(weather.cityName));
  weather.lat = cfg.weatherLat;
  weather.lon = cfg.weatherLon;

  // ── Schritt 2: Open-Meteo Forecast abrufen ──────────────────────
  {
    char path[320];
    snprintf(path, sizeof(path),
      "/v1/forecast?latitude=%.4f&longitude=%.4f"
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "surface_pressure,wind_speed_10m,weather_code"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
      "&temperature_unit=%s&wind_speed_unit=%s"
      "&timezone=auto&forecast_days=4",
      cfg.weatherLat, cfg.weatherLon,
      strcmp(cfg.weatherTempUnit,"F")==0 ? "fahrenheit" : "celsius",
      strcmp(cfg.weatherSpeedUnit,"mph")==0 ? "mph" : "kmh");
    String body;
    int st = httpsGet("api.open-meteo.com", path, body, 8192);
    JsonDocument doc;
    if (st != 200) {
      logMsg("WEATHER", (String("Abruf fehlgeschlagen: ") + st).c_str());
    } else if (deserializeJson(doc, body)) {
      logMsg("WEATHER", "JSON-Parse fehlgeschlagen");
    } else {
        if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        JsonObject cur = doc["current"];
        weather.tempC       = cur["temperature_2m"]          | 0.0f;
        weather.feelsLikeC  = cur["apparent_temperature"]    | 0.0f;
        weather.humidity    = cur["relative_humidity_2m"]    | 0;
        weather.pressureHpa = cur["surface_pressure"]        | 0.0f;
        weather.windKmh     = cur["wind_speed_10m"]          | 0.0f;
        weather.weatherCode = cur["weather_code"]            | 0;

        JsonObject daily = doc["daily"];
        JsonArray sunriseArr = daily["sunrise"];
        JsonArray sunsetArr  = daily["sunset"];
        if (sunriseArr.size() > 0) {
          const char* sr = sunriseArr[0] | "";  // "2026-06-19T04:51"
          const char* p = strchr(sr, 'T');
          if (p) strlcpy(weather.sunrise, p+1, sizeof(weather.sunrise));
        }
        if (sunsetArr.size() > 0) {
          const char* ss = sunsetArr[0] | "";
          const char* p = strchr(ss, 'T');
          if (p) strlcpy(weather.sunset, p+1, sizeof(weather.sunset));
        }

        JsonArray dCodes = daily["weather_code"];
        JsonArray dMax   = daily["temperature_2m_max"];
        JsonArray dMin   = daily["temperature_2m_min"];
        JsonArray dDates = daily["time"];

        // Tag 1-3 (Index 1,2,3 — Index 0 ist heute)
        for (int i = 0; i < 3; i++) {
          int srcIdx = i + 1;
          if (srcIdx < (int)dCodes.size()) {
            weather.forecast[i].weatherCode = dCodes[srcIdx] | 0;
            weather.forecast[i].tempMax     = dMax[srcIdx]   | 0.0f;
            weather.forecast[i].tempMin     = dMin[srcIdx]   | 0.0f;
            const char* dstr = dDates[srcIdx] | "";  // "2026-06-20"
            if (strlen(dstr) >= 10) {
              snprintf(weather.forecast[i].date, sizeof(weather.forecast[i].date),
                       "%c%c-%c%c", dstr[5],dstr[6],dstr[8],dstr[9]);
            }
          }
        }

        weather.valid     = true;
        weather.fetchedAt = millis();
        xSemaphoreGive(stateMutex);
      }
      logMsg("WEATHER", (String(weatherCodeDesc(weather.weatherCode))
             + " " + String(weather.tempC,1) + "C").c_str());
    }
  }

  // ── Schritt 3: Luftqualität (Open-Meteo Air Quality) ────────────
  char airPath[200];
  snprintf(airPath, sizeof(airPath),
    "/v1/air-quality?latitude=%.4f&longitude=%.4f"
    "&current=pm2_5,pm10&hourly=pm2_5,pm10&forecast_days=4&timezone=auto",
    cfg.weatherLat, cfg.weatherLon);
  String airBody;
  int airSt = httpsGet("air-quality-api.open-meteo.com", airPath, airBody, 6144);
  if (airSt == 200) {
    static AirData air;   // statisch: Task-Stack schonen
    if (parseAirQuality(airBody.c_str(), air)) {
      if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        weather.pmValid = air.nowOk;
        weather.pm25 = air.pm25Now;
        weather.pm10 = air.pm10Now;
        for (int i = 0; i < 3; i++) {
          weather.forecast[i].pmValid = air.day[i].ok;
          weather.forecast[i].pm25 = air.day[i].pm25;
          weather.forecast[i].pm10 = air.day[i].pm10;
        }
        xSemaphoreGive(stateMutex);
      }
      logMsg("AIR", (String("PM2.5=") + air.pm25Now + " PM10=" + air.pm10Now).c_str());
    } else logMsg("AIR", "Antwort ohne PM-Werte");
  } else logMsg("AIR", (String("Abruf fehlgeschlagen: ") + airSt).c_str());
}

void weatherTaskFn(void* param) {
  weatherFetch();
  logTaskMem("weather");
  weatherTaskRunning = false;
  vTaskDelete(NULL);
}

void weatherInit() {
  if (!cfg.weatherAutoLoc) strlcpy(weather.cityName, cfg.weatherCity, sizeof(weather.cityName));
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED || weatherTaskRunning) return;
  weatherTaskRunning = true;
  logMsg("WEATHER", "Abruf...");
  xTaskCreatePinnedToCore(weatherTaskFn, "weather", 12288, NULL, 1, NULL, 0);
}
