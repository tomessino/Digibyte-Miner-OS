#pragma once
#include <Arduino.h>

// ================================================================
// MINER STATE
// ================================================================
struct MinerState {
  bool     poolConn      = false;
  uint8_t  activePool    = 1;
  uint32_t hashrate      = 0;
  uint32_t hrCore1       = 0;     // Anteil Kern 1
  uint32_t hrCore0       = 0;     // Anteil Kern 0
  uint32_t hwErrors      = 0;     // Hardware meldete einen Treffer, den die Software nicht bestätigt
  uint32_t hwChecks      = 0;     // Laufende Prüfung: geprüfte Runden
  uint32_t hwCheckFails  = 0;     // Laufende Prüfung: letzter Nonce einer Runde falsch
  uint32_t sharesSent    = 0;     // Eingereicht (K3: nur ab Pool-Diff)
  uint32_t sharesOk      = 0;
  uint32_t sharesRej     = 0;
  uint32_t uptime        = 0;
  float    tempC         = 0.0f;
  double   difficultyF   = 1.0;   // Pool-Diff, auch unter 1 (E8)
  double   bestDiff      = 0;     // Best Diff seit dem Start (H1, E10)
  double   bestDiffAllTime = 0;   // All-Time Best Diff (H1)
  double   lastShareDiff = 0;     // Diff des zuletzt gesendeten Shares
  uint32_t blockHits     = 0;     // Gefundene Blöcke
  uint32_t jobs          = 0;     // Vom Pool erhaltene Jobs seit dem Start
  char     lastNonce[18] = "--";
  char     poolDisp[86]  = "";
  bool     ntpOk         = false;
  bool     screenOn      = true;
};
extern MinerState miner;

// Rechenweg, zur Laufzeit wählbar für Messungen (Plan 2.6)
enum MinerBackend { BK_MBEDTLS=0, BK_SOFT=1, BK_HW=2 };

float readTemp();
void miningStart();                         // Selbsttests und Miner-Tasks
void miningSetMode(int backend, bool core0);
int  miningBackend();
bool miningCore0();
bool miningHwTimed();         // feste Wartezeiten statt Pollen aktiv
uint32_t miningHwCycLoop();   // Takte je Hash x100 in der Hardware-Schleife
uint32_t miningHwCycAll();    // Takte je Hash x100 von Runde zu Runde
void miningLockJob();
void miningUnlockJob();
bool miningNextShare(struct ShareMsg& out);
void miningStatsTick(uint32_t now);         // 1x pro Sekunde aus dem App-Task
