#include "web.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "mining.h"
#include "stratum.h"
#include "net_services.h"
#include "net_parse.h"


AsyncWebServer  httpSrv(80);
AsyncWebSocket  wsSrv("/ws");
AsyncAuthenticationMiddleware webAuth;

// ================================================================
// WLAN WECHSELN — das neue Netz wird erst nur im RAM ausprobiert. Klappt es nicht,
// verbindet sich der Miner wieder mit dem alten, ein Tippfehler sperrt also nicht aus.
// Läuft im App-Task, der WebSocket-Handler setzt nur die Anfrage.
// ================================================================
static char wifiNewSsid[33];
static char wifiNewPass[64];
static volatile int wifiChangeState = 0;

void wifiChangeTick() {
  if (!wifiChangeState) return;
  wifiChangeState = 0;
  String oldSsid = WiFi.SSID(), oldPass = WiFi.psk();
  bool samePass = !wifiNewPass[0] && oldSsid == wifiNewSsid;
  if (samePass) { logMsg("WIFI", "Kein neues Passwort, nichts geändert"); return; }
  logMsg("WIFI", (String("Teste ") + wifiNewSsid).c_str());
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.begin(wifiNewSsid, wifiNewPass);
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(100);
  bool ok = WiFi.status() == WL_CONNECTED;
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  if (ok) {
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(wifiNewSsid, wifiNewPass);   // jetzt dauerhaft
    logMsg("WIFI", "Neues WLAN gespeichert, Neustart");
    delay(1000);
    ESP.restart();
  }
  logMsg("WIFI", "Neues WLAN nicht erreichbar, altes bleibt");
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(oldSsid.c_str(), oldPass.c_str());
}

// Ohne Passwort bleibt AUTH_NONE, alles ist offen wie bisher.
void webAuthSetup() {
  if (!cfg.webPass[0]) return;
  webAuth.setUsername("admin");
  webAuth.setPassword(cfg.webPass);
  webAuth.setRealm("Tomessino-DGB");
  webAuth.setAuthFailureMessage("Passwort erforderlich");
  webAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
  webAuth.generateHash();
  logMsg("HTTP", "Dashboard mit Passwort geschützt");
}

// ================================================================
// JSON STATUS (Web-Dashboard)
// ================================================================
// Text für einen JSON-String: Anführungszeichen, Backslash und Steuerzeichen
// maskieren. Ein Ortsname mit " machte sonst das ganze Status-JSON ungültig.
static const char* esc(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (; *in && o + 7 < n; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
    else if (c < 0x20) o += snprintf(out + o, n - o, "\\u%04x", c);
    else out[o++] = c;
  }
  out[o] = 0;
  return out;
}

