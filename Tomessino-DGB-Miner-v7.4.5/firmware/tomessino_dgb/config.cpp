#include "config.h"
#include <Preferences.h>
#include "mining.h"

Preferences     prefs;
Config cfg;
SemaphoreHandle_t stateMutex = nullptr;
String logRing[LOG_CAP];
int    logHead = 0;

void logMsg(const char* lvl, const char* msg) {
  char buf[180];
  unsigned long s = millis() / 1000;
  snprintf(buf, sizeof(buf), "[%02lu:%02lu:%02lu][%-4s] %s",
           s/3600, (s/60)%60, s%60, lvl, msg);
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    logRing[logHead % LOG_CAP] = String(buf);
    logHead++;
    xSemaphoreGive(stateMutex);
  } else if (!stateMutex) {
    // Mutex noch nicht initialisiert (sehr früher Boot) — direkt schreiben
    logRing[logHead % LOG_CAP] = String(buf);
    logHead++;
  }
  Serial.println(buf);
}

// ================================================================
// CONFIG LOAD / SAVE (NVS / Preferences)
// ================================================================
void loadConfig() {
  prefs.begin("tomDGB2", true);
  prefs.getString("wallet",  cfg.wallet,   sizeof(cfg.wallet));
  prefs.getString("p1h",     cfg.pool1Host,sizeof(cfg.pool1Host));
  cfg.pool1Port     = prefs.getUShort("p1p",  DEF_POOL1_PORT);
  prefs.getString("p2h",     cfg.pool2Host,sizeof(cfg.pool2Host));
  cfg.pool2Port     = prefs.getUShort("p2p",  DEF_POOL2_PORT);
  prefs.getString("worker",  cfg.worker,   sizeof(cfg.worker));
  prefs.getString("workerPw",cfg.workerPw, sizeof(cfg.workerPw));
  cfg.cpuMhz        = prefs.getUShort("cpu",  DEF_CPU_MHZ);
  cfg.brightness    = prefs.getUChar("bri",   DEF_BRIGHTNESS);
  cfg.screenTimeout = prefs.getUShort("scto", DEF_SCREEN_TO);
  cfg.timezone      = prefs.getFloat("tz",    1.0f);
  cfg.saveStatus    = prefs.getBool("saveSt", true);
  // Wetter
  cfg.weatherLat      = prefs.getFloat("wLat", 52.5200f);
  cfg.weatherLon      = prefs.getFloat("wLon", 13.4050f);
  cfg.weatherAutoLoc  = prefs.getBool("wAuto", true);
  prefs.getString("wCity", cfg.weatherCity, sizeof(cfg.weatherCity));
  if (strlen(cfg.weatherCity) == 0) strlcpy(cfg.weatherCity, "Berlin, Germany", sizeof(cfg.weatherCity));
  prefs.getString("wTU", cfg.weatherTempUnit, sizeof(cfg.weatherTempUnit));
  if (strlen(cfg.weatherTempUnit) == 0) strlcpy(cfg.weatherTempUnit, "C", sizeof(cfg.weatherTempUnit));
  prefs.getString("wSU", cfg.weatherSpeedUnit, sizeof(cfg.weatherSpeedUnit));
  if (strlen(cfg.weatherSpeedUnit) == 0) strlcpy(cfg.weatherSpeedUnit, "kmh", sizeof(cfg.weatherSpeedUnit));
  cfg.weatherIntervalMin = prefs.getUShort("wIntv", 15);
  if (cfg.weatherIntervalMin < 5) cfg.weatherIntervalMin = 5;  // Mindestabstand (Rate-Limit-Schutz)
  if (prefs.isKey("watch")) prefs.getString("watch", cfg.watchlist, sizeof(cfg.watchlist));
  if (prefs.isKey("webPw")) prefs.getString("webPw", cfg.webPass, sizeof(cfg.webPass));
  if (cfg.saveStatus) {
    // Bis v7.0 als uint32 unter "bestDiff" gespeichert (H1)
    miner.bestDiffAllTime = prefs.isKey("bestD") ? prefs.getDouble("bestD", 0) : (double)prefs.getUInt("bestDiff", 0);
    // Neuer Schlüssel: "blocks" kann falsche Treffer der alten Block-Prüfung enthalten (K2)
    miner.blockHits       = prefs.getUInt("blk2", 0);
  }
  prefs.end();
  logMsg("CFG","Konfiguration geladen");
}

void saveConfig() {
  prefs.begin("tomDGB2", false);
  prefs.putString("wallet",  cfg.wallet);
  prefs.putString("p1h",     cfg.pool1Host);
  prefs.putUShort("p1p",     cfg.pool1Port);
  prefs.putString("p2h",     cfg.pool2Host);
  prefs.putUShort("p2p",     cfg.pool2Port);
  prefs.putString("worker",  cfg.worker);
  prefs.putString("workerPw",cfg.workerPw);
  prefs.putUShort("cpu",     cfg.cpuMhz);
  prefs.putUChar("bri",      cfg.brightness);
  prefs.putUShort("scto",    cfg.screenTimeout);
  prefs.putFloat("tz",       cfg.timezone);
  prefs.putBool("saveSt",    cfg.saveStatus);
  // Wetter
  prefs.putFloat("wLat",  cfg.weatherLat);
  prefs.putFloat("wLon",  cfg.weatherLon);
  prefs.putBool("wAuto",  cfg.weatherAutoLoc);
  prefs.putString("wCity",cfg.weatherCity);
  prefs.putString("wTU",  cfg.weatherTempUnit);
  prefs.putString("wSU",  cfg.weatherSpeedUnit);
  prefs.putUShort("wIntv",cfg.weatherIntervalMin);
  prefs.putString("watch",cfg.watchlist);
  prefs.putString("webPw",cfg.webPass);
  prefs.end();
  logMsg("CFG","Konfiguration gespeichert");
}

void saveBestDiff() {
  if (!cfg.saveStatus) return;
  prefs.begin("tomDGB2", false);
  prefs.putDouble("bestD", miner.bestDiffAllTime);
  if (prefs.isKey("bestDiff")) prefs.remove("bestDiff");
  prefs.putUInt("blk2",     miner.blockHits);
  if (prefs.isKey("blocks")) prefs.remove("blocks");
  prefs.end();
}
