// PC-Test des Mining-Kerns: nutzt mining_core.cpp und stratum_core.cpp der Firmware.
// Liest Pool-Nachrichten von stdin, schuerft bei jedem Job und schreibt Ereignisse und
// mining.submit-Zeilen nach stdout. check_mining.py spielt den Pool und prueft nach.
//
//   host_mining selftest
//   host_mining diff <hash-hex>
//   host_mining netdiff <nbits>
//   host_mining pool <startnonce-hex> <max-hashes>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mining_core.h"
#include "stratum_core.h"

// ----------------------------------------------------------------
static const char* evName(StratumEvent e) {
  switch (e) {
    case EV_NONE: return "NONE"; case EV_JOB: return "JOB"; case EV_JOB_BAD: return "JOB_BAD";
    case EV_DIFF: return "DIFF"; case EV_EXTRANONCE: return "EXTRANONCE"; case EV_SUBSCRIBED: return "SUBSCRIBED";
    case EV_SUBSCRIBE_FAIL: return "SUBSCRIBE_FAIL"; case EV_AUTH_OK: return "AUTH_OK"; case EV_AUTH_FAIL: return "AUTH_FAIL";
    case EV_SHARE_OK: return "SHARE_OK"; case EV_SHARE_REJ: return "SHARE_REJ"; case EV_SUGGEST_REPLY: return "SUGGEST_REPLY";
    case EV_JSON_ERROR: return "JSON_ERROR"; case EV_OTHER: return "OTHER";
  }
  return "?";
}

static StratumJob job;
static char lineBuf[8192];

// Dieselbe Pruefung wie handleHit() in mining.cpp
static void mine(double poolDiff, uint32_t nonce, unsigned long maxHashes, const char* user) {
  uint8_t header[80];
  if (!buildHeader(job, header)) { printf("HEADER_FEHLER\n"); return; }
  char hx[161];
  bytesToHex(header, 80, hx);
  printf("HEADER %s\n", hx);
  MiningMid mid;
  midPrepare(header, mid);
  static int submitId = STRATUM_ID_SUBMIT_FIRST;
  unsigned long done = 0;
  while (done < maxHashes) {
    uint32_t hits[16];
    uint32_t cnt = maxHashes - done < 1000 ? (uint32_t)(maxHashes - done) : 1000;
    uint32_t n = scanSoftware(mid, nonce, cnt, hits, 16);
    for (uint32_t i = 0; i < n; i++) {
      setHeaderNonce(header, hits[i]);
      uint8_t hash[32];
      sha256d(header, 80, hash);
      double d = hashToDiff(hash);
      if (d >= poolDiff) {
        ShareMsg s;
        fillShare(s, job, hits[i]);
        char line[512];
        if (stratumFormatSubmit(line, sizeof(line), submitId++, user, s)) printf("SUBMIT %.12g %s", d, line);
        return;
      }
    }
    nonce += cnt;
    done += cnt;
  }
  printf("KEIN_TREFFER\n");
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (!strcmp(argv[1], "selftest")) {
    char out[200];
    bool ok = miningSelfTest(out, sizeof(out));
    printf("%s\n", out);
    return ok ? 0 : 1;
  }
  if (!strcmp(argv[1], "diff")) {
    uint8_t h[32];
    hexToBytes(argv[2], h, 32);
    printf("%.17g\n", hashToDiff(h));
    return 0;
  }
  if (!strcmp(argv[1], "h7")) {
    uint8_t h[80];
    hexToBytes(argv[2], h, 80);
    MiningMid m;
    midPrepare(h, m);
    printf("%08x\n", hashNonceH7(m, (uint32_t)strtoul(argv[3], nullptr, 16)));
    return 0;
  }
  if (!strcmp(argv[1], "scan")) {
    uint8_t h[80];
    hexToBytes(argv[2], h, 80);
    MiningMid m;
    midPrepare(h, m);
    uint32_t hits[16];
    uint32_t n = scanSoftware(m, (uint32_t)strtoul(argv[3], nullptr, 16), (uint32_t)strtoul(argv[4], nullptr, 10), hits, 16);
    for (uint32_t i = 0; i < n; i++) printf("%08x\n", hits[i]);
    return 0;
  }
  if (!strcmp(argv[1], "netdiff")) { printf("%.17g\n", netDiffFromBits(argv[2])); return 0; }
  if (!strcmp(argv[1], "pool")) {
    uint32_t start = (uint32_t)strtoul(argv[2], nullptr, 16);
    unsigned long maxHashes = strtoul(argv[3], nullptr, 10);
    double poolDiff = 1;
    while (fgets(lineBuf, sizeof(lineBuf), stdin)) {
      static StratumReply r;
      if (!strcmp(lineBuf, "INC_EN2\n")) { incExNonce2(job); char e[33]; bytesToHex(job.exNonce2, job.exNonce2Len, e); printf("EN2 %s\n", e); continue; }
      StratumEvent ev = stratumParseLine(lineBuf, job, r);
      printf("EV %s%s%s\n", evName(ev), r.text[0] ? " " : "", r.text);
      if (ev == EV_DIFF) poolDiff = r.diff;
      if (ev == EV_JOB) {
        printf("NETDIFF %.6f\n", job.netDiff);
        printf("HEIGHT %u\n", job.height);
        mine(poolDiff, start, maxHashes, "wallet.worker");
      }
      fflush(stdout);
    }
    return 0;
  }
  return 2;
}