String buildJson() {
  // Thread-sicherer Snapshot von prices/weather — diese Felder werden
  // von priceTaskFn/weatherTaskFn auf Core 0 geschrieben, während
  // buildJson() im Hauptloop auf Core 1 läuft.
  PriceData   pSnap;
  WeatherData wSnap;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    pSnap = prices;
    wSnap = weather;
    xSemaphoreGive(stateMutex);
  } else {
    pSnap = prices;  // Fallback falls Mutex kurzzeitig belegt: leicht veraltete Daten statt Blockade
    wSnap = weather;
  }

  // Log-Array
  String logArr="[";
  // Nur die letzten Zeilen: Das volle Log liefe bei jedem Abruf durch den Heap (E6).
  const int LOG_JSON_LINES=25;
  int total=min(logHead,LOG_JSON_LINES);
  int si=logHead-total;
  for (int i=0;i<total;i++){
    if(i>0) logArr+=",";
    String s=logRing[(si+i)%LOG_CAP];
    s.replace("\\","\\\\"); s.replace("\"","\\\"");
    logArr+="\""+s+"\"";
  }
  logArr+="]";

  // Swarm-Peers-Array (nur aktive Peers, sortiert wäre Aufwand im Backend —
  // Sortierung übernimmt das Frontend via JS, hier nur Rohdaten)
  String peersArr = "[";
  bool firstPeer = true;
  for (int i = 0; i < swarmPeerCount; i++) {
    if (!swarmPeers[i].active) continue;
    if (!firstPeer) peersArr += ",";
    firstPeer = false;
    char peerBuf[360], ew[72], ei[40];
    snprintf(peerBuf, sizeof(peerBuf),
      "{\"worker\":\"%s\",\"ip\":\"%s\",\"hr\":%lu,\"ok\":%lu,\"rej\":%lu,"
      "\"bestDiffAT\":%.4f,\"blocks\":%lu,\"jobs\":%ld,\"up\":%lu,\"temp\":%.1f}",
      esc(swarmPeers[i].workerName, ew, sizeof(ew)), esc(swarmPeers[i].ip, ei, sizeof(ei)), swarmPeers[i].hashrate,
      swarmPeers[i].sharesOk, swarmPeers[i].sharesRej,
      swarmPeers[i].bestDiffAllTime, swarmPeers[i].blockHits, (long)swarmPeers[i].jobs, swarmPeers[i].uptime, swarmPeers[i].tempC);
    peersArr += peerBuf;
  }
  peersArr += "]";

  // Ohne Log (E6: Das Log wird unten angehängt, kann den Puffer also nicht mehr sprengen)
  char eDisp[96], eWallet[140], eP1[80], eP2[80], eWorker[72], eSsid[72], eCity[100], eCfgCity[100], eWatch[240];
  char j[2560];
  int jl=  snprintf(j,sizeof(j),
    "{"
    "\"hashrate\":%lu,\"hrCore1\":%lu,\"hrCore0\":%lu,\"minerMode\":%d,\"minerCore0\":%s,\"hwErrors\":%lu,\"hwChecks\":%lu,\"hwCheckFails\":%lu,\"hwTimed\":%s,\"hwCycLoop\":%lu,\"hwCycAll\":%lu,\"sharesSent\":%lu,\"sharesOk\":%lu,\"sharesRej\":%lu,"
    "\"uptime\":%lu,\"temp\":%.1f,"
    "\"freeHeap\":%lu,\"maxAlloc\":%lu,\"poolConn\":%s,\"activePool\":%u,\"difficulty\":%.6f,\"netDiff\":%.2f,\"height\":%lu,\"lastDiff\":%.6f,"
    "\"bestDiff\":%.6f,\"bestDiffAllTime\":%.6f,\"blockHits\":%lu,"
    "\"lastNonce\":\"%s\",\"poolDisp\":\"%s\","
    "\"ip\":\"%s\",\"wallet\":\"%s\","
    "\"pool1Host\":\"%s\",\"pool1Port\":%u,"
    "\"pool2Host\":\"%s\",\"pool2Port\":%u,"
    "\"worker\":\"%s\",\"workerCfg\":\"%s\",\"cpuMhz\":%u,"
    "\"brightness\":%u,\"screenTimeout\":%u,\"timezone\":%.1f,"
    "\"rssi\":%d,\"ssid\":\"%s\","
    "\"time\":\"%s\",\"date\":\"%s\",\"ntpOk\":%s,"
    "\"dgbUsd\":%.6f,\"dgbChg\":%.2f,"
    "\"btcUsd\":%.2f,\"btcChg\":%.2f,"
    "\"weatherValid\":%s,\"weatherTemp\":%.1f,\"weatherFeels\":%.1f,"
    "\"weatherHumidity\":%d,\"weatherPressure\":%.0f,\"weatherWind\":%.1f,"
    "\"weatherCode\":%d,\"sunrise\":\"%s\",\"sunset\":\"%s\","
    "\"weatherCity\":\"%s\",\"weatherCfgCity\":\"%s\",\"weatherLat\":%.4f,\"weatherLon\":%.4f,"
    "\"weatherAutoLoc\":%s,\"weatherTempUnit\":\"%s\",\"weatherSpeedUnit\":\"%s\","
    "\"weatherIntervalMin\":%u,"
    "\"watchlist\":\"%s\",\"webPassSet\":%s,"
    "\"jobs\":%lu,\"ledMode\":%u,\"ledColor\":\"#%06lx\",\"ledBright\":%u,\"marketView\":%u,"
    "\"fwName\":\"%s\",\"fwVersion\":\"%s\"",
    miner.hashrate,miner.hrCore1,miner.hrCore0,miningBackend(),miningCore0()?"true":"false",miner.hwErrors,miner.hwChecks,miner.hwCheckFails,miningHwTimed()?"true":"false",(unsigned long)miningHwCycLoop(),(unsigned long)miningHwCycAll(),miner.sharesSent,miner.sharesOk,miner.sharesRej,
    miner.uptime,miner.tempC,
    (unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getMaxAllocHeap(),miner.poolConn?"true":"false",miner.activePool,miner.difficultyF,job.valid?job.netDiff:0.0,(unsigned long)job.height,miner.lastShareDiff,
    miner.bestDiff,miner.bestDiffAllTime,miner.blockHits,
    miner.lastNonce,esc(miner.poolDisp,eDisp,sizeof(eDisp)),
    WiFi.localIP().toString().c_str(),esc(cfg.wallet,eWallet,sizeof(eWallet)),
    esc(cfg.pool1Host,eP1,sizeof(eP1)),cfg.pool1Port,esc(cfg.pool2Host,eP2,sizeof(eP2)),cfg.pool2Port,
    effWorker,esc(cfg.worker,eWorker,sizeof(eWorker)),(unsigned)getCpuFrequencyMhz(),   // effWorker hat nur sichere Zeichen; Takt ist fest 240 MHz
    cfg.brightness,cfg.screenTimeout,cfg.timezone,
    WiFi.RSSI(),esc(WiFi.SSID().c_str(),eSsid,sizeof(eSsid)),
    timeStr().c_str(),dateStr().c_str(),miner.ntpOk?"true":"false",
    pSnap.dgbUsd,pSnap.dgbChg,
    pSnap.btcUsd,pSnap.btcChg,
    wSnap.valid?"true":"false",wSnap.tempC,wSnap.feelsLikeC,
    wSnap.humidity,wSnap.pressureHpa,wSnap.windKmh,
    wSnap.weatherCode,wSnap.sunrise,wSnap.sunset,
    esc(wSnap.cityName,eCity,sizeof(eCity)),esc(cfg.weatherCity,eCfgCity,sizeof(eCfgCity)),cfg.weatherLat,cfg.weatherLon,
    cfg.weatherAutoLoc?"true":"false",cfg.weatherTempUnit,cfg.weatherSpeedUnit,
    cfg.weatherIntervalMin,
    esc(cfg.watchlist,eWatch,sizeof(eWatch)),cfg.webPass[0]?"true":"false",
    (unsigned long)miner.jobs,cfg.ledMode,(unsigned long)cfg.ledColor,cfg.ledBright,cfg.marketView,
    FW_NAME,FW_VERSION
  );
  if (jl<0||jl>=(int)sizeof(j)) { Serial.printf("[JSON] Status-Kopf zu lang: %d\n",jl); j[0]=0; strlcpy(j,"{\"fwVersion\":\"" FW_VERSION "\"",sizeof(j)); }

  String result; result.reserve(strlen(j)+logArr.length()+peersArr.length()+40);
  result = j;
  result += ",\"log\":"; result += logArr;
  result += ",\"swarmPeers\":"; result += peersArr;
  result += "}";
  return result;
}

