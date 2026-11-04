#include "mining.h"
#include <mbedtls/sha256.h>
#include <sha/sha_parallel_engine.h>
#include <soc/hwcrypto_reg.h>
#include <esp_system.h>
#include <math.h>
#include <xtensa/core-macros.h>
#include <freertos/queue.h>
#include "config.h"
#include "stratum.h"
#include "mining_core.h"

MinerState miner;

// ================================================================
// TEMPERATUR (interner ESP32-Sensor)
// ================================================================
// Der Sensor liefert oft 53,33 °C (Rohwert 128): Das Auslesen hat dann nicht geklappt, etwa
// weil das WLAN den ADC belegt. Diese Werte zählen nicht, der Rest läuft über einen Median.
// Der Wert ist die Chip-Temperatur ohne Abgleich, also nur grob.
float readTemp() {
#if CONFIG_IDF_TARGET_ESP32
  static float last[9];
  static int count = 0, pos = 0;
  for (int i = 0; i < 3; i++) {
    float t = temperatureRead();
    if (fabs(t - 53.33f) < 0.05f || t < -10.0f || t > 125.0f) continue;
    last[pos] = t;
    pos = (pos + 1) % 9;
    if (count < 9) count++;
  }
  if (!count) return 0.0f;
  float sorted[9];
  memcpy(sorted, last, sizeof(float) * count);
  for (int i = 1; i < count; i++) {
    float v = sorted[i]; int j = i - 1;
    while (j >= 0 && sorted[j] > v) { sorted[j+1] = sorted[j]; j--; }
    sorted[j+1] = v;
  }
  return sorted[count / 2];
#else
  return 0.0f;
#endif
}

// ================================================================
// ÜBERGABE ZWISCHEN DEN TASKS
// job gehört dem App-Task (Socket, Kern 0). Jeder Miner rechnet auf einer
// eigenen Kopie. Lösungen laufen über eine Queue zurück.
// ================================================================
static SemaphoreHandle_t jobMutex = nullptr;
static QueueHandle_t     shareQueue = nullptr;
static portMUX_TYPE      statMux = portMUX_INITIALIZER_UNLOCKED;

static volatile int      backendMode = BK_HW;
static volatile bool     core0Mode = true;
static bool              hwUsable = false;
static volatile bool     hwTimed = false;   // feste Wartezeiten statt Pollen
static volatile bool     bestDirty = false;
static volatile uint32_t workerHashes[2] = {0, 0};

void miningLockJob()   { if (jobMutex) xSemaphoreTake(jobMutex, portMAX_DELAY); }
void miningUnlockJob() { if (jobMutex) xSemaphoreGive(jobMutex); }
bool miningNextShare(ShareMsg& out) { return shareQueue && xQueueReceive(shareQueue, &out, 0) == pdTRUE; }

void miningSetMode(int backend, bool core0) {
  if (backend == BK_HW && !hwUsable) backend = BK_SOFT;
  backendMode = backend;
  core0Mode = core0;
}
int  miningBackend() { return backendMode; }
bool miningCore0()   { return core0Mode; }
bool miningHwTimed() { return hwTimed; }
// Diagnose: Takte je Hash x100, in der Schleife und von Runde zu Runde (mit allem dazwischen)
static volatile uint32_t hwCycLoop = 0, hwCycAll = 0;
uint32_t miningHwCycLoop() { return hwCycLoop; }
uint32_t miningHwCycAll()  { return hwCycAll; }

// ================================================================
// DIREKTER ZUGRIFF AUF DIE SHA-HARDWARE (Plan 2.4)
// Die Hardware kann ihren Zustand nur auslesen, nicht laden. Pro Nonce also drei
// Blöcke, kein Midstate. Der Text-Speicher enthält nach LOAD das Ergebnis, und das
// sind schon die ersten 8 Worte des zweiten Hashs. Das spart Kopieren.
// ================================================================
// Register direkt per s32i: volatile setzte vor jeden Zugriff ein memw, das kostet hier am meisten.
// memw steht nur, wo die Reihenfolge zählt: vor dem Warten und vor dem Lesen.
#define HW_OFF_START    0x90
#define HW_OFF_CONTINUE 0x94
#define HW_OFF_LOAD     0x98
#define HW_OFF_BUSY     0x9c
#define HW_W(base,off,v) __asm__ __volatile__("s32i %0, %1, %2" : : "r"(v), "r"(base), "i"(off) : "memory")
#define HW_MEMW()        __asm__ __volatile__("memw" : : : "memory")

