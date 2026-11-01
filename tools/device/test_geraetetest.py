#!/usr/bin/env python3
"""Testet geraetetest.py ohne Geraet: falscher Miner auf einem pty plus lokaler Web-Server."""
import http.server
import json
import os
import pathlib
import pty
import shutil
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import geraetetest as g

TMP = pathlib.Path(tempfile.mkdtemp(prefix="geraetetest_"))
g.OUT, g.LOGS, g.BACKUP = TMP, TMP / "logs", TMP / "backup"
fails = 0


def check(name, ok):
    global fails
    print(f"{name:<62} {'OK' if ok else 'FEHLER'}")
    fails += 0 if ok else 1


GOOD_LOG = """ets Jul 29 2019 12:21:46
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)

=== Tomessino-DGB v7.0.0-d3 ===
[00:00:01][TOUCH] XPT2046 bereit
[00:00:05][WIFI] MeinNetz / 127.0.0.1
[00:00:06][HTTP] http://127.0.0.1
[00:00:06][POOL] Verbinde P1: dgb-stratum.solominer.net:3333
[00:00:06][POOL] Subscribe+Authorize gesendet
[00:00:07][POOL] EN1=abcd1234 sz=8
[00:00:07][POOL] Diff: 0.01
[00:00:07][MINE] Job:12 br:3
[DISP] page=0 ms=41
[00:00:11][PRICE] DGB=$0.00 BTC=$77853 Coins 7/7
[MEM] price heap=120000 minheap=60000 stackfrei=3000
[00:00:21][WEATHER] Bewoelkt 12.0C
[00:00:22][AIR ] PM2.5=6 PM10=8
[MEM] weather heap=118000 minheap=55000 stackfrei=2500
[DISP] page=1 ms=45
[DISP] page=2 ms=43
[DISP] page=3 ms=47
[DISP] page=4 ms=44
[00:00:30][DIAG] nonce=123 best=0
"""
CRASH_LOG = GOOD_LOG + "Guru Meditation Error: Core  1 panic'ed (LoadProhibited)\nBacktrace: 0x400\n=== Tomessino-DGB v7.0.0-d3 ===\n"

STATE = {"info": {"fwVersion": "7.0.0-d3", "poolConn": True, "temp": 45.0, "wallet": "DTest", "worker": "w", "pool1Host": "p",
                  "pool2Host": "q", "ssid": "MeinNetz", "timezone": 1.0, "brightness": 200, "weatherLat": 1.0, "weatherLon": 2.0, "ntpOk": True},
         "broken_json": False, "pages": []}


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/probe":
            body = json.dumps({"hashrate": 12000}).encode()
        elif self.path == "/api/system/info":
            body = b'{"hashrate":1,"log":["abgeschni' if STATE["broken_json"] else json.dumps(STATE["info"]).encode()
        elif self.path.startswith("/api/display/page"):
            STATE["pages"].append(self.path.split("=")[1])
            body = b"ok"
        elif self.path == "/":
            body = b"<html>"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
URL = f"http://127.0.0.1:{srv.server_address[1]}"


def fake_miner(text):
    """Oeffnet ein pty und schreibt das Log Zeile fuer Zeile hinein."""
    master, slave = pty.openpty()
    name = os.ttyname(slave)

    def run():
        time.sleep(0.3)
        for line in text.split("\n"):
            os.write(master, (line + "\r\n").encode())
            time.sleep(0.01)

    threading.Thread(target=run, daemon=True).start()
    return name, master, slave


def status(name):
    d = json.loads((g.LOGS / f"{name}.ergebnis.json").read_text())
    return {i: st for i, st, _ in d["ergebnisse"]}


EXPECT = {"preise7", "pm", "mem", "disp"}
FAST = dict(probe_s=0.2, info_s=0.5, settle=0.5, page_s=0.1, web_ab=0)

# 1. Guter Lauf mit Seitenwechsel
port, m, s = fake_miner(GOOD_LOG)
# 30-s-Grenze der Hashrate-Auswertung liegt hoeher als der Testlauf, Proben werden unten nachgeprueft
e = g.capture("t_gut", port, 3, "7.0.0-d3", URL, True, EXPECT, None, **FAST)
st = status("t_gut")
for k in ("firmware", "absturz", "neustarts", "wlan", "pool", "kurse", "wetter", "feinstaub", "speicher", "zeichnen", "seiten", "web", "status_json"):
    check(f"Guter Lauf: {k}", st.get(k) == "OK")
check("Guter Lauf: E1 wird benannt (sz=8)", st.get("en2_laenge") == "INFO")
check("Guter Lauf: Hashrate in Ruhe- und Wechselphase", "hashrate_ruhe" in st and "hashrate_wechsel" in st)
check("Guter Lauf: Seiten wurden per Web umgeschaltet", len(set(STATE["pages"])) >= 2)
check("Guter Lauf: Rueckkehr zu Seite 0", STATE["pages"][-1] == "0")
check("Log-Datei enthaelt Zeitstempel", (g.LOGS / "t_gut.log").read_text().split("\n")[0].startswith("[+"))

# 2. Hashrate-Auswertung (Proben ab 30 s)
r = {i: st for i, st, _ in g.analyze(GOOD_LOG, [{"t": 40, "probe": 200, "hr": 12000}], 200, "7.0.0-d3")}
check("Hashrate > 0 ergibt OK", r["hashrate"] == "OK")
r = {i: st for i, st, _ in g.analyze(GOOD_LOG, [{"t": 40, "probe": 200, "hr": 0}], 200, "7.0.0-d3")}
check("Hashrate 0 ergibt FEHLER", r["hashrate"] == "FEHLER")

