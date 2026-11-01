#!/usr/bin/env python3
"""Prueft den Mining-Kern der Firmware (Plan 1.10). Python spielt den Pool:
gleiche Nachrichten wie Stratum v1, jede Einreichung wird unabhaengig mit hashlib nachgerechnet.

Aufruf: python3 tools/mining_test/check_mining.py
"""
import hashlib
import json
import os
import pathlib
import random
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/tomessino_dgb"
HERE = ROOT / "tools/mining_test"
DIFF1 = 0xFFFF * 2 ** 208
fails = 0


def check(name, ok, extra=""):
    global fails
    print(f"{name:<62} {'OK' if ok else 'FEHLER'} {extra}")
    fails += 0 if ok else 1


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def word_swap(b):
    return b"".join(b[i:i + 4][::-1] for i in range(0, 32, 4))


TMP = tempfile.mkdtemp(prefix="mining_test_")
BIN = os.path.join(TMP, "host_mining")
subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", f"-I{SKETCH}", f"-I{os.path.expanduser('~/Arduino/libraries/ArduinoJson/src')}",
                str(HERE / "host_mining.cpp"), str(SKETCH / "mining_core.cpp"), str(SKETCH / "stratum_core.cpp"), "-lm", "-o", BIN], check=True)


def pool(lines, start=0, max_hashes=20_000_000):
    r = subprocess.run([BIN, "pool", f"{start:08x}", str(max_hashes)], input="".join(l + "\n" for l in lines),
                       capture_output=True, text=True, timeout=300)
    return r.stdout.split("\n")


def ev(lines):
    return [l[3:] for l in lines if l.startswith("EV ")]


SUB = json.dumps({"id": 1, "result": [[["mining.notify", "x"]], "9a997fd1", 8], "error": None})
AUTH = json.dumps({"id": 2, "result": True, "error": None})


def notify(job_id, prev, coinb1, coinb2, branches, version, nbits, ntime, clean=True):
    return json.dumps({"id": None, "method": "mining.notify", "params": [job_id, prev, coinb1, coinb2, branches, version, nbits, ntime, clean]})


def diff_msg(d):
    return json.dumps({"id": None, "method": "mining.set_difficulty", "params": [d]})


def verify_submit(line, en1, coinb1, coinb2, branches, version, prev, ntime_expected, nbits, want_diff):
    """So rechnet ein Pool: Header aus den eigenen Job-Daten und dem Submit neu aufbauen."""
    parts = line.split(" ", 2)
    msg = json.loads(parts[2])
    assert msg["method"] == "mining.submit"
    user, jid, en2_hex, ntime, nonce_hex = msg["params"]
    assert ntime == ntime_expected
    coinbase = bytes.fromhex(coinb1) + bytes.fromhex(en1) + bytes.fromhex(en2_hex) + bytes.fromhex(coinb2)
    root = sha256d(coinbase)
    for b in branches:
        root = sha256d(root + bytes.fromhex(b))
    header = (int(version, 16).to_bytes(4, "little") + word_swap(bytes.fromhex(prev)) + root +
              int(ntime, 16).to_bytes(4, "little") + int(nbits, 16).to_bytes(4, "little") +
              int(nonce_hex, 16).to_bytes(4, "little"))
    h = sha256d(header)
    d = DIFF1 / int.from_bytes(h, "little")
    return d >= want_diff, d, en2_hex, h, float(parts[1])


# ----------------------------------------------------------------
# 1. Selbsttest und Sollwerte aus tools/test_hash_vektoren.py
# ----------------------------------------------------------------
r = subprocess.run([BIN, "selftest"], capture_output=True, text=True)
print(r.stdout.strip())
check("Selbsttest OK", r.returncode == 0 and r.stdout.strip().endswith("OK") and "diff=2536.43" in r.stdout)

# ----------------------------------------------------------------
# 2. hashToDiff und netDiff gegen exakte Rechnung
# ----------------------------------------------------------------
random.seed(1)
bad = 0
for i in range(300):
    top = random.randint(0, 31)
    h = bytes(random.randrange(256) for _ in range(top + 1)) + bytes(31 - top)
    if int.from_bytes(h, "little") == 0:
        continue
    got = float(subprocess.run([BIN, "diff", h.hex()], capture_output=True, text=True).stdout)
    want = DIFF1 / int.from_bytes(h, "little")
    if abs(got - want) / want > 1e-12:
        bad += 1