#define HW_R(var,base,off) __asm__ __volatile__("l32i %0, %1, %2" : "=r"(var) : "r"(base), "i"(off) : "memory")

#define HW_GO(base,reg) do { uint32_t busy; HW_W(base,reg,one); HW_MEMW(); do { HW_R(busy,base,HW_OFF_BUSY); } while (busy); } while (0)
#define HW_KICK(base,reg) do { HW_W(base,reg,one); HW_MEMW(); } while (0)
#define HW_WAIT(base)     do { uint32_t busy; HW_MEMW(); do { HW_R(busy,base,HW_OFF_BUSY); } while (busy); } while (0)
#define HW_CC(var)        __asm__ __volatile__("rsr %0, ccount" : "=r"(var))
#define HW_KICKT(base,reg,t0) do { HW_W(base,reg,one); HW_MEMW(); HW_CC(t0); } while (0)
#define HW_UNTIL(t0,d)    do { uint32_t c_; do { HW_CC(c_); } while ((int32_t)(c_-(t0)-(d))<0); } while (0)

// Feste Wartezeiten bei 240 MHz, gezählt ab dem memw nach dem Startbefehl. Gemessen am 03.11.2026 mit
// Mess-Sketches im IRAM, Mindestwerte in Klammern. Die Schleife muss im IRAM laufen: Aus dem Flash-Cache
// hängt die Dauer der NOPs von der Code-Ausrichtung ab.
// Block 1 und 3: ccount, weil die CPU dort nebenbei schreibt. Darunter schleichend falsch (52 fiel im
// Selbsttest einmal durch, 50 bis 48: 3 bis 12 von 1024 falsch).
#define HW_T_BLOCK  54
// Reine Wartestellen: exakte NOPs, Grenzen scharf (darunter fast jeder Hash falsch). Zu frühes Schreiben ist
// das einzige Risiko, Busverkehr macht die CPU nur später. Deshalb hier je 1 Takt Reserve.
#define HW_N_PADLEN 9    // CONTINUE: erst danach T[8]/T[15] für Block 3 schreiben (8)
#define HW_N_CONT   48   // CONTINUE: Rest nach pad/len (PADLEN + CONT mindestens 56)
#define HW_N_LOAD1  5    // LOAD vor dem Start von Block 3 (4 oder weniger)
#define HW_N_LOAD2W 5    // LOAD, danach schon b[0..6] für den nächsten Nonce schreiben (3, darunter Rennen)
#define HW_NOPS(n)  __asm__ __volatile__(".rept %c0\n nop\n .endr" :: "i"(n) : "memory")

