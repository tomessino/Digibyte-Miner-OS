// Rechenkern: Das Arduino-Standard -Os kostet hier messbar Hashrate.
#pragma GCC optimize ("O2")
#include "mining_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <esp_attr.h>
#define MINING_IRAM IRAM_ATTR
#else
#define MINING_IRAM
#endif

// ================================================================
// SHA-256 — aufgerollt, Rundenkonstanten direkt im Code
// ================================================================
#define ROR(x,n)  (((x)>>(n))|((x)<<(32-(n))))
#define S0(x)     (ROR(x,2)^ROR(x,13)^ROR(x,22))
#define S1(x)     (ROR(x,6)^ROR(x,11)^ROR(x,25))
#define s0(x)     (ROR(x,7)^ROR(x,18)^((x)>>3))
#define s1(x)     (ROR(x,17)^ROR(x,19)^((x)>>10))
#define CH(e,f,g)  ((g)^((e)&((f)^(g))))
#define MAJ(a,b,c) (((a)&(b))|((c)&((a)|(b))))

// Runde ohne Nachrichten-Erweiterung (Runde 0-15)
#define RA(a,b,c,d,e,f,g,h,k,i) do { uint32_t t1=(h)+S1(e)+CH(e,f,g)+(k)+W[i]; \
    uint32_t t2=S0(a)+MAJ(a,b,c); (d)+=t1; (h)=t1+t2; } while(0)
// Runde mit rollender Nachrichten-Erweiterung auf 16 Worten (Runde 16-63)
#define RB(a,b,c,d,e,f,g,h,k,i) do { \
    W[(i)&15]+=s1(W[((i)+14)&15])+W[((i)+9)&15]+s0(W[((i)+1)&15]); \
    uint32_t t1=(h)+S1(e)+CH(e,f,g)+(k)+W[(i)&15]; \
    uint32_t t2=S0(a)+MAJ(a,b,c); (d)+=t1; (h)=t1+t2; } while(0)

#define ROUNDS_0_60 \
  RA(a,b,c,d,e,f,g,h,0x428a2f98,0); RA(h,a,b,c,d,e,f,g,0x71374491,1); RA(g,h,a,b,c,d,e,f,0xb5c0fbcf,2); RA(f,g,h,a,b,c,d,e,0xe9b5dba5,3); \
  RA(e,f,g,h,a,b,c,d,0x3956c25b,4); RA(d,e,f,g,h,a,b,c,0x59f111f1,5); RA(c,d,e,f,g,h,a,b,0x923f82a4,6); RA(b,c,d,e,f,g,h,a,0xab1c5ed5,7); \
  RA(a,b,c,d,e,f,g,h,0xd807aa98,8); RA(h,a,b,c,d,e,f,g,0x12835b01,9); RA(g,h,a,b,c,d,e,f,0x243185be,10); RA(f,g,h,a,b,c,d,e,0x550c7dc3,11); \
  RA(e,f,g,h,a,b,c,d,0x72be5d74,12); RA(d,e,f,g,h,a,b,c,0x80deb1fe,13); RA(c,d,e,f,g,h,a,b,0x9bdc06a7,14); RA(b,c,d,e,f,g,h,a,0xc19bf174,15); \
  RB(a,b,c,d,e,f,g,h,0xe49b69c1,16); RB(h,a,b,c,d,e,f,g,0xefbe4786,17); RB(g,h,a,b,c,d,e,f,0x0fc19dc6,18); RB(f,g,h,a,b,c,d,e,0x240ca1cc,19); \
  RB(e,f,g,h,a,b,c,d,0x2de92c6f,20); RB(d,e,f,g,h,a,b,c,0x4a7484aa,21); RB(c,d,e,f,g,h,a,b,0x5cb0a9dc,22); RB(b,c,d,e,f,g,h,a,0x76f988da,23); \
  RB(a,b,c,d,e,f,g,h,0x983e5152,24); RB(h,a,b,c,d,e,f,g,0xa831c66d,25); RB(g,h,a,b,c,d,e,f,0xb00327c8,26); RB(f,g,h,a,b,c,d,e,0xbf597fc7,27); \
  RB(e,f,g,h,a,b,c,d,0xc6e00bf3,28); RB(d,e,f,g,h,a,b,c,0xd5a79147,29); RB(c,d,e,f,g,h,a,b,0x06ca6351,30); RB(b,c,d,e,f,g,h,a,0x14292967,31); \
  RB(a,b,c,d,e,f,g,h,0x27b70a85,32); RB(h,a,b,c,d,e,f,g,0x2e1b2138,33); RB(g,h,a,b,c,d,e,f,0x4d2c6dfc,34); RB(f,g,h,a,b,c,d,e,0x53380d13,35); \
  RB(e,f,g,h,a,b,c,d,0x650a7354,36); RB(d,e,f,g,h,a,b,c,0x766a0abb,37); RB(c,d,e,f,g,h,a,b,0x81c2c92e,38); RB(b,c,d,e,f,g,h,a,0x92722c85,39); \
  RB(a,b,c,d,e,f,g,h,0xa2bfe8a1,40); RB(h,a,b,c,d,e,f,g,0xa81a664b,41); RB(g,h,a,b,c,d,e,f,0xc24b8b70,42); RB(f,g,h,a,b,c,d,e,0xc76c51a3,43); \
  RB(e,f,g,h,a,b,c,d,0xd192e819,44); RB(d,e,f,g,h,a,b,c,0xd6990624,45); RB(c,d,e,f,g,h,a,b,0xf40e3585,46); RB(b,c,d,e,f,g,h,a,0x106aa070,47); \
  RB(a,b,c,d,e,f,g,h,0x19a4c116,48); RB(h,a,b,c,d,e,f,g,0x1e376c08,49); RB(g,h,a,b,c,d,e,f,0x2748774c,50); RB(f,g,h,a,b,c,d,e,0x34b0bcb5,51); \
  RB(e,f,g,h,a,b,c,d,0x391c0cb3,52); RB(d,e,f,g,h,a,b,c,0x4ed8aa4a,53); RB(c,d,e,f,g,h,a,b,0x5b9cca4f,54); RB(b,c,d,e,f,g,h,a,0x682e6ff3,55); \
  RB(a,b,c,d,e,f,g,h,0x748f82ee,56); RB(h,a,b,c,d,e,f,g,0x78a5636f,57); RB(g,h,a,b,c,d,e,f,0x84c87814,58); RB(f,g,h,a,b,c,d,e,0x8cc70208,59); \
  RB(e,f,g,h,a,b,c,d,0x90befffa,60);