// ================================================================
// WEBSOCKET HANDLER
// ================================================================
void onWsEvent(AsyncWebSocket* s,AsyncWebSocketClient* c,
               AwsEventType t,void* arg,uint8_t* data,size_t len) {
  if (t==WS_EVT_CONNECT){ logMsg("WS",(String("Client #")+c->id()).c_str()); c->text(buildJson()); return; }
  if (t==WS_EVT_DISCONNECT){ logMsg("WS",(String("Disc #")+c->id()).c_str()); return; }
  if (t!=WS_EVT_DATA) return;
  AwsFrameInfo* fi=(AwsFrameInfo*)arg;
  if (fi->opcode!=WS_TEXT) return;
  char buf[1024]; size_t n=min(len,sizeof(buf)-1);
  memcpy(buf,data,n); buf[n]='\0';
  JsonDocument doc;
  if (deserializeJson(doc,buf)) return;
  const char* action=doc["action"]|"";

  if (strcmp(action,"save")==0){
    if (doc["wallet"].is<const char*>()) {
      // "Adresse.Name": Der Name geht als Worker an den Pool und ins LAN
      const char* wa = doc["wallet"];
      const char* dot = strchr(wa, '.');
      if (dot && dot[1] && !validWorkerName(dot + 1)) logMsg("CFG", "Name hinter der Wallet ungültig, nicht übernommen");
      else strlcpy(cfg.wallet, wa, sizeof(cfg.wallet));
    }
    if (doc["pool1Host"].is<const char*>()) strlcpy(cfg.pool1Host,doc["pool1Host"]|cfg.pool1Host,sizeof(cfg.pool1Host));
    if (!doc["pool1Port"].isNull())         cfg.pool1Port=doc["pool1Port"]|cfg.pool1Port;
    if (doc["pool2Host"].is<const char*>()) strlcpy(cfg.pool2Host,doc["pool2Host"]|cfg.pool2Host,sizeof(cfg.pool2Host));
    if (!doc["pool2Port"].isNull())         cfg.pool2Port=doc["pool2Port"]|cfg.pool2Port;
    if (doc["worker"].is<const char*>()) {
      const char* wk = doc["worker"];   // leer: Standardname
      if (!wk[0] || validWorkerName(wk)) strlcpy(cfg.worker, wk, sizeof(cfg.worker));
      else logMsg("CFG", "Worker-Name ungültig, nicht übernommen");
    }
    if (doc["workerPw"].is<const char*>())  strlcpy(cfg.workerPw, doc["workerPw"] |cfg.workerPw, sizeof(cfg.workerPw));
    if (!doc["brightness"].isNull())        cfg.brightness=doc["brightness"]|cfg.brightness;
    if (!doc["screenTimeout"].isNull())     cfg.screenTimeout=doc["screenTimeout"]|cfg.screenTimeout;
    if (!doc["timezone"].isNull())          cfg.timezone  =doc["timezone"].as<float>();
    if (!doc["weatherAutoLoc"].isNull())    cfg.weatherAutoLoc = doc["weatherAutoLoc"].as<bool>();
    if (!cfg.weatherAutoLoc) {
      float lat = doc["weatherLat"].isNull() ? cfg.weatherLat : doc["weatherLat"].as<float>();
      float lon = doc["weatherLon"].isNull() ? cfg.weatherLon : doc["weatherLon"].as<float>();
      bool coordsChanged = fabsf(lat - cfg.weatherLat) > 0.0005f || fabsf(lon - cfg.weatherLon) > 0.0005f;
      if (coordsChanged) {
        if (validLatLon(lat, lon)) { cfg.weatherLat = lat; cfg.weatherLon = lon; cfg.weatherGeo = false; }
        else logMsg("CFG", "Koordinaten ungültig (Breite -90..90, Länge -180..180), nicht übernommen");
      }
      if (doc["weatherCity"].is<const char*>()) {
        const char* city = doc["weatherCity"];
        if (!strncmp(city, "Standort wird", 13)) city = cfg.weatherCity;   // Anzeigetext, kein Ort
        // Dashboard schickt weatherGeo, wenn die Koordinatenfelder leer sind. Ältere Seiten:
        // nur der Ort geändert, Koordinaten gleich. In beiden Fällen die Koordinaten suchen.
        bool cityChanged = strlen(city) > 1 && strcmp(city, cfg.weatherCity) != 0;
        if (doc["weatherGeo"].as<bool>() && strlen(city) > 1) cfg.weatherGeo = true;
        else if (cityChanged && !coordsChanged) cfg.weatherGeo = true;
        strlcpy(cfg.weatherCity, city, sizeof(cfg.weatherCity));
      }
    }
    if (doc["weatherTempUnit"].is<const char*>())  strlcpy(cfg.weatherTempUnit,  doc["weatherTempUnit"]|cfg.weatherTempUnit,   sizeof(cfg.weatherTempUnit));
    if (doc["weatherSpeedUnit"].is<const char*>()) strlcpy(cfg.weatherSpeedUnit, doc["weatherSpeedUnit"]|cfg.weatherSpeedUnit, sizeof(cfg.weatherSpeedUnit));
    if (!doc["weatherIntervalMin"].isNull()) {
      uint16_t wi = doc["weatherIntervalMin"]|15;
      cfg.weatherIntervalMin = (wi < 5) ? 5 : wi;  // Mindestabstand
    }
    if (doc["watchlist"].is<const char*>()) {
      const char* wl = doc["watchlist"];
      WatchEntry probe[WATCH_MAX];
      if (strlen(wl) < sizeof(cfg.watchlist) && parseWatchlist(wl, probe, WATCH_MAX) >= 0) strlcpy(cfg.watchlist, wl, sizeof(cfg.watchlist));
      else logMsg("CFG", "Watchlist ungültig, nicht übernommen");
    }
    if (!doc["ledMode"].isNull())    cfg.ledMode = min(2, doc["ledMode"].as<int>());
    if (!doc["ledBright"].isNull())  cfg.ledBright = constrain(doc["ledBright"].as<int>(), 0, 100);
    if (doc["ledColor"].is<const char*>()) {
      const char* hc = doc["ledColor"];
      if (hc[0] == '#') cfg.ledColor = strtoul(hc + 1, nullptr, 16) & 0xFFFFFF;
    }
    if (!doc["marketView"].isNull()) cfg.marketView = min(2, doc["marketView"].as<int>());
    if (doc["webPassClear"].as<bool>()) cfg.webPass[0] = 0;
    else if (doc["webPass"].is<const char*>() && strlen(doc["webPass"].as<const char*>()) > 0)
      strlcpy(cfg.webPass, doc["webPass"], sizeof(cfg.webPass));
    saveConfig();
    c->text("{\"ok\":true}"); delay(400); ESP.restart();
  }
  else if (strcmp(action,"wifiSave")==0)  {
    const char* id = doc["ssid"] | "";
    const char* pw = doc["pass"] | "";
    size_t idLen = strlen(id), pwLen = strlen(pw);
    if (idLen < 1 || idLen > 32 || pwLen > 63 || (pwLen > 0 && pwLen < 8)) { logMsg("WIFI", "Eingabe ungültig: SSID 1-32, Passwort 8-63 Zeichen"); return; }
    if (wifiChangeState) return;
    strlcpy(wifiNewSsid, id, sizeof(wifiNewSsid));
    strlcpy(wifiNewPass, pw, sizeof(wifiNewPass));
    wifiChangeState = 1;
    c->text("{\"ok\":true}");
  }
  else if (strcmp(action,"restart")==0)   { c->text("{\"ok\":true}"); delay(300); ESP.restart(); }
  else if (strcmp(action,"wifiReset")==0) { WiFiManager wm; wm.resetSettings(); c->text("{\"ok\":true}"); delay(300); ESP.restart(); }
  else if (strcmp(action,"resetStats")==0){ miningRequestReset(true, false); c->text("{\"ok\":true}"); }
  else if (strcmp(action,"resetBest")==0) { miningRequestReset(false, true); c->text("{\"ok\":true}"); }
  else if (strcmp(action,"ledPreview")==0) {
    // Nur anzeigen, nicht speichern: Speichern läuft über "save"
    if (!doc["ledMode"].isNull())   cfg.ledMode = min(2, doc["ledMode"].as<int>());
    if (!doc["ledBright"].isNull()) cfg.ledBright = constrain(doc["ledBright"].as<int>(), 0, 100);
    if (doc["ledColor"].is<const char*>()) { const char* hc = doc["ledColor"]; if (hc[0] == '#') cfg.ledColor = strtoul(hc + 1, nullptr, 16) & 0xFFFFFF; }
  }
  else if (strcmp(action,"ping")==0)      { c->text(buildJson()); }
  else if (strcmp(action,"fetchPrice")==0){ fetchPrices(); c->text(buildJson()); }
  else if (strcmp(action,"fetchWeather")==0){ fetchWeather(); c->text(buildJson()); }
}