check("hashToDiff: 300 Zufallswerte auf 1e-12 genau", bad == 0, f"{bad} Abweichungen")
for bits, want in (("1d00ffff", 1.0), ("1b0404cb", 16307.420938523983), ("1a0de6f4", None)):
    target = int(bits[2:], 16) * 256 ** (int(bits[:2], 16) - 3)
    w = DIFF1 / target
    got = float(subprocess.run([BIN, "netdiff", bits], capture_output=True, text=True).stdout)
    check(f"netDiff {bits} = {w:.3f}", abs(got - w) / w < 1e-12)

# ----------------------------------------------------------------
# 2a. Software-SHA mit Midstate gegen hashlib (Plan 2.2, 2.3)
# ----------------------------------------------------------------
rnd = random.Random(3)
bad = 0
for i in range(200):
    h = rnd.randbytes(80)
    nonce = rnd.randrange(2 ** 32)
    full = sha256d(h[:76] + nonce.to_bytes(4, "little"))
    want = f"{int.from_bytes(full[28:32], 'big'):08x}"
    got = subprocess.run([BIN, "h7", h.hex(), f"{nonce:08x}"], capture_output=True, text=True).stdout.strip()
    if got != want:
        bad += 1
check("H7 mit Midstate: 200 Zufalls-Header wie hashlib", bad == 0, f"{bad} Abweichungen")
h = rnd.randbytes(80)
start = rnd.randrange(2 ** 32 - 700000)
want = [n for n in range(start, start + 500000) if sha256d(h[:76] + n.to_bytes(4, "little"))[30:32] == b"\0\0"]
got = [int(x, 16) for x in subprocess.run([BIN, "scan", h.hex(), f"{start:08x}", "500000"], capture_output=True, text=True).stdout.split()]
check(f"Scan: gleiche Treffer wie hashlib ueber 500000 Nonces ({len(want)})", got == want[:16] and len(want) > 0)

# ----------------------------------------------------------------
# 3. Echter Block: Bitcoin-Block 1, Coinbase in Stratum-Teile zerlegt.
#    Gleicher Weg wie im Betrieb, muss den echten Block-Hash treffen.
# ----------------------------------------------------------------
B1_HASH = "00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048"
B1_MERKLE = "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098"
B1_PREV = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"
B1_COINBASE = ("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff"
               "0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000")
check("Testvektor: Coinbase ergibt Merkle-Root von Block 1", sha256d(bytes.fromhex(B1_COINBASE))[::-1].hex() == B1_MERKLE)
# EN2 startet in der Firmware bei null. Darum liegen EN1 und EN2 hier in den Null-Bytes der Prevout.
cb1 = B1_COINBASE[:10 + 8]
en1, en2 = "00000000", "0000"
cb2 = B1_COINBASE[10 + 8 + 12:]
assert cb1 + en1 + en2 + cb2 == B1_COINBASE
prev_stratum = word_swap(bytes.fromhex(B1_PREV)[::-1]).hex()
sub_b1 = json.dumps({"id": 1, "result": [[], en1, 2], "error": None})
out = pool([sub_b1, diff_msg(1), notify("b1", prev_stratum, cb1, cb2, [], "00000001", "1d00ffff", "4966bc61")], start=0x9962e301, max_hashes=1)
sub_line = next((l for l in out if l.startswith("SUBMIT")), None)
check("Block 1: Firmware findet die echte Nonce", sub_line is not None and '"9962e301"' in sub_line)
hdr = next(l[7:] for l in out if l.startswith("HEADER"))
check("Block 1: Header identisch bis auf die Nonce", hdr[:152] == ("01000000" + bytes.fromhex(B1_PREV)[::-1].hex() + bytes.fromhex(B1_MERKLE)[::-1].hex() + "61bc6649" + "ffff001d"))
hb = bytes.fromhex(hdr[:152]) + (0x9962e301).to_bytes(4, "little")
check("Block 1: Hash ist der echte Block-Hash", sha256d(hb)[::-1].hex() == B1_HASH)
check("Block 1: Submit sendet EN2 mit Pool-Laenge", '"0000"' in (sub_line or ""))