#define ROUNDS_61_63 \
  RB(d,e,f,g,h,a,b,c,0xa4506ceb,61); RB(c,d,e,f,g,h,a,b,0xbef9a3f7,62); RB(b,c,d,e,f,g,h,a,0xc67178f2,63);

static const uint32_t SHA_INIT[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

// Verbraucht W (16 Worte)
static inline __attribute__((always_inline)) void compress(uint32_t* st, const uint32_t* in, uint32_t* W) {
  uint32_t a=in[0],b=in[1],c=in[2],d=in[3],e=in[4],f=in[5],g=in[6],h=in[7];
  ROUNDS_0_60
  ROUNDS_61_63
  st[0]=in[0]+a; st[1]=in[1]+b; st[2]=in[2]+c; st[3]=in[3]+d;
  st[4]=in[4]+e; st[5]=in[5]+f; st[6]=in[6]+g; st[7]=in[7]+h;
}

static inline uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

static void sha256Soft(const uint8_t* d, size_t len, uint8_t* out) {
  uint32_t st[8], W[16];
  memcpy(st, SHA_INIT, sizeof(st));
  size_t i = 0;
  for (; i + 64 <= len; i += 64) {
    for (int k=0;k<16;k++) W[k]=be32(d+i+k*4);
    compress(st, st, W);
  }
  uint8_t last[128];
  memset(last, 0, sizeof(last));
  size_t rest = len - i;
  memcpy(last, d+i, rest);
  last[rest] = 0x80;
  size_t total = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int b=0;b<8;b++) last[total-1-b]=(uint8_t)(bits>>(8*b));
  for (size_t o=0;o<total;o+=64) {
    for (int k=0;k<16;k++) W[k]=be32(last+o+k*4);
    compress(st, st, W);
  }
  for (int k=0;k<8;k++) { out[k*4]=st[k]>>24; out[k*4+1]=st[k]>>16; out[k*4+2]=st[k]>>8; out[k*4+3]=st[k]; }
}

void sha256d(const uint8_t* d, size_t len, uint8_t* out) {
  uint8_t tmp[32];
  sha256Soft(d, len, tmp);
  sha256Soft(tmp, 32, out);
}

