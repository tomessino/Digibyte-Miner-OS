#include "stratum_core.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

// Kopiert und meldet, ob alles gepasst hat. Abgeschnittene Felder ergäben
// einen falschen Merkle-Root, der nur beim Pool auffiele (M8).
static bool copyField(char* dst, size_t n, const char* src) {
  size_t len = strlen(src);
  if (len >= n) { dst[0] = 0; return false; }
  memcpy(dst, src, len + 1);
  return true;
}

static void errorText(JsonVariantConst err, char* out, size_t n) {
  if (err.is<JsonArrayConst>()) {
    JsonArrayConst a = err.as<JsonArrayConst>();
    snprintf(out, n, "%d %s", a[0] | 0, a[1] | "");
  } else if (err.is<const char*>()) {
    snprintf(out, n, "%s", err.as<const char*>());
  } else if (err.is<JsonObjectConst>()) {
    JsonObjectConst o = err.as<JsonObjectConst>();
    snprintf(out, n, "%d %s", o["code"] | 0, o["message"] | "");
  } else {
    snprintf(out, n, "ohne Fehlertext");
  }
}

static bool setExtranonce(StratumJob& job, const char* en1, int en2sz) {
  size_t len1 = strlen(en1) / 2;
  if (len1 > EXNONCE_MAX || en2sz < 1 || en2sz > EXNONCE_MAX) return false;
  hexToBytes(en1, job.exNonce1, len1);
  job.exNonce1Len = (uint8_t)len1;
  job.exNonce2Len = (uint8_t)en2sz;
  memset(job.exNonce2, 0, sizeof(job.exNonce2));
  job.haveExtranonce = true;
  job.generation++;
  return true;
}

// BIP34: Hinter Version (4), Input-Anzahl (1), Prevout (36) und Skriptlänge (1)
// steht die Blockhöhe als Push: ein Längenbyte, dann little-endian.
static uint32_t heightFromCoinb1(const char* hex) {
  if (strlen(hex) < 88) return 0;
  uint8_t head[47] = {};
  size_t n = hexToBytes(hex, head, sizeof(head));
  uint8_t push = head[42];
  if (push < 1 || push > 4 || n < (size_t)43 + push) return 0;
  uint32_t h = 0;
  for (int i = 0; i < push; i++) h |= (uint32_t)head[43 + i] << (8 * i);
  return h;
}

static StratumEvent parseNotify(JsonArrayConst p, StratumJob& job, StratumReply& out) {
  job.valid = false;
  if (p.isNull() || p.size() < 9) {
    snprintf(out.text, sizeof(out.text), "notify mit %d Feldern", (int)p.size());
    return EV_JOB_BAD;
  }
  bool ok = copyField(job.id, sizeof(job.id), p[0] | "")
         && copyField(job.prevHash, sizeof(job.prevHash), p[1] | "");
  if (!ok) { snprintf(out.text, sizeof(out.text), "Job-ID oder PrevHash zu lang"); return EV_JOB_BAD; }
  if (!copyField(job.coinb1, sizeof(job.coinb1), p[2] | "")
   || !copyField(job.coinb2, sizeof(job.coinb2), p[3] | "")) {
    snprintf(out.text, sizeof(out.text), "Coinbase zu lang: %d und %d Zeichen, Puffer %d",
             (int)strlen(p[2] | ""), (int)strlen(p[3] | ""), COINB_HEX_MAX);
    return EV_JOB_BAD;
  }
  JsonArrayConst br = p[4];
  if (br.size() > MAX_MERKLE_BRANCHES) {
    snprintf(out.text, sizeof(out.text), "%d Merkle-Zweige, Puffer %d", (int)br.size(), MAX_MERKLE_BRANCHES);
    return EV_JOB_BAD;
  }
  job.merkleBranchCount = 0;
  for (JsonVariantConst b : br) {
    if (!copyField(job.merkleBranch[job.merkleBranchCount], 74, b | "")) {
      snprintf(out.text, sizeof(out.text), "Merkle-Zweig zu lang");
      return EV_JOB_BAD;
    }
    job.merkleBranchCount++;
  }
  if (!copyField(job.version, sizeof(job.version), p[5] | "")
   || !copyField(job.nbits, sizeof(job.nbits), p[6] | "")
   || !copyField(job.ntime, sizeof(job.ntime), p[7] | "")
   || !job.ntime[0]) {
    snprintf(out.text, sizeof(out.text), "Version, nbits oder ntime ungültig");
    return EV_JOB_BAD;
  }
  job.cleanJobs = p[8] | false;
  if (job.cleanJobs) memset(job.exNonce2, 0, sizeof(job.exNonce2));
  job.netDiff = netDiffFromBits(job.nbits);
  job.height = heightFromCoinb1(job.coinb1);
  job.generation++;
  if (!job.haveExtranonce) {
    snprintf(out.text, sizeof(out.text), "Job vor Subscribe-Antwort");
    return EV_JOB_BAD;
  }
  job.valid = true;
  return EV_JOB;
}