# ----------------------------------------------------------------
# 4. Simulierter Pool, Diff 0,001 wie am Geraet, 8 Byte EN2, mit Merkle-Zweigen.
#    Ein Share bei dieser Diff braucht im Mittel 4,3 Millionen Hashes.
# ----------------------------------------------------------------
rnd = random.Random(7)
for case in range(3):
    prev = rnd.randbytes(32).hex()
    cb1 = rnd.randbytes(rnd.randint(40, 90)).hex()
    cb2 = rnd.randbytes(rnd.randint(40, 150)).hex()
    branches = [rnd.randbytes(32).hex() for _ in range(case * 3)]
    version, nbits, ntime = "20000004", "1a0de6f4", f"{rnd.randrange(2**32):08x}"
    out = pool([SUB, AUTH, diff_msg(0.001), notify(f"j{case}", prev, cb1, cb2, branches, version, nbits, ntime)], start=rnd.randrange(2**32))
    line = next((l for l in out if l.startswith("SUBMIT")), None)
    if not line:
        check(f"Pool-Simulation {case}: Share gefunden", False)
        continue
    ok, d, en2_hex, h, fw_d = verify_submit(line, "9a997fd1", cb1, cb2, branches, version, prev, ntime, nbits, 0.001)
    check(f"Pool-Simulation {case}: Share gueltig, {len(branches)} Zweige, Diff {d:.5f}", ok)
    check(f"Pool-Simulation {case}: EN2 hat 8 Byte (E1)", len(en2_hex) == 16)
    check(f"Pool-Simulation {case}: Firmware-Diff stimmt", abs(fw_d - d) / d < 1e-6)
    check(f"Pool-Simulation {case}: Hash oben genullt (K1)", h[31] == 0)

# Unter der Pool-Diff darf nichts gesendet werden (K3)
prev, cb1, cb2 = rnd.randbytes(32).hex(), rnd.randbytes(50).hex(), rnd.randbytes(50).hex()
out = pool([SUB, diff_msg(1), notify("k3", prev, cb1, cb2, [], "20000004", "1a0de6f4", "5f5e1000")], max_hashes=200000)
check("K3: Bei Diff 1 und 200000 Hashes wird nichts gesendet", any(l == "KEIN_TREFFER" for l in out) and not any(l.startswith("SUBMIT") for l in out))

# BIP34-Hoehe aus der Coinbase. Aufbau wie am Geraet: 42 Byte vor dem Push.
def coinb_with_height(h, push):
    return ("01000000" + "01" + "00" * 32 + "ffffffff" + "40" + f"{push:02x}" + h.to_bytes(push, "little").hex() + "aabb")
for h, push in ((23456789, 4), (500000, 3), (127, 1)):
    out = pool([SUB, notify("h", prev, coinb_with_height(h, push), cb2, [], "20000004", "1a0de6f4", "5f5e1000")], max_hashes=1)
    check(f"Blockhoehe {h} aus der Coinbase", f"HEIGHT {h}" in out)
out = pool([SUB, notify("h", prev, "0100", cb2, [], "20000004", "1a0de6f4", "5f5e1000")], max_hashes=1)
check("Kurze Coinbase ergibt Hoehe 0", "HEIGHT 0" in out)

# ----------------------------------------------------------------
# 5. Extranonce2 zaehlt und landet im Submit
# ----------------------------------------------------------------
out = pool([SUB, "INC_EN2", "INC_EN2", diff_msg(0.0001), notify("n", prev, cb1, cb2, [], "20000004", "1a0de6f4", "5f5e1000", False)], max_hashes=20000000)
line = next((l for l in out if l.startswith("SUBMIT")), None)
check("EN2: Zaehler erhoeht", "EN2 0200000000000000" in out)
ok, d, en2_hex, _, _ = verify_submit(line, "9a997fd1", cb1, cb2, [], "20000004", prev, "5f5e1000", "1a0de6f4", 0.0001)
check("EN2: Submit nutzt dieselben Bytes wie die Coinbase", en2_hex == "0200000000000000" and ok)
out = pool([SUB, "INC_EN2", notify("n", prev, cb1, cb2, [], "20000004", "1a0de6f4", "5f5e1000", True), "INC_EN2"], max_hashes=1)
check("EN2: clean_jobs setzt zurueck", "EN2 0100000000000000" in out)