void midPrepare(const uint8_t* header, MiningMid& m) {
  uint32_t W[16];
  for (int k=0;k<16;k++) W[k]=be32(header+k*4);
  compress(m.mid, SHA_INIT, W);
  for (int k=0;k<3;k++) m.tail[k]=be32(header+64+k*4);
}

// Nur H7 zählt für den Vorfilter. Der zweite Hash spart die letzten 3 Runden:
// Das spätere h steht schon nach Runde 60 fest.
static inline __attribute__((always_inline)) uint32_t oneNonce(const MiningMid& m, uint32_t nonce) {
  uint32_t W[16], d1[8];
  W[0]=m.tail[0]; W[1]=m.tail[1]; W[2]=m.tail[2]; W[3]=__builtin_bswap32(nonce);
  W[4]=0x80000000u; W[5]=W[6]=W[7]=W[8]=W[9]=W[10]=W[11]=W[12]=W[13]=W[14]=0; W[15]=640;
  compress(d1, m.mid, W);
  uint32_t a=SHA_INIT[0],b=SHA_INIT[1],c=SHA_INIT[2],d=SHA_INIT[3],e=SHA_INIT[4],f=SHA_INIT[5],g=SHA_INIT[6],h=SHA_INIT[7];
  W[0]=d1[0];W[1]=d1[1];W[2]=d1[2];W[3]=d1[3];W[4]=d1[4];W[5]=d1[5];W[6]=d1[6];W[7]=d1[7];
  W[8]=0x80000000u; W[9]=W[10]=W[11]=W[12]=W[13]=W[14]=0; W[15]=256;
  ROUNDS_0_60
  (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
  return SHA_INIT[7] + h;   // Runde 60 schreibt das neue e in die Variable h, das nach Runde 63 h ist
}

uint32_t hashNonceH7(const MiningMid& m, uint32_t nonce) { return oneNonce(m, nonce); }

MINING_IRAM uint32_t scanSoftware(const MiningMid& m, uint32_t nonce, uint32_t count, uint32_t* hits, uint32_t maxHits) {
  uint32_t n = 0;
  for (uint32_t i=0; i<count; i++, nonce++) {
    if ((oneNonce(m, nonce) & H7_FILTER_MASK) == 0 && n < maxHits) hits[n++] = nonce;
  }
  return n;
}

// ================================================================
// HILFSFUNKTIONEN
// ================================================================
size_t hexToBytes(const char* hex, uint8_t* out, size_t maxOut) {
  auto nib = [](char c) -> uint8_t {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return 0;
  };
  size_t n = strnlen(hex, maxOut*2+2) / 2;
  if (n > maxOut) n = maxOut;
  for (size_t i=0; i<n; i++)
    out[i] = (nib(hex[i*2])<<4)|nib(hex[i*2+1]);
  return n;
}

void bytesToHex(const uint8_t* in, size_t len, char* out) {
  static const char H[] = "0123456789abcdef";
  for (size_t i=0; i<len; i++) { out[i*2]=H[in[i]>>4]; out[i*2+1]=H[in[i]&15]; }
  out[len*2]='\0';
}

void writeLE32(uint8_t* buf, uint32_t v) {
  buf[0]=v; buf[1]=v>>8; buf[2]=v>>16; buf[3]=v>>24;
}

// prevHash: 8x 4-Byte Wörter, jedes Wort byte-swappen
static void swapPrevHash(const uint8_t* in, uint8_t* out) {
  for (int i=0; i<8; i++) {
    int b = i*4;
    out[b+0]=in[b+3]; out[b+1]=in[b+2]; out[b+2]=in[b+1]; out[b+3]=in[b+0];
  }
}

// ================================================================
// DIFF
// diff = (0xFFFF * 2^208) / Hash als 256-Bit-Zahl.
// Double reicht für Anzeige und Schwellenvergleich, der Pool prüft exakt.
// ================================================================
double hashToDiff(const uint8_t* hash) {
  int i = 31;
  while (i >= 0 && hash[i] == 0) i--;
  if (i < 0) return 1e300;  // Hash ist 0
  double m = 0.0;
  int used = 0;
  for (; i >= 0 && used < 8; i--, used++) m = m * 256.0 + hash[i];
  return ldexp(65535.0 / m, 208 - 8 * (i + 1));
}

double netDiffFromBits(const char* hex) {
  if (!hex || !*hex) return 0;
  uint32_t bits = (uint32_t)strtoul(hex, nullptr, 16);
  uint32_t man = bits & 0xFFFFFF;
  int exp = (int)(bits >> 24);
  if (!man) return 0;
  return 65535.0 / (double)man * pow(256.0, 0x1d - exp);
}

// ================================================================
// MERKLE ROOT
// Statischer Puffer statt malloc: läuft bei jedem Job und Nonce-Überlauf.
// ================================================================
bool computeMerkleRoot(const StratumJob& job, uint8_t* merkleRoot) {
  static uint8_t coinbase[COINB_HEX_MAX + 2*EXNONCE_MAX];
  size_t cb1len = strnlen(job.coinb1, sizeof(job.coinb1)) / 2;
  size_t cb2len = strnlen(job.coinb2, sizeof(job.coinb2)) / 2;
  if (cb1len + job.exNonce1Len + job.exNonce2Len + cb2len > sizeof(coinbase)) return false;

  size_t pos = 0;
  hexToBytes(job.coinb1, coinbase+pos, cb1len); pos += cb1len;
  memcpy(coinbase+pos, job.exNonce1, job.exNonce1Len); pos += job.exNonce1Len;
  memcpy(coinbase+pos, job.exNonce2, job.exNonce2Len); pos += job.exNonce2Len;
  hexToBytes(job.coinb2, coinbase+pos, cb2len); pos += cb2len;
  sha256d(coinbase, pos, merkleRoot);

  for (int i=0; i<job.merkleBranchCount; i++) {
    uint8_t combined[64];
    memcpy(combined, merkleRoot, 32);
    hexToBytes(job.merkleBranch[i], combined+32, 32);
    sha256d(combined, 64, merkleRoot);
  }
  return true;
}

bool buildHeader(const StratumJob& job, uint8_t* header) {
  memset(header, 0, 80);
  writeLE32(header+0, (uint32_t)strtoul(job.version, nullptr, 16));
  uint8_t prevRaw[32] = {};
  if (hexToBytes(job.prevHash, prevRaw, 32) != 32) return false;
  swapPrevHash(prevRaw, header+4);
  if (!computeMerkleRoot(job, header+36)) return false;
  writeLE32(header+68, (uint32_t)strtoul(job.ntime, nullptr, 16));
  writeLE32(header+72, (uint32_t)strtoul(job.nbits, nullptr, 16));
  return true;
}

void setHeaderNonce(uint8_t* header, uint32_t nonce) { writeLE32(header+76, nonce); }

void fillShare(ShareMsg& out, const StratumJob& job, uint32_t nonce) {
  strncpy(out.id, job.id, sizeof(out.id)-1); out.id[sizeof(out.id)-1]=0;
  bytesToHex(job.exNonce2, job.exNonce2Len, out.en2);
  strncpy(out.ntime, job.ntime, sizeof(out.ntime)-1); out.ntime[sizeof(out.ntime)-1]=0;
  out.nonce = nonce;
}

// Zähler über alle Extranonce2-Bytes. Das erste Byte zählt am schnellsten.
void incExNonce2(StratumJob& job) {
  for (int i=0; i<job.exNonce2Len; i++) {
    if (++job.exNonce2[i] != 0) break;
  }
  job.generation++;
}

// ================================================================
// SELBSTTEST (Plan 1.9) — Sollwerte aus tools/test_hash_vektoren.py
// ================================================================
bool miningSelfTest(char* out, size_t outLen) {
  uint8_t header[80];
  hexToBytes("01000000"
             "0000000000000000000000000000000000000000000000000000000000000000"
             "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
             "29ab5f49ffff001d1dac2b7c", header, 80);
  uint8_t hash[32];
  sha256d(header, 80, hash);
  uint8_t disp[32];
  for (int i=0; i<32; i++) disp[i] = hash[31-i];
  char hex[65];
  bytesToHex(disp, 32, hex);
  double d = hashToDiff(hash);
  double nd = netDiffFromBits("1d00ffff");
  bool ok = strcmp(hex, "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") == 0
         && hash[31] == 0 && d > 2536.0 && d < 2537.0 && fabs(nd - 1.0) < 1e-9;
  snprintf(out, outLen, "SELFTEST hash=%s diff=%.2f netdiff=%.1f %s", hex, d, nd, ok ? "OK" : "FAIL");
  return ok;
}