// Ein Nonce: drei Blöcke. Liefert H7. T[9..14] bleiben aus dem zweiten Block null,
// das prüft der Selbsttest über aufeinanderfolgende Nonces.
// START übernimmt den Text während des memw, danach darf der nächste Text geschrieben werden
// (T[15] erst 2 Takte später). CONTINUE liest T[8] und T[15] erst bis rund 8 Takte nach dem memw.
// Ein LOAD schreibt nur T[0..7], T[0..6] nach rund 3 Takten, T[7] nach rund 8.
// Gepollte Fassung: Block 2 wird geschrieben, während Block 1 rechnet.
static inline __attribute__((always_inline)) uint32_t hwOneNonce(uint32_t* T, const uint32_t* b, const uint32_t* tail, uint32_t nonce) {
  const uint32_t one=1, zero=0, pad2=0x80000000u, len1=640, len2=256;
  HW_W(T,0,b[0]);  HW_W(T,4,b[1]);  HW_W(T,8,b[2]);  HW_W(T,12,b[3]);
  HW_W(T,16,b[4]); HW_W(T,20,b[5]); HW_W(T,24,b[6]); HW_W(T,28,b[7]);
  HW_W(T,32,b[8]); HW_W(T,36,b[9]); HW_W(T,40,b[10]);HW_W(T,44,b[11]);
  HW_W(T,48,b[12]);HW_W(T,52,b[13]);HW_W(T,56,b[14]);HW_W(T,60,b[15]);
  HW_KICK(T,HW_OFF_START);
  uint32_t nb=__builtin_bswap32(nonce);
  HW_W(T,0,tail[0]); HW_W(T,4,tail[1]); HW_W(T,8,tail[2]); HW_W(T,12,nb);
  HW_W(T,16,pad2);
  HW_W(T,20,zero); HW_W(T,24,zero); HW_W(T,28,zero); HW_W(T,32,zero); HW_W(T,36,zero);
  HW_W(T,40,zero); HW_W(T,44,zero); HW_W(T,48,zero); HW_W(T,52,zero); HW_W(T,56,zero);
  HW_W(T,60,len1);
  HW_WAIT(T);
  HW_GO(T,HW_OFF_CONTINUE);
  HW_GO(T,HW_OFF_LOAD);
  HW_W(T,32,pad2); HW_W(T,60,len2);
  HW_GO(T,HW_OFF_START);
  HW_GO(T,HW_OFF_LOAD);
  uint32_t h7;
  HW_R(h7,T,28);
  return h7;
}

// Wartet, bis keine SHA-Art mehr rechnet (SHA-1/256/384/512, Busy-Register 0x8c bis 0xbc).
// Nachbau von esp_sha_wait_idle(), das die Arduino-Bibliothek nicht exportiert. Nach dem Sperren
// des Speicherblocks aufrufen: Die Sperre verhindert nur neue Starts, keine laufende Rechnung.
static inline void hwWaitIdle() {
  uint32_t* T = (uint32_t*)SHA_TEXT_BASE;
  uint32_t b1, b2, b3, b4;
  HW_MEMW();
  do { HW_R(b1,T,0x8c); HW_R(b2,T,0x9c); HW_R(b3,T,0xac); HW_R(b4,T,0xbc); } while (b1|b2|b3|b4);
}

// Hintere Hälfte von Block 1. Bleibt über den LOAD stehen.
static inline __attribute__((always_inline)) void hwPrime(uint32_t* T, const uint32_t* b) {
  HW_W(T,32,b[8]); HW_W(T,36,b[9]); HW_W(T,40,b[10]);HW_W(T,44,b[11]);
  HW_W(T,48,b[12]);HW_W(T,52,b[13]);HW_W(T,56,b[14]);HW_W(T,60,b[15]);
}

// Ganzer Block 1, vor der ersten Runde.
static inline __attribute__((always_inline)) void hwPrimeAll(uint32_t* T, const uint32_t* b) {
  HW_W(T,0,b[0]);  HW_W(T,4,b[1]);  HW_W(T,8,b[2]);  HW_W(T,12,b[3]);
  HW_W(T,16,b[4]); HW_W(T,20,b[5]); HW_W(T,24,b[6]); HW_W(T,28,b[7]);
  hwPrime(T,b);
}