StratumEvent stratumParseLine(const char* line, StratumJob& job, StratumReply& out) {
  out.diff = 0;
  out.text[0] = 0;
  JsonDocument doc;
  if (deserializeJson(doc, line)) return EV_JSON_ERROR;

  const char* method = doc["method"] | "";
  if (method[0]) {
    JsonArrayConst p = doc["params"];
    if (strcmp(method, "mining.notify") == 0) return parseNotify(p, job, out);
    if (strcmp(method, "mining.set_difficulty") == 0) {
      double d = p[0] | 0.0;
      if (d <= 0) return EV_NONE;
      out.diff = d;
      return EV_DIFF;
    }
    if (strcmp(method, "mining.set_extranonce") == 0) {
      job.valid = false;
      if (!setExtranonce(job, p[0] | "", p[1] | 0)) {
        snprintf(out.text, sizeof(out.text), "set_extranonce ungültig");
        return EV_JOB_BAD;
      }
      return EV_EXTRANONCE;
    }
    snprintf(out.text, sizeof(out.text), "%s", method);
    return EV_OTHER;
  }

  if (!doc["id"].is<int>()) return EV_NONE;
  int id = doc["id"].as<int>();
  JsonVariantConst result = doc["result"];
  JsonVariantConst error = doc["error"];

  if (id == STRATUM_ID_SUBSCRIBE) {
    JsonArrayConst r = result.as<JsonArrayConst>();
    if (r.size() < 3 || !setExtranonce(job, r[1] | "", r[2] | 0)) {
      errorText(error, out.text, sizeof(out.text));
      return EV_SUBSCRIBE_FAIL;
    }
    return EV_SUBSCRIBED;
  }
  if (id == STRATUM_ID_AUTHORIZE) {
    if (result.is<bool>() && result.as<bool>()) return EV_AUTH_OK;
    errorText(error, out.text, sizeof(out.text));
    return EV_AUTH_FAIL;
  }
  if (id == STRATUM_ID_SUGGEST) {
    if (!error.isNull()) errorText(error, out.text, sizeof(out.text));
    return EV_SUGGEST_REPLY;
  }
  if (id >= STRATUM_ID_SUBMIT_FIRST) {
    if (result.is<bool>() && result.as<bool>()) return EV_SHARE_OK;
    errorText(error, out.text, sizeof(out.text));
    return EV_SHARE_REJ;
  }
  return EV_NONE;
}

size_t stratumFormatSubmit(char* out, size_t outLen, int id, const char* user, const ShareMsg& share) {
  int n = snprintf(out, outLen,
    "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%08lx\"]}\n",
    id, user, share.id, share.en2, share.ntime, (unsigned long)share.nonce);
  return (n > 0 && (size_t)n < outLen) ? (size_t)n : 0;
}
