#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mining_core.h"

// ================================================================
// STRATUM-PROTOKOLL — ohne Socket und ohne Arduino-Abhängigkeit,
// damit der PC-Test dieselben Funktionen nutzt (tools/mining_test/).
// Antworten werden über die id zugeordnet (E2/E3).
// ================================================================
#define STRATUM_ID_SUBSCRIBE    1
#define STRATUM_ID_AUTHORIZE    2
#define STRATUM_ID_SUGGEST      3
#define STRATUM_ID_SUBMIT_FIRST 100

enum StratumEvent {
  EV_NONE,             // nichts zu melden
  EV_JOB,              // neuer gültiger Job
  EV_JOB_BAD,          // Job unbrauchbar, Grund in text
  EV_DIFF,             // neue Pool-Diff in diff
  EV_EXTRANONCE,       // Extranonce geändert
  EV_SUBSCRIBED,
  EV_SUBSCRIBE_FAIL,
  EV_AUTH_OK,
  EV_AUTH_FAIL,
  EV_SHARE_OK,
  EV_SHARE_REJ,        // Fehlertext des Pools in text
  EV_SUGGEST_REPLY,
  EV_JSON_ERROR,
  EV_OTHER             // unbekannte Methode in text
};

struct StratumReply {
  double diff;
  char   text[96];
};

StratumEvent stratumParseLine(const char* line, StratumJob& job, StratumReply& out);

// Schreibt die mining.submit-Zeile samt '\n'. Rückgabe: Länge, 0 wenn zu lang.
size_t stratumFormatSubmit(char* out, size_t outLen, int id, const char* user, const ShareMsg& share);