# 3. Falsche Version, Absturz, Neustart
port, m, s = fake_miner(CRASH_LOG)
g.capture("t_crash", port, 2, "7.0.0", URL, False, EXPECT, None, **FAST)
st = status("t_crash")
check("Absturz erkannt", st["absturz"] == "FEHLER")
check("Neustart erkannt", st["neustarts"] == "FEHLER")
check("Falsche Version erkannt", st["firmware"] == "FEHLER")

# 4. E6: abgeschnittenes Status-JSON
STATE["broken_json"] = True
port, m, s = fake_miner(GOOD_LOG)
g.capture("t_e6", port, 2, "7.0.0-d3", URL, False, EXPECT, None, **FAST)
STATE["broken_json"] = False
check("Abgeschnittenes JSON ergibt Warnung", status("t_e6")["status_json"] == "WARNUNG")

# 5. Einstellungen gegen Ausgangsstand
port, m, s = fake_miner(GOOD_LOG)
g.capture("t_basis", port, 2, None, URL, False, (), None, **FAST)
port, m, s = fake_miner(GOOD_LOG)
g.capture("t_gleich", port, 2, "7.0.0-d3", URL, False, EXPECT, "t_basis", **FAST)
check("Gleiche Einstellungen ergeben OK", status("t_gleich")["einstellungen"] == "OK")
STATE["info"]["wallet"] = ""
port, m, s = fake_miner(GOOD_LOG)
g.capture("t_weg", port, 2, "7.0.0-d3", URL, False, EXPECT, "t_basis", **FAST)
d = json.loads((g.LOGS / "t_weg.ergebnis.json").read_text())
txt = next(tx for i, _, tx in d["ergebnisse"] if i == "einstellungen")
check("Verlorene Wallet ergibt FEHLER", status("t_weg")["einstellungen"] == "FEHLER")
check("Bericht nennt das Feld, nicht den Wert", "wallet" in txt and "DTest" not in txt)
STATE["info"]["wallet"] = "DTest"

# 6. Log ohne WLAN, ohne Pool
r = {i: st for i, st, _ in g.analyze("=== Tomessino-DGB v7.0.0 ===\n[00:00:20][WIFI] Kein WLAN - Offline\n", [], None, "7.0.0")}
check("Kein WLAN ergibt FEHLER", r["wlan"] == "FEHLER" and r["pool"] == "FEHLER")

# 7. Backup-Analyse an echter merged.bin
merged = g.BUILD / g.SKETCHES["firmware"] / f"{g.SKETCHES['firmware']}.ino.merged.bin"
if merged.exists():
    data = merged.read_bytes().ljust(g.FLASH_SIZE, b"\xff")
    info = g.parse_flash(data)
    check("Partitionstabelle gelesen", any(p["name"] == "nvs" for p in info["partitionen"]))
    check("NVS passt zum Ziel-Layout", info["nvs_passt"])
    check("Leeres NVS wird erkannt", not info["nvs_nicht_leer"])
    check("App-Beschreibung gelesen", any(a["projekt"] for a in info["apps"]))
    g.BACKUP.mkdir(parents=True, exist_ok=True)
    f = g.BACKUP / "flash_test.bin"
    f.write_bytes(data)
    check("finish_backup: Rueckgabe 0", g.finish_backup(f) == 0)
    check("Backup nur fuer den Besitzer lesbar", oct(f.stat().st_mode)[-3:] == "600")
    check("Backup wird gefunden", g.latest_backup() is not None)
    # Anderes NVS-Layout
    bad = bytearray(data)
    for off in range(0x8000, 0x8C00, 32):
        if bad[off:off + 2] == b"\xaa\x50" and bad[off + 2] == 1 and bad[off + 3] == 2:
            bad[off + 4:off + 8] = (0x11000).to_bytes(4, "little")
    f2 = g.BACKUP / "flash_test2.bin"
    f2.write_bytes(bad)
    check("Abweichendes NVS: Rueckgabe 2", g.finish_backup(f2) == 2)
else:
    check("merged.bin vorhanden (erst bauen)", False)


# 8. Flash-Sperren
class A:
    was = "firmware"; dry_run = True; nvs_egal = False; port = None


def flash_refused():
    try:
        g.cmd_flash(A)
    except SystemExit as e:
        return str(e)
    return ""


check("Abweichendes NVS sperrt das Flashen", "NVS PASST NICHT" in flash_refused())
shutil.rmtree(g.BACKUP, ignore_errors=True)
check("Kein Backup sperrt das Flashen", "KEIN BACKUP" in flash_refused())

# 9. Stale-Build-Erkennung
if g.MANIFEST.exists():
    real = g.sketch_hash
    g.sketch_hash = lambda s: "anders"
    check("Geaenderte Quellen werden erkannt", any("neu kompilieren" in x for x in g.build_problems("firmware")))
    g.sketch_hash = real
    check("Build passt zu den Quellen", g.build_problems("firmware") == [])

# 10. Bericht
p_ok = g.cmd_bericht(None) == 0 and "t_gut" in (TMP / "BERICHT.md").read_text()
check("Bericht wird geschrieben", p_ok)

shutil.rmtree(TMP, ignore_errors=True)
print("ALLE TESTS OK" if not fails else f"{fails} TESTS FEHLGESCHLAGEN")
sys.exit(1 if fails else 0)
