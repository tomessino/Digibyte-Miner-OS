/*
 * Tomessino-DGB v7.4 — DigiByte-SHA-256d-Solo-Miner für ESP32-2432S028R (CYD)
 *
 * Kern 1: Miner über die SHA-Hardware. Kern 0: alles andere (Task "app") und ein Software-Miner.
 * Bauen, Bibliotheken und Partition: docs/INSTALLATION.md. Bedienung: docs/ANLEITUNG.md.
 * Die TFT-Einstellungen stehen in firmware/tft_espi/User_Setup.h.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "mining.h"
#include "mining_core.h"
#include "stratum.h"
#include "web.h"       // vor display.h: TFT_eSPI setzt FS_NO_GLOBALS, das bricht WebServer.h
#include "display.h"
#include "net_services.h"
#include "net_parse.h"

// ================================================================
// TIMER
// ================================================================
static uint32_t t0          = 0;
static uint32_t tHash       = 0;
static uint32_t tWs         = 0;
static uint32_t tWifiRetry  = 0;
static uint32_t tPrice      = 0;
static uint32_t tWeather    = 0;
static uint32_t tSwarm      = 0;
static uint32_t tSwarmPrune = 0;

// ================================================================
// SETUP
// ================================================================
static void appTask(void*);

void setup() {
  Serial.begin(115200); delay(200);
  Serial.println("\n=== " FW_FULL " ===");

  // Mutex für Thread-Safety zwischen Hauptloop und FreeRTOS-Tasks
  // (priceTaskFn/weatherTaskFn) — MUSS vor jedem logMsg()-Aufruf stehen.
  stateMutex = xSemaphoreCreateMutex();

  // Backlight aus während Display-Init
  pinMode(PIN_BL, OUTPUT); analogWrite(PIN_BL, 0);
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Display init
  tft.init();
  // Am Gerät mit Farbstreifen geprüft (01.11.2026): Dieses Panel braucht Inversion aus und BGR.
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.writecommand(TFT_MADCTL);
  tft.writedata(TFT_MAD_MX | TFT_MAD_MV | TFT_MAD_BGR);
  tft.fillScreen(C_BG);
  delay(100);
  analogWrite(PIN_BL, 100);

  // XPT2046 Touch init (VSPI, separate Pins)
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);
  logMsg("TOUCH", "XPT2046 bereit");

  loadConfig();
  loadWatchlist();
  webAuthSetup();

  // Selbsttest (Plan 1.9): Sollwerte wie in tools/test_hash_vektoren.py
  char selfTest[140];
  miningSelfTest(selfTest, sizeof(selfTest));
  logMsg("TEST", selfTest);
  miningStart();

  tftSplash();
  analogWrite(PIN_BL, cfg.brightness);
  setCpuFrequencyMhz(cfg.cpuMhz);

  // WiFi — autoConnect: verbindet mit gespeichertem WLAN,
  // strahlt bei Erststart / Fehler "DGB-Miner-Setup" aus
  WiFiManager wm;
  wm.setConfigPortalTimeout(AP_TIMEOUT);
  wm.setConnectTimeout(20);
  wm.setAPClientCheck(true);

  // Extra-Felder im Portal
  WiFiManagerParameter pWallet("wallet", "DGB Wallet (D...)", cfg.wallet, 129);
  WiFiManagerParameter pPool1("pool1", "Pool 1 Host", cfg.pool1Host, 65);
  wm.addParameter(&pWallet);
  wm.addParameter(&pPool1);

  tftWifiSetup();

  bool wmOk = wm.autoConnect(AP_SSID, AP_PASS);

  if (!wmOk) {
    logMsg("WIFI", "Kein WLAN - Offline");
    tft.fillScreen(C_BG);
    tft.setTextColor(C_RED, C_BG); tft.setCursor(6, 60); tft.setTextSize(1);
    tft.print("WLAN-Fehler - Neustart...");
    delay(3000); ESP.restart();
  }

  // Wallet aus Portal-Eingabe übernehmen
  // Nur bei echter Änderung speichern (E14), sonst schriebe jeder Start ins NVS.
  bool changed = false;
  if (strlen(pWallet.getValue()) > 10 && strcmp(pWallet.getValue(), cfg.wallet) != 0) { strlcpy(cfg.wallet, pWallet.getValue(), sizeof(cfg.wallet)); changed = true; }
  if (strlen(pPool1.getValue()) > 3 && strcmp(pPool1.getValue(), cfg.pool1Host) != 0) { strlcpy(cfg.pool1Host, pPool1.getValue(), sizeof(cfg.pool1Host)); changed = true; }
  if (changed) saveConfig();

  logMsg("WIFI", (WiFi.SSID() + " / " + WiFi.localIP().toString()).c_str());
  tftConnected();
  delay(1200);

  // NTP
  ntpCli.begin();
  ntpCli.forceUpdate();
  miner.ntpOk = (ntpCli.getEpochTime() > 1000000UL);

  // HTTP + WebSocket — MUSS vor stratumConnect stehen!
  wsSrv.onEvent(onWsEvent);
  httpSrv.addHandler(&wsSrv);
  wsSrv.addMiddleware(&webAuth);
  httpSrv.on("/", [](AsyncWebServerRequest* r) { 
    // Direkt aus dem Flash streamen. Die const-char*-Variante kopiert die Seite in einen String:
    // Fehlt ein freier Block dieser Größe, antwortet sie mit 200 und 0 Byte.
    r->send(200, "text/html", (const uint8_t*)DASH_HTML, strlen(DASH_HTML));
   }).addMiddleware(&webAuth);
  httpSrv.on("/probe", [](AsyncWebServerRequest* r) {
    char buf[400];
    snprintf(buf, sizeof(buf),
      "{\"fw\":\"%s\",\"coin\":\"DGB\",\"model\":\"CYD-ESP32\","
      "\"hashrate\":%lu,\"hr\":%lu,\"hashRate\":%lu,"
      "\"difficulty\":%.6f,\"diff\":%.6f,"
      "\"sharesOk\":%lu,\"sharesRej\":%lu,"
      "\"bestDiff\":%.3f,\"bestDifficulty\":%.3f,"
      "\"uptime\":%lu,\"worker\":\"%s\",\"ip\":\"%s\"}",
      FW_FULL, miner.hashrate, miner.hashrate, miner.hashrate,
      miner.difficultyF, miner.difficultyF,
      miner.sharesOk, miner.sharesRej,
      miner.bestDiffAllTime, miner.bestDiffAllTime,
      miner.uptime, cfg.worker, WiFi.localIP().toString().c_str());
    r->send(200, "application/json", buf);
  });
  httpSrv.on("/api/system/info", [](AsyncWebServerRequest* r) { r->send(200, "application/json", buildJson()); }).addMiddleware(&webAuth);
  // Für den Gerätetest (tools/device): Seite 0-4 per Web wählen
  httpSrv.on("/api/display/page", [](AsyncWebServerRequest* r) {
    if (!r->hasParam("p")) { r->send(400, "text/plain", "p fehlt"); return; }
    displayRequestPage(r->getParam("p")->value().toInt());
    r->send(200, "text/plain", "ok");
  }).addMiddleware(&webAuth);
  // Für Messungen (Plan 2.6): Rechenweg b=0 mbedTLS, 1 Software, 2 Hardware; c0=0/1 zweiter Miner auf Kern 0
  httpSrv.on("/api/miner/mode", [](AsyncWebServerRequest* r) {
    int b = r->hasParam("b") ? r->getParam("b")->value().toInt() : miningBackend();
    bool c0 = r->hasParam("c0") ? r->getParam("c0")->value().toInt() != 0 : miningCore0();
    miningSetMode(b, c0);
    r->send(200, "text/plain", String(miningBackend()) + " " + (miningCore0() ? "1" : "0"));
  }).addMiddleware(&webAuth);
  // Für den Gerätetest: Farbdarstellung prüfen (invert=0/1, bgr=0/1, bars=0/1)
  httpSrv.on("/api/display/color", [](AsyncWebServerRequest* r) {
    int inv = r->hasParam("invert") ? r->getParam("invert")->value().toInt() : 0;
    int bgr = r->hasParam("bgr") ? r->getParam("bgr")->value().toInt() : 1;
    int bars = r->hasParam("bars") ? r->getParam("bars")->value().toInt() : 0;
    displayRequestColorTest(inv, bgr, bars);
    r->send(200, "text/plain", "ok");
  }).addMiddleware(&webAuth);
  httpSrv.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "Not found"); });
  httpSrv.begin();
  logMsg("HTTP", ("http://" + WiFi.localIP().toString()).c_str());

  // Swarm UDP
  swarmUDP.begin(55123);

  // Pool verbinden
  stratumConnect(1);

  // fetchPrices() NICHT hier — würde WiFiClient blockieren und HTTP crashen!
  // Wird im Loop nach 5s erstmalig aufgerufen.
  tPrice   = millis() - 295000;  // Trigger: erster Fetch nach 5s
  tWeather = millis() - ((uint32_t)cfg.weatherIntervalMin*60000UL - 20000UL);  // Trigger: erster Wetter-Fetch nach 20s

  t0 = tLastTouch = millis();
  logMsg("SYS", FW_FULL " · DGB SHA-256d · Bereit!");

  // Kern 1 gehört dem Miner, alles andere läuft auf Kern 0 (Plan 2.1)
  xTaskCreatePinnedToCore(appTask, "app", 12288, NULL, 2, NULL, 0);
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  vTaskDelete(NULL);   // Der Arduino-Task auf Kern 1 wird nicht mehr gebraucht
}

static void appLoop() {
  uint32_t now = millis();

  inputTick();  // Touch + Button

  wifiChangeTick();

  // Passwort vergessen: BOOT 10 Sekunden gehalten
  if (passReset) {
    passReset = false;
    cfg.webPass[0] = 0;
    saveConfig();
    logMsg("SYS", "Dashboard-Passwort gelöscht");
    delay(300); ESP.restart();
  }

  // Config-Modus via Long-Press (3s Boot-Button)
  if (configMode) {
    configMode = false;
    logMsg("SYS", "Config-Modus...");
    tft.fillScreen(C_BG);
    tft.setTextColor(C_ACCENT, C_BG); tft.setTextSize(1);
    tft.setCursor(4, 30); tft.print("Config-Modus");
    tft.setCursor(4, 50); tft.print("WLAN: \"" AP_SSID "\"");
    tft.setCursor(4, 70); tft.print("Browser: 192.168.4.1");
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.startConfigPortal(AP_SSID, AP_PASS);
    ESP.restart();
  }

  stratumTick();

  // 1s: Hashrate + Uptime + Temp + NTP
  if (now - tHash >= 1000) {
    miningStatsTick(now);
    miner.uptime    = (now - t0) / 1000;
    miner.tempC     = readTemp();
    static uint32_t tTempWarn = 0;
    if (miner.tempC >= 85 && (tTempWarn == 0 || now - tTempWarn > 300000)) {
      logMsg("WARN", (String("Temp ") + (int)miner.tempC + "C!").c_str());
      tTempWarn = now;
    }
    if (WiFi.status() == WL_CONNECTED) { ntpCli.update(); miner.ntpOk = (ntpCli.getEpochTime() > 1000000UL); }
    tHash = now;
  }

  // Display 3s (nicht 2s — stiehlt sonst Mining-Zeit)
  if (now - tDisplay >= 3000) { tftUpdate(); tDisplay = now; }

  // WebSocket 4s
  if (now - tWs >= 4000) {
    if (WiFi.status() == WL_CONNECTED && wsSrv.count()) { wsSrv.textAll(buildJson()); wsSrv.cleanupClients(); }
    tWs = now;
  }

  // Preise alle 5 Minuten (NICHT im setup!)
  if (now - tPrice >= 300000) { fetchPrices(); tPrice = now; }

  // Wetter alle 15 Minuten (Open-Meteo, non-blocking Task)
  if (now - tWeather >= (uint32_t)cfg.weatherIntervalMin*60000UL) { fetchWeather(); tWeather = now; }

  // Swarm 5s
  if (now - tSwarm >= 5000) { swarmBroadcast(); tSwarm = now; }
  swarmListen();   // non-blocking, jeden Loop-Durchlauf prüfen
  if (now - tSwarmPrune >= 5000) { swarmPrune(); tSwarmPrune = now; }

  // WiFi-Reconnect
  if (WiFi.status() != WL_CONNECTED && now - tWifiRetry > 30000) {
    logMsg("WIFI", "Reconnect..."); WiFi.reconnect(); tWifiRetry = now;
  }
}

static void appTask(void*) {
  for (;;) {
    appLoop();
    vTaskDelay(1);
  }
}