// Fassung mit festen Wartezeiten statt Pollen. Erwartet T[0..15] = Block 1 und hinterlässt das wieder:
// Die hintere Hälfte wird geschrieben, während Block 3 rechnet, die vordere während des letzten LOAD.
// w3 ist das Textwort T[3], also der Nonce byte-vertauscht. Die Schleife zählt w3 hoch statt des Nonce:
// Die Block-1-Phase ist CPU-begrenzt, so entfällt das Vertauschen dort. Gleicher Nonce-Raum, andere Reihenfolge.
static inline __attribute__((always_inline)) uint32_t hwOneNonceTimed(uint32_t* T, const uint32_t* b, const uint32_t* tail, uint32_t w3) {
  const uint32_t one=1, zero=0, pad2=0x80000000u, len1=640, len2=256;
  uint32_t t0;
  HW_KICKT(T,HW_OFF_START,t0);
  HW_W(T,0,tail[0]); HW_W(T,4,tail[1]); HW_W(T,8,tail[2]); HW_W(T,12,w3);
  HW_W(T,16,pad2);
  HW_W(T,20,zero); HW_W(T,24,zero); HW_W(T,28,zero); HW_W(T,32,zero); HW_W(T,36,zero);
  HW_W(T,40,zero); HW_W(T,44,zero); HW_W(T,48,zero); HW_W(T,52,zero); HW_W(T,56,zero);
  HW_W(T,60,len1);
  HW_UNTIL(t0,HW_T_BLOCK);
  HW_KICK(T,HW_OFF_CONTINUE);
  HW_NOPS(HW_N_PADLEN);
  HW_W(T,32,pad2); HW_W(T,60,len2);   // CONTINUE hat T[8] und T[15] dann gelesen
  HW_NOPS(HW_N_CONT);
  HW_KICK(T,HW_OFF_LOAD);
  HW_NOPS(HW_N_LOAD1);
  HW_KICKT(T,HW_OFF_START,t0);
  HW_NOPS(4);
  hwPrime(T,b);
  HW_UNTIL(t0,HW_T_BLOCK);
  HW_KICK(T,HW_OFF_LOAD);
  HW_NOPS(HW_N_LOAD2W);
  // Block 1 des nächsten Nonce schon jetzt: LOAD hat T[0..6] dann geschrieben, T[7] kommt nach dem Lesen
  HW_W(T,0,b[0]);  HW_W(T,4,b[1]);  HW_W(T,8,b[2]);  HW_W(T,12,b[3]);
  HW_W(T,16,b[4]); HW_W(T,20,b[5]); HW_W(T,24,b[6]);
  uint32_t h7;
  HW_R(h7,T,28);
  HW_W(T,28,b[7]);
  return h7;
}

// Liefert die Zahl der Treffer, in *last das H7 des letzten Nonce (für die laufende Prüfung).
// TIMED: nonce ist der Startwert von w3 (siehe hwOneNonceTimed), Treffer kommen als echte Nonces zurück.
template<bool TIMED>
static IRAM_ATTR __attribute__((noinline)) uint32_t scanHardware(const uint32_t* block1, const uint32_t* tail, uint32_t nonce,
                                       uint32_t count, uint32_t* hits, uint32_t maxHits, uint32_t* last) {
  uint32_t* T = (uint32_t*)SHA_TEXT_BASE;
  uint32_t n = 0, h7 = 0;
  if (TIMED) {
    hwPrimeAll(T,block1);   // TLS kann T seit dem letzten Aufruf überschrieben haben
    for (uint32_t i=0; i<count; i++, nonce++) {   // nonce zählt hier w3
      h7 = hwOneNonceTimed(T,block1,tail,nonce);
      if ((h7 & H7_FILTER_MASK)==0 && n<maxHits) hits[n++]=__builtin_bswap32(nonce);
    }
  } else {
    for (uint32_t i=0; i<count; i++, nonce++) {
      h7 = hwOneNonce(T,block1,tail,nonce);
      if ((h7 & H7_FILTER_MASK)==0 && n<maxHits) hits[n++]=nonce;
    }
  }
  *last = h7;
  return n;
}

// Wie scanHardware, liefert aber jedes H7. Nur für den Selbsttest, damit er dieselbe enge Schleife prüft.
template<bool TIMED>
static IRAM_ATTR __attribute__((noinline)) void scanHardwareH7(const uint32_t* block1, const uint32_t* tail, uint32_t nonce,
                                     uint32_t count, uint32_t* out) {
  uint32_t* T = (uint32_t*)SHA_TEXT_BASE;
  if (TIMED) {
    hwPrimeAll(T,block1);
    for (uint32_t i=0; i<count; i++, nonce++) out[i] = hwOneNonceTimed(T,block1,tail,nonce);
  } else {
    for (uint32_t i=0; i<count; i++, nonce++) out[i] = hwOneNonce(T,block1,tail,nonce);
  }
}