# ----------------------------------------------------------------
# 6. Antworten nach id (E2, E3, E9)
# ----------------------------------------------------------------
def one(msg, sub_first=True):
    return ev(pool(([SUB] if sub_first else []) + [msg], max_hashes=1))[-1]


check("E2: Authorize zaehlt nicht als Share", one(AUTH) == "AUTH_OK")
check("Authorize abgelehnt mit Text", one(json.dumps({"id": 2, "result": False, "error": [24, "Unauthorized", None]})) == "AUTH_FAIL 24 Unauthorized")
check("Share OK", one(json.dumps({"id": 100, "result": True, "error": None})) == "SHARE_OK")
check("E3: Ablehnung mit result:null und Text", one(json.dumps({"id": 101, "result": None, "error": [23, "Low difficulty share", None]})) == "SHARE_REJ 23 Low difficulty share")
check("Ablehnung mit result:false ohne Text", one(json.dumps({"id": 102, "result": False, "error": None})) == "SHARE_REJ ohne Fehlertext")
check("Fehler als String", one(json.dumps({"id": 102, "result": None, "error": "stale"})) == "SHARE_REJ stale")
check("Diff-Wunsch abgelehnt wird gemeldet", one(json.dumps({"id": 3, "result": None, "error": [20, "nope", None]})) == "SUGGEST_REPLY 20 nope")
check("Unbekannte id wird ignoriert", one(json.dumps({"id": 50, "result": True, "error": None})) == "NONE")
check("Kaputtes JSON", one('{"id":1,') == "JSON_ERROR")
check("Unbekannte Methode", one(json.dumps({"id": None, "method": "client.reconnect", "params": []})) == "OTHER client.reconnect")
check("Subscribe ohne Extranonce ist Fehler", one(json.dumps({"id": 1, "result": [[], "", 0], "error": None}), False).startswith("SUBSCRIBE_FAIL"))

# ----------------------------------------------------------------
# 7. Jobs, die nicht passen (M8)
# ----------------------------------------------------------------
long_cb = "ab" * 512
r = one(notify("x", prev, long_cb, cb2, [], "20000004", "1a0de6f4", "5f5e1000"))
check("M8: Zu lange Coinbase wird gemeldet", r.startswith("JOB_BAD Coinbase zu lang"), r)
ok_cb = "ab" * 511
check("M8: Coinbase knapp unter dem Puffer geht", one(notify("x", prev, ok_cb, cb2, [], "20000004", "1a0de6f4", "5f5e1000")) == "JOB")
r = one(notify("x", prev, cb1, cb2, [prev] * 17, "20000004", "1a0de6f4", "5f5e1000"))
check("Zu viele Merkle-Zweige werden gemeldet", r.startswith("JOB_BAD 17 Merkle"), r)
r = ev(pool([notify("x", prev, cb1, cb2, [], "20000004", "1a0de6f4", "5f5e1000")], max_hashes=1))[-1]
check("Job vor Subscribe ist unbrauchbar", r.startswith("JOB_BAD Job vor"), r)
r = one(json.dumps({"id": None, "method": "mining.notify", "params": ["x"]}))
check("notify mit zu wenig Feldern", r.startswith("JOB_BAD notify"), r)
out = pool([SUB, json.dumps({"id": None, "method": "mining.set_extranonce", "params": ["aabb", 4]}), diff_msg(0.0001),
            notify("e", prev, cb1, cb2, [], "20000004", "1a0de6f4", "5f5e1000")])
line = next((l for l in out if l.startswith("SUBMIT")), None)
ok, _, en2_hex, _, _ = verify_submit(line, "aabb", cb1, cb2, [], "20000004", prev, "5f5e1000", "1a0de6f4", 0.0001)
check("set_extranonce: neue Laengen gelten", ok and len(en2_hex) == 8)

subprocess.run(["rm", "-rf", TMP])
print("ALLE TESTS OK" if not fails else f"{fails} TESTS FEHLGESCHLAGEN")
sys.exit(1 if fails else 0)
