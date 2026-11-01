#include "stratum.h"
#include <WiFi.h>
#include "config.h"
#include "mining.h"
#include "stratum_core.h"

#define POOL_SILENCE_MS 300000   // Ohne Daten vom Pool gilt die Verbindung als tot (statt mining.ping, E4)
#define LINE_MAX_LEN    6000

StratumJob job;
WiFiClient      stratumSock;

static uint32_t tRetry      = 0;
static uint32_t tLastRx     = 0;
static String   stratBuf    = "";
static int      nextSubmitId = STRATUM_ID_SUBMIT_FIRST;
static bool     lineTooLong = false;

// ================================================================
// STRATUM CONNECT
// ================================================================
bool stratumConnect(uint8_t poolNum) {
  if (strlen(cfg.wallet)<10) { logMsg("POOL","DGB Wallet fehlt!"); return false; }
  const char* host = (poolNum==2) ? cfg.pool2Host : cfg.pool1Host;
  uint16_t    port = (poolNum==2) ? cfg.pool2Port  : cfg.pool1Port;
  snprintf(miner.poolDisp, sizeof(miner.poolDisp), "%s:%d", host, port);
  logMsg("POOL", (String("Verbinde P")+poolNum+": "+host+":"+port).c_str());
  stratumSock.stop(); delay(100);
  stratBuf="";
  miningLockJob(); job.valid=false; job.haveExtranonce=false; job.generation++; miningUnlockJob();
  if (!stratumSock.connect(host,port)) {
    logMsg("POOL","TCP fehlgeschlagen"); miner.poolConn=false; return false;
  }
  stratumSock.setTimeout(5000);
  miner.poolConn=true; miner.activePool=poolNum; tLastRx=millis();
  nextSubmitId=STRATUM_ID_SUBMIT_FIRST;
  stratumSock.print(String("{\"id\":")+STRATUM_ID_SUBSCRIBE+",\"method\":\"mining.subscribe\",\"params\":[\"" FW_FULL "\",null]}\n");
  stratumSock.print(String("{\"id\":")+STRATUM_ID_AUTHORIZE+",\"method\":\"mining.authorize\",\"params\":[\""+cfg.wallet+"."+cfg.worker+"\",\""+cfg.workerPw+"\"]}\n");
  // Nur ein Wunsch: Ohne ihn senkt der Pool die Diff erst nach ca. 13 Minuten.
  stratumSock.print(String("{\"id\":")+STRATUM_ID_SUGGEST+",\"method\":\"mining.suggest_difficulty\",\"params\":[0.001]}\n");
  logMsg("POOL","Subscribe+Authorize gesendet");
  return true;
}

static void sendShare(const ShareMsg& share) {
  char user[170];
  snprintf(user, sizeof(user), "%s.%s", cfg.wallet, cfg.worker);
  char line[420];
  size_t n = stratumFormatSubmit(line, sizeof(line), nextSubmitId, user, share);
  if (!n) { logMsg("MINE","Submit zu lang"); return; }
  nextSubmitId++;
  stratumSock.write((const uint8_t*)line, n);
  miner.sharesSent++;
  miner.lastShareDiff = share.diff;
  snprintf(miner.lastNonce, sizeof(miner.lastNonce), "0x%08lx", (unsigned long)share.nonce);
  logMsg("MINE", (String("Share: ")+miner.lastNonce).c_str());
}

static void handleLine(const String& line) {
  static StratumReply r;
  miningLockJob();
  StratumEvent ev = stratumParseLine(line.c_str(), job, r);
  if (ev == EV_DIFF) { miner.difficultyF = r.diff; job.generation++; }
  miningUnlockJob();
  char msg[150];
  switch (ev) {
    case EV_JOB:
      snprintf(msg, sizeof(msg), "Job:%s br:%d cb:%d/%d net:%.0f", job.id, job.merkleBranchCount,
               (int)strlen(job.coinb1)/2, (int)strlen(job.coinb2)/2, job.netDiff);
      logMsg("MINE", msg);
      break;
    case EV_JOB_BAD:
      logMsg("POOL", (String("Job unbrauchbar: ")+r.text).c_str());
      break;
    case EV_DIFF:
      snprintf(msg, sizeof(msg), "Diff: %.6g", r.diff);
      logMsg("POOL", msg);
      break;
    case EV_EXTRANONCE:
      logMsg("POOL", "Extranonce geändert, warte auf neuen Job");
      break;
    case EV_SUBSCRIBED: {
      char en1[EXNONCE_MAX*2+1];
      bytesToHex(job.exNonce1, job.exNonce1Len, en1);
      snprintf(msg, sizeof(msg), "EN1=%s sz=%d", en1, job.exNonce2Len);
      logMsg("POOL", msg);
      break;
    }
    case EV_SUBSCRIBE_FAIL:
      logMsg("POOL", (String("Subscribe abgelehnt: ")+r.text).c_str());
      break;
    case EV_AUTH_OK:
      logMsg("POOL", "Authorize OK");
      break;
    case EV_AUTH_FAIL:
      logMsg("POOL", (String("Authorize abgelehnt: ")+r.text).c_str());
      break;
    case EV_SHARE_OK:
      miner.sharesOk++;
      logMsg("POOL", "Share OK");
      break;
    case EV_SHARE_REJ:
      miner.sharesRej++;
      logMsg("POOL", (String("Share REJ: ")+r.text).c_str());
      break;
    case EV_SUGGEST_REPLY:
      if (r.text[0]) logMsg("POOL", (String("Diff-Wunsch abgelehnt: ")+r.text).c_str());
      break;
    case EV_JSON_ERROR:
      logMsg("POOL", "JSON-Fehler");
      break;
    case EV_OTHER:
      logMsg("POOL", (String("Unbekannt: ")+r.text).c_str());
      break;
    case EV_NONE:
      break;
  }
}

void stratumTick() {
  if (!miner.poolConn||!stratumSock.connected()) {
    if (miner.poolConn){
      miner.poolConn=false;
      miningLockJob(); job.valid=false; job.generation++; miningUnlockJob();
      logMsg("POOL","Getrennt");
    }
    uint32_t now=millis();
    if (now-tRetry>15000){tRetry=now;static uint8_t rc=0;rc++;
      if (!stratumConnect(rc%4==0?2:1)) stratumConnect(2);}
    return;
  }
  while (stratumSock.available()) {
    tLastRx=millis();
    char c=(char)stratumSock.read();
    if (c=='\n') {
      stratBuf.trim();
      if (lineTooLong) logMsg("POOL","Zeile zu lang, verworfen");
      else if (stratBuf.length()>0) handleLine(stratBuf);
      stratBuf=""; lineTooLong=false;
    }
    else if (c!='\r') {
      if (stratBuf.length()<LINE_MAX_LEN) stratBuf+=c;
      else { stratBuf=""; lineTooLong=true; }
    }
  }
  ShareMsg share;
  while (miningNextShare(share)) sendShare(share);
  if (millis()-tLastRx>POOL_SILENCE_MS) {
    logMsg("POOL","Keine Daten vom Pool, verbinde neu");
    stratumSock.stop();
  }
}