// 4096 Nonces in der engen Schleife gegen die Software. Liefert die Zahl der Abweichungen.
template<bool TIMED>
static int selfTestRun(const MiningMid& m, const uint32_t* block1, uint32_t& fhw, uint32_t& fsw, uint32_t& total) {
  int bad=0;
  const uint32_t batch=256;
  uint32_t hw[batch];
  total=4096;
  for (uint32_t s=0;s<total;s+=batch) {
    esp_sha_lock_engine(SHA2_256);
    esp_sha_lock_memory_block();
    hwWaitIdle();
    scanHardwareH7<TIMED>(block1,m.tail,0x12345678u+s,batch,hw);
    esp_sha_unlock_memory_block();
    esp_sha_unlock_engine(SHA2_256);
    for (uint32_t i=0;i<batch;i++) {
      uint32_t x=0x12345678u+s+i;
      uint32_t sw=hashNonceH7(m, TIMED ? __builtin_bswap32(x) : x);
      if (hw[i]!=sw) { if (!bad) { fhw=hw[i]; fsw=sw; } bad++; }
    }
  }
  return bad;
}

// Prüft den Hardware-Weg gegen die Software. Bei Abweichung bleibt die Hardware aus.
// Zuerst die Fassung mit festen Wartezeiten, bei Fehlern die gepollte.
static bool hardwareSelfTest(char* out, size_t n) {
  if (ESP.getChipRevision() < 3) {
    // Vor Revision 3 sind direkte DPORT-Lesezugriffe nicht ohne Vorkehrungen sicher.
    snprintf(out, n, "HWTEST aus: Chip-Revision %d", ESP.getChipRevision());
    return false;
  }
  uint8_t header[80];
  for (int i=0;i<80;i++) header[i]=(uint8_t)(i*37+11);
  MiningMid m; midPrepare(header,m);
  uint32_t block1[16];
  for (int k=0;k<16;k++) block1[k]=__builtin_bswap32(*(uint32_t*)(header+k*4));
  uint32_t fhw=0, fsw=0, total=0;
  int badT=-1, bad;
  if (getCpuFrequencyMhz() == 240) {   // Die festen Wartezeiten gelten nur für 240 MHz
    badT = selfTestRun<true>(m, block1, fhw, fsw, total);
    hwTimed = badT == 0;
  }
  if (hwTimed) bad = 0;
  else bad = selfTestRun<false>(m, block1, fhw, fsw, total);
  char tm[24] = "";
  if (badT > 0) snprintf(tm, sizeof(tm), ", fest FAIL %d", badT);
  if (bad) snprintf(out, n, "HWTEST FAIL %d/%lu hw=%08lx sw=%08lx%s", bad, (unsigned long)total, (unsigned long)fhw, (unsigned long)fsw, tm);
  else     snprintf(out, n, "HWTEST OK %lu/%lu %s%s", (unsigned long)total, (unsigned long)total, hwTimed ? "fest" : "Pollen", tm);
  return bad==0;
}

// ================================================================
// MINER-TASKS
// ================================================================
struct Worker {
  uint8_t    id;
  uint32_t   gen;
  bool       ready;
  StratumJob j;
  uint8_t    header[80];
  uint32_t   block1[16];
  MiningMid  mid;
  double     target;
  double     net;
  uint32_t   nonce;
};
static Worker workers[2];

static void buildWork(Worker& w) {
  w.ready = buildHeader(w.j, w.header);
  if (!w.ready) return;
  midPrepare(w.header, w.mid);
  for (int k=0;k<16;k++) w.block1[k]=__builtin_bswap32(*(uint32_t*)(w.header+k*4));
}

