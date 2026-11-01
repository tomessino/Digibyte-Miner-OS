#pragma once
#include <stdint.h>
#include <stddef.h>

// ================================================================
// MINING-KERN — ohne Arduino-Abhängigkeit, damit dieselben Funktionen
// auf dem PC gegen Testvektoren laufen (tools/mining_test/).
// ================================================================
#define MAX_MERKLE_BRANCHES 16
#define COINB_HEX_MAX       1024   // je Coinbase-Teil, als Hex-Zeichen (512 Byte)
#define EXNONCE_MAX         16     // Byte

// ================================================================
// STRATUM JOB
// Extranonce als Bytes in Pool-Reihenfolge: Coinbase und Submit nutzen
// dieselben Bytes (E1/H2).
// ================================================================
struct StratumJob {
  char     id[34]        = "";
  char     prevHash[74]  = "";   // vom Pool: je 4-Byte-Wort vertauscht
  char     coinb1[COINB_HEX_MAX] = "";
  char     coinb2[COINB_HEX_MAX] = "";
  char     merkleBranch[MAX_MERKLE_BRANCHES][74] = {};
  int      merkleBranchCount = 0;
  char     version[12]   = "00000001";
  char     nbits[12]     = "1d00ffff";
  char     ntime[12]     = "";
  uint8_t  exNonce1[EXNONCE_MAX] = {};
  uint8_t  exNonce1Len   = 0;
  uint8_t  exNonce2[EXNONCE_MAX] = {};
  uint8_t  exNonce2Len   = 0;
  bool     haveExtranonce = false;
  uint32_t generation    = 0;    // steigt bei neuem Job oder neuem Extranonce
  double   netDiff       = 0;    // aus nbits, pro Job
  uint32_t height        = 0;    // aus der Coinbase (BIP34), 0 wenn nicht lesbar
  bool     valid         = false;
  bool     cleanJobs     = false;
};

// Eine gefundene Lösung, vom Miner-Task an den Task mit dem Socket
struct ShareMsg {
  char     id[34];
  char     en2[EXNONCE_MAX*2+1];
  char     ntime[12];
  uint32_t nonce;
  double   diff;
};

void sha256d(const uint8_t* d, size_t len, uint8_t* out);

// Software-SHA-256 mit Midstate: Der erste Header-Block ist pro Job fest.
// Die Worte sind die numerischen SHA-Worte (big-endian gelesen).
struct MiningMid {
  uint32_t mid[8];    // Zustand nach dem ersten 64-Byte-Block
  uint32_t tail[3];   // Merkle-Ende, ntime, nbits aus dem zweiten Block
};

// Vorfilter auf dem letzten Hash-Wort H7: hash[31] und hash[30] sind die niedrigen
// 16 Bit. Null heißt Diff >= 2^-16 (ca. 1,5e-5), weit unter jeder Pool-Diff.
#define H7_FILTER_MASK 0xFFFFu

void     midPrepare(const uint8_t* header80, MiningMid& m);
uint32_t hashNonceH7(const MiningMid& m, uint32_t nonce);
// Prüft count Nonces ab nonce. Schreibt Treffer des Vorfilters, Rückgabe: Anzahl.
uint32_t scanSoftware(const MiningMid& m, uint32_t nonce, uint32_t count, uint32_t* hits, uint32_t maxHits);

size_t hexToBytes(const char* hex, uint8_t* out, size_t maxOut);
void   bytesToHex(const uint8_t* in, size_t len, char* out);
void   writeLE32(uint8_t* buf, uint32_t v);

// Rohes SHA-256d-Ergebnis ist little-endian: Das höchstwertige Byte ist
// hash[31]. Eine Share-Diff ab 2^-24 setzt hash[31]==0 voraus (Vorfilter).
double hashToDiff(const uint8_t* hash);
double netDiffFromBits(const char* nbitsHex);

bool   computeMerkleRoot(const StratumJob& job, uint8_t* merkleRoot);
bool   buildHeader(const StratumJob& job, uint8_t* header80);
void   setHeaderNonce(uint8_t* header80, uint32_t nonce);
void   fillShare(ShareMsg& out, const StratumJob& job, uint32_t nonce);
void   incExNonce2(StratumJob& job);

// Hasht den Genesis-Header. Schreibt "SELFTEST hash=… diff=… netdiff=… OK|FAIL".
bool   miningSelfTest(char* out, size_t outLen);