// Holt bei Bedarf den aktuellen Job. Die Miner unterscheiden sich im letzten
// Extranonce2-Byte, rechnen also nie dieselben Hashes.
static void refreshWork(Worker& w) {
  if (w.gen == job.generation) return;
  miningLockJob();
  bool valid = job.valid && miner.poolConn;
  bool newJob = strcmp(w.j.id, job.id) != 0;
  w.gen = job.generation;
  if (valid) {
    w.j = job;
    w.target = miner.difficultyF;
    w.net = job.netDiff;
  }
  miningUnlockJob();
  w.ready = false;
  if (!valid) return;
  if (w.j.exNonce2Len) w.j.exNonce2[w.j.exNonce2Len-1] ^= (uint8_t)(w.id << 7);
  buildWork(w);
  if (!w.ready) { logMsg("MINE","Header nicht baubar, warte auf neuen Job"); return; }
  if (newJob) w.nonce = esp_random();
}

// Treffer des Vorfilters vollständig in Software nachrechnen
static void handleHit(Worker& w, uint32_t nonce, bool fromHw) {
  setHeaderNonce(w.header, nonce);
  uint8_t hash[32];
  sha256d(w.header, 80, hash);
  if (hash[31] || hash[30]) { if (fromHw) miner.hwErrors++; return; }
  double d = hashToDiff(hash);
  bool isBlock = (w.net > 0 && d >= w.net);   // K2
  portENTER_CRITICAL(&statMux);
  if (d > miner.bestDiff) miner.bestDiff = d;
  if (d > miner.bestDiffAllTime) { miner.bestDiffAllTime = d; bestDirty = true; }
  if (isBlock) { miner.blockHits++; bestDirty = true; }
  portEXIT_CRITICAL(&statMux);
  if (d >= w.target || isBlock) {             // K3
    ShareMsg s;
    fillShare(s, w.j, nonce);
    s.diff = d;
    if (xQueueSend(shareQueue, &s, 0) != pdTRUE) logMsg("MINE","Share-Queue voll");
  }
  if (isBlock) logMsg("MINE","BLOCK!");
}

static uint32_t mbedtlsBatch(Worker& w, uint32_t count, uint32_t* hits, uint32_t maxHits) {
  uint32_t n=0, nonce=w.nonce;
  for (uint32_t i=0;i<count;i++,nonce++) {
    setHeaderNonce(w.header, nonce);
    uint8_t a[32], h[32];
    mbedtls_sha256(w.header, 80, a, 0);
    mbedtls_sha256(a, 32, h, 0);
    if (h[31]==0 && h[30]==0 && n<maxHits) hits[n++]=nonce;
  }
  return n;
}

static IRAM_ATTR void minerTask(void* arg) {
  Worker& w = *(Worker*)arg;
  uint32_t hits[16];
  for (;;) {
    int bk = backendMode;
    if (w.id == 1 && (!core0Mode || bk == BK_MBEDTLS)) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    if (w.id == 1 && bk == BK_HW) bk = BK_SOFT;   // Es gibt nur eine SHA-Hardware
    refreshWork(w);
    if (!w.ready) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    // Ein Durchlauf dauert rund 30-50 ms, dann ist neuer Job oder Sperre für TLS möglich
    uint32_t count = bk == BK_HW ? 20000 : bk == BK_SOFT ? 1000 : 300;
    if ((uint64_t)w.nonce + count > 0xFFFFFFFFull) {
      w.nonce = 0;
      incExNonce2(w.j);
      buildWork(w);
      if (!w.ready) continue;
    }
    uint32_t n;
    if (bk == BK_HW) {
      esp_sha_lock_engine(SHA2_256);
      esp_sha_lock_memory_block();
      hwWaitIdle();   // TLS oder WLAN (SHA-1/384) kann die Hardware noch belegen, siehe sha_parallel_engine.h
      bool timed = hwTimed;
      uint32_t last;
      static uint32_t cPrev = 0;
      uint32_t c0 = xthal_get_ccount();
      n = timed ? scanHardware<true>(w.block1, w.mid.tail, w.nonce, count, hits, 16, &last)
                : scanHardware<false>(w.block1, w.mid.tail, w.nonce, count, hits, 16, &last);
      uint32_t c1 = xthal_get_ccount();
      hwCycLoop = (uint32_t)((uint64_t)(c1 - c0) * 100 / count);
      if (cPrev) hwCycAll = (uint32_t)((uint64_t)(c0 - cPrev) * 100 / count);
      cPrev = c0;
      esp_sha_unlock_memory_block();
      esp_sha_unlock_engine(SHA2_256);
      // Laufende Prüfung: letzter Nonce der Runde in Software. Kostet rund 0,1 % der Rechenzeit.
      // Einzelne Fehler kommen auch beim Pollen vor, gehäuft in der Startphase mit vielen TLS-Abrufen.
      // Zu knappe Wartezeiten machen dagegen fast jeden Hash falsch. Deshalb erst ab 25 % in 256 Runden zurück.
      static uint32_t winChecks = 0, winFails = 0;
      miner.hwChecks++; winChecks++;
      uint32_t lastN = w.nonce + count - 1;
      if (last != hashNonceH7(w.mid, timed ? __builtin_bswap32(lastN) : lastN)) { miner.hwCheckFails++; winFails++; }
      if (winChecks >= 256) {
        if (timed && winFails >= 64) { hwTimed = false; logMsg("MINE","HW-Pruefung gehaeuft falsch, zurueck auf Pollen"); }
        winChecks = winFails = 0;
      }
    } else if (bk == BK_SOFT) {
      n = scanSoftware(w.mid, w.nonce, count, hits, 16);
    } else {
      n = mbedtlsBatch(w, count, hits, 16);
    }
    for (uint32_t i=0;i<n;i++) handleHit(w, hits[i], bk == BK_HW);
    w.nonce += count;
    workerHashes[w.id] += count;
    // Kern 0: Der Idle-Task muss laufen, sonst schlägt der Task-Watchdog an.
    // Kern 1 gehört allein dem Miner (Arduino-Task gelöscht, Idle 1 nicht überwacht).
    // vTaskDelay(1) kostete dort bis zu 1 ms je Runde von rund 25 ms.
    if (w.id == 0) taskYIELD(); else vTaskDelay(1);
  }
}

void miningStart() {
  jobMutex = xSemaphoreCreateMutex();
  shareQueue = xQueueCreate(16, sizeof(ShareMsg));
  char msg[80];
  hwUsable = hardwareSelfTest(msg, sizeof(msg));
  logMsg("TEST", msg);
  if (!hwUsable) backendMode = BK_SOFT;
  workers[0].id = 0;
  workers[1].id = 1;
  xTaskCreatePinnedToCore(minerTask, "miner1", 6144, &workers[0], 1, NULL, 1);
  xTaskCreatePinnedToCore(minerTask, "miner0", 6144, &workers[1], 1, NULL, 0);
}

void miningStatsTick(uint32_t now) {
  static uint32_t tLast=0, last0=0, last1=0, tDiag=0, sentAtDiag=0;
  if (tLast == 0) { tLast = now; last0 = workerHashes[0]; last1 = workerHashes[1]; return; }
  uint32_t dt = now - tLast;
  if (dt == 0) return;
  uint32_t h0 = workerHashes[0], h1 = workerHashes[1];
  miner.hrCore1 = (uint32_t)((uint64_t)(h0-last0)*1000/dt);
  miner.hrCore0 = (uint32_t)((uint64_t)(h1-last1)*1000/dt);
  miner.hashrate = miner.hrCore1 + miner.hrCore0;
  last0 = h0; last1 = h1; tLast = now;
  if (bestDirty) { bestDirty = false; saveBestDiff(); }

  if (now - tDiag > 60000) {
    char diagMsg[170];
    snprintf(diagMsg, sizeof(diagMsg),
      "PoolDiff=%.6g NetDiff=%.0f Best=%.4g Shares/min=%lu HR=%luH/s K1=%lu K0=%lu Modus=%d%s HWfehler=%lu Pruef=%lu",
      miner.difficultyF, job.netDiff, miner.bestDiff, miner.sharesSent-sentAtDiag, miner.hashrate,
      miner.hrCore1, miner.hrCore0, backendMode, hwTimed ? "f" : "", miner.hwErrors, miner.hwCheckFails);
    logMsg("DIAG", diagMsg);
    sentAtDiag = miner.sharesSent;
    tDiag = now;
  }
}
