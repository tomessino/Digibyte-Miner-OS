#!/usr/bin/env python3
"""Geraetetest fuer den Tomessino-Miner. Nur Python-Standardbibliothek.

Ablauf und Abnahmekriterien: docs/TESTS.md
Ergebnisse:                  geraetetest/ (Logs, Backup, BERICHT.md)

  check                      Rechner, Builds und Port pruefen
  ausgangsstand              Neustart, Log und Web des aktuell geflashten Stands
  backup                     Ganzen Flash sichern, Partitionen pruefen
  flash firmware             Firmware aufspielen (nur mit gueltigem Backup)
  test NAME --version V      Neustart, Log und Web aufzeichnen, auswerten
  seite N                    Display-Seite waehlen (nur Design-Firmware)
  einstellen k=v ...         Einstellungen speichern, z. B. wallet=D...
  bericht                    geraetetest/BERICHT.md schreiben
  restore DATEI --ja         Backup zurueckspielen
  manifest                   Pruefsummen der Builds neu schreiben
"""
import argparse
import base64
import datetime
import fcntl
import glob
import hashlib
import json
import os
import pathlib
import re
import select
import shlex
import socket
import stat
import struct
import subprocess
import sys
import termios
import threading
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "geraetetest"
LOGS = OUT / "logs"
BACKUP = OUT / "backup"
BUILD = ROOT / "firmware/build"
MANIFEST = BUILD / "manifest.json"
ARDUINO_CLI = os.path.expanduser("~/.local/bin/arduino-cli")
FQBN = "esp32:esp32:esp32:CPUFreq=240,FlashSize=4M,PartitionScheme=huge_app,UploadSpeed={speed}"
SKETCHES = {"firmware": "tomessino_dgb"}
VERSIONS = {"firmware": "7.4.5"}
ZIEL_NVS = (0x9000, 0x5000)  # NVS in der huge_app-Tabelle
FLASH_SIZE = 0x400000
USB_SERIAL = {"1a86:7523": "CH340", "1a86:55d4": "CH9102", "10c4:ea60": "CP210x", "0403:6001": "FTDI"}
TIOCMBIS, TIOCMBIC = 0x5416, 0x5417
TIOCM_DTR, TIOCM_RTS = 0x002, 0x004


# ----------------------------------------------------------------
# Port und Rechte
# ----------------------------------------------------------------
def find_ports():
    ports = []
    for dev in sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")):
        sysdev = pathlib.Path("/sys/class/tty") / os.path.basename(dev) / "device"
        vidpid = "?"
        p = sysdev.resolve() if sysdev.exists() else None
        while p and p != p.parent:
            if (p / "idVendor").exists():
                vidpid = (p / "idVendor").read_text().strip() + ":" + (p / "idProduct").read_text().strip()
                break
            p = p.parent
        ports.append((dev, vidpid, USB_SERIAL.get(vidpid, "unbekannt")))
    return ports


def pick_port(arg):
    if arg:
        return arg
    ports = find_ports()
    known = [p for p in ports if p[2] != "unbekannt"] or ports
    if len(known) == 1:
        return known[0][0]
    if not known:
        sys.exit("KEIN PORT: Kein USB-Seriell-Geraet gefunden. Ist der Miner mit einem Datenkabel angesteckt? "
                 "Verschwindet der Port gleich wieder: siehe docs/TESTS.md, Abschnitt Störungen.")
    sys.exit("MEHRERE PORTS: " + ", ".join(p[0] for p in known) + ". Bitte --port angeben.")


def ensure_dialout(argv):
    """Neue Gruppenrechte gelten erst nach Neuanmeldung. sg holt sie ohne Passwort."""
    if os.environ.get("TOMESSINO_SG") or os.getuid() == 0:
        return
    import grp
    try:
        g = grp.getgrnam("dialout")
    except KeyError:
        return
    if g.gr_gid in os.getgroups():
        return
    import pwd
    if pwd.getpwuid(os.getuid()).pw_name in g.gr_mem:
        os.environ["TOMESSINO_SG"] = "1"
        os.execvp("sg", ["sg", "dialout", "-c", shlex.join([sys.executable, os.path.abspath(__file__)] + argv)])


def esptool_path():
    found = sorted(glob.glob(os.path.expanduser("~/.arduino15/packages/esp32/tools/esptool_py/*/esptool")))
    return found[-1] if found else None


# ----------------------------------------------------------------
# Serielle Aufzeichnung
# ----------------------------------------------------------------
class SerialLog(threading.Thread):
    def __init__(self, port, path):
        super().__init__(daemon=True)
        self.path = path
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0
        a[1] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0
        a[4] = a[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.stop_flag = threading.Event()
        self.t0 = time.time()
        self.lines = []
        self.lock = threading.Lock()

    def _modem(self, op, bits):
        try:
            fcntl.ioctl(self.fd, op, struct.pack("I", bits))
        except OSError:
            pass  # kein echter Seriell-Port (Test mit pty)

    def reset(self):
        """Hard-Reset wie esptool: IO0 hoch lassen, EN kurz auf Low."""
        self._modem(TIOCMBIC, TIOCM_DTR)
        self._modem(TIOCMBIS, TIOCM_RTS)
        time.sleep(0.1)
        try:
            termios.tcflush(self.fd, termios.TCIFLUSH)  # Reste von vor dem Neustart
        except termios.error:
            pass
        self._modem(TIOCMBIC, TIOCM_RTS)
        self.t0 = time.time()

    def run(self):
        buf = b""
        with open(self.path, "w", encoding="utf-8") as f:
            while not self.stop_flag.is_set():
                r, _, _ = select.select([self.fd], [], [], 0.2)
                if not r:
                    continue
                try:
                    chunk = os.read(self.fd, 4096)
                except OSError:
                    continue
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    stamped = f"[+{time.time() - self.t0:7.1f}] {line}"
                    f.write(stamped + "\n")
                    f.flush()
                    with self.lock:
                        self.lines.append(line)

    def text(self):
        with self.lock:
            return "\n".join(self.lines)

    def close(self):
        self.stop_flag.set()
        self.join(timeout=2)
        os.close(self.fd)


# ----------------------------------------------------------------
# Web
# ----------------------------------------------------------------
def http_get(url, timeout=6):
    try:
        req = urllib.request.Request(url)
        # Ist am Miner ein Dashboard-Passwort gesetzt, kommt es aus der Umgebung, nie aus einer Datei.
        pw = os.environ.get("TOMESSINO_WEB_PW")
        if pw:
            req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{pw}".encode()).decode())
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, ""
    except Exception as e:
        return None, str(e)


IP_RE = re.compile(r"\[WIFI\s*\].* / (\d+\.\d+\.\d+\.\d+)")


class WebSampler(threading.Thread):
    """Fragt /probe und /api/system/info ab, sobald die IP im Log steht."""

    def __init__(self, serial, base_url=None, probe_s=5.0, info_s=15.0, use_probe=True):
        super().__init__(daemon=True)
        self.use_probe = use_probe  # /probe stuerzt in v6.5 und v7.0.0 ab (E12)
        self.serial, self.base = serial, base_url
        self.probe_s, self.info_s = probe_s, info_s
        self.stop_flag = threading.Event()
        self.samples = []
        self.home_status = None
        self.phase = "start"

    def run(self):
        while not self.base and not self.stop_flag.is_set():
            m = IP_RE.search(self.serial.text())
            if m:
                self.base = "http://" + m.group(1)
            else:
                time.sleep(0.5)
        next_info = 0.0
        while not self.stop_flag.is_set():
            now = time.time()
            s = {"t": round(now - self.serial.t0, 1), "phase": self.phase}
            if self.use_probe:
                code, body = http_get(self.base + "/probe")
                s["probe"] = code
                if code == 200:
                    try:
                        s["hr"] = json.loads(body).get("hashrate")
                    except ValueError:
                        s["probe_json"] = False
            if now >= next_info:
                next_info = now + self.info_s
                code, body = http_get(self.base + "/api/system/info")
                s["info"] = code
                if not self.use_probe:
                    s["probe"] = code
                if code == 200:
                    try:
                        d = json.loads(body)
                        s["info_ok"] = True
                        if not self.use_probe:
                            s["hr"] = d.get("hashrate")
                        s["data"] = {k: d.get(k) for k in (
                            "fwVersion", "poolConn", "temp", "wallet", "worker", "pool1Host", "pool2Host", "ssid",
                            "timezone", "brightness", "weatherLat", "weatherLon", "dgbUsd", "btcUsd", "weatherValid", "ntpOk", "rssi")}
                    except ValueError:
                        s["info_ok"] = False
                        s["info_len"] = len(body)
                if self.home_status is None:
                    self.home_status = http_get(self.base + "/")[0]
            self.samples.append(s)
            self.stop_flag.wait(self.probe_s)

    def set_page(self, n):
        return http_get(f"{self.base}/api/display/page?p={n}")[0] if self.base else None


# ----------------------------------------------------------------
# Auswertung
# ----------------------------------------------------------------
CRASH_RE = re.compile(r"Guru Meditation|Backtrace:|abort\(\)|Brownout|Stack canary|Task watchdog|assert failed|Stack smashing|stack overflow", re.I)
SETTINGS = ("wallet", "worker", "pool1Host", "pool2Host", "ssid", "timezone", "brightness", "weatherLat", "weatherLon")


def mean(v):
    return sum(v) / len(v) if v else None


def analyze(log_text, samples, home_status, version=None, baseline=None, expect=(), web_ab=15):
    """Liefert Liste (id, status, text). status: OK, FEHLER, WARNUNG, INFO.
    expect: Merkmale des Stands, z. B. {"preise7", "pm", "disp"} fuer die Design-Firmware."""
    R = []
    add = lambda i, st, tx: R.append((i, st, tx))
    # Reste aus dem Puffer des USB-Chips stammen noch von vor dem Neustart
    rom = log_text.find("ets Jul")
    t = log_text[rom:] if rom > 0 else log_text

    m = re.findall(r"=== (.+?) ===", t)
    if not m:
        add("firmware", "FEHLER", "Keine Startzeile im Log")
    elif version and not any(x.endswith("v" + version) for x in m):
        add("firmware", "FEHLER", f"Erwartet v{version}, im Log: {m[0]}")
    else:
        add("firmware", "OK" if version else "INFO", m[0])

    crashes = [l for l in t.split("\n") if CRASH_RE.search(l)]
    add("absturz", "FEHLER" if crashes else "OK", f"{len(crashes)} Absturzzeilen: {crashes[0][:100]}" if crashes else "Keine Absturzzeilen")
    add("neustarts", "FEHLER" if len(m) > 1 else "OK", f"{max(len(m), 0)} Start(s) im Log")

    w = IP_RE.search(t)
    if w:
        add("wlan", "OK", f"Verbunden, IP {w.group(1)}")
    else:
        add("wlan", "FEHLER", "Keine WLAN-Zeile im Log. Portal „DGB-Miner-Setup“ aktiv oder keine Zugangsdaten mehr?")

    if "DGB Wallet fehlt" in t:
        add("pool", "FEHLER", "Wallet fehlt in den Einstellungen")
    elif "Subscribe+Authorize" in t:
        add("pool", "OK", "Subscribe und Authorize gesendet")
    elif "TCP fehlgeschlagen" in t:
        add("pool", "FEHLER", "Pool-Verbindung fehlgeschlagen")
    else:
        add("pool", "FEHLER", "Keine Pool-Verbindung im Log")
    en = re.search(r"EN1=(\S*) sz=(\d+)", t)
    if en:
        sz = int(en.group(2))
        add("en2_laenge", "OK" if sz == 4 else "INFO", f"extranonce2_size = {sz}" + ("" if sz == 4 or "phase1" in expect else " (bestätigt E1: Firmware sendet nur 4 Byte, Fix in Phase 1)"))
    else:
        add("en2_laenge", "WARNUNG", "Keine Zeile EN1=… sz=… im Log")
    d = re.findall(r"Diff: (\S+)", t)
    add("pool_diff", "INFO" if d else "WARNUNG", f"Pool-Diff im Log: {d[-1]}" if d else "Keine Diff-Zeile im Log")
    jobs = len(re.findall(r"Job:", t))
    add("jobs", "OK" if jobs else "WARNUNG", f"{jobs} Jobs empfangen")
    drops = t.count("Getrennt")
    add("trennungen", "WARNUNG" if drops else "OK", f"{drops} Trennungen vom Pool")
    add("shares", "INFO", f"Gesendet {len(re.findall(r'Share: ', t))}, OK-Zeilen {t.count('Share OK')}, REJ-Zeilen {t.count('Share REJ')}." + ("" if "phase1" in expect else " Die OK-Zeilen enthalten die Authorize-Antwort (E2)."))
    diag = re.findall(r"\[DIAG\s*\] ?(.*)", t)
    if diag:
        add("diag", "INFO", "Letzte DIAG-Zeile: " + diag[-1][:150])

    # Phase 1: Selbsttest und echte Shares
    if "phase1" in expect:
        st = re.search(r"SELFTEST hash=\S+ diff=(\S+) netdiff=(\S+) (OK|FAIL)", t)
        add("selbsttest", "OK" if st and st.group(3) == "OK" else "FEHLER", f"diff={st.group(1)} netdiff={st.group(2)} {st.group(3)}" if st else "Keine SELFTEST-Zeile")
        au = "Authorize OK" in t
        add("authorize", "OK" if au else "FEHLER", "Pool hat die Wallet angenommen" if au else (re.search(r"Authorize abgelehnt: (.*)", t) or [None, "Keine Authorize-Antwort"])[1])
        rej = re.findall(r"Share REJ: (.*)", t)
        okc = t.count("Share OK")
        add("shares_pool", "OK" if okc else "FEHLER", f"{okc} akzeptiert, {len(rej)} abgelehnt" + (f", zuerst: {rej[0][:60]}" if rej else ""))
        if rej:
            add("ablehnungen", "WARNUNG", f"{len(rej)} Ablehnungen, Gründe: " + "; ".join(sorted(set(r[:40] for r in rej))[:4]))
        hw = re.findall(r"HWfehler=(\d+)", t)
        if hw:
            # Die Software rechnet jeden Treffer nach, falsche Shares gehen also nicht raus.
            mins = max(1, t.count("HWfehler="))
            add("hw_fehler", "OK" if not int(hw[-1]) else "FEHLER" if int(hw[-1]) > 3 * mins + 3 else "WARNUNG",
                f"{hw[-1]} Treffer der Hardware nicht bestätigt, in {mins} Minuten")
        ht = re.search(r"HWTEST (.*)", t)
        if ht:
            add("hw_selbsttest", "OK" if ht.group(1).startswith("OK") else "FEHLER", ht.group(1)[:70])
        sg = re.search(r"Diff-Wunsch abgelehnt: (.*)", t)
        if sg:
            add("diff_wunsch", "INFO", "Pool lehnt suggest_difficulty ab: " + sg.group(1)[:60])

    # Abrufe
    coins = re.findall(r"Coins (\d+)/(\d+)", t)
    if "preise7" in expect:
        add("kurse", "OK" if any(a == b for a, b in coins) else "FEHLER", f"Coins {coins[-1][0]}/{coins[-1][1]}" if coins else "Kein erfolgreicher Kurs-Abruf im Log")
    else:
        ok = re.search(r"\[PRICE\s*\] DGB=", t)
        add("kurse", "OK" if ok else "WARNUNG", "Kurs-Abruf erfolgreich" if ok else "Kein erfolgreicher Kurs-Abruf im Log")
    bad = re.search(r"\[WEATHER\s*\] (JSON-Parse fehlgeschlagen|Kein JSON in Response|HTTPS connect fehlgeschlagen)", t)
    good = re.search(r"\[WEATHER\s*\] (?!Abruf|Standort|JSON|Kein|HTTPS)(.+)", t)
    add("wetter", "OK" if good else "FEHLER" if bad else "WARNUNG", f"Wetter: {good.group(1)}" if good else (bad.group(1) if bad else "Kein Wetter-Ergebnis im Log"))
    if "pm" in expect:
        a = re.search(r"\[AIR\s*\] PM2\.5=(\d+) PM10=(\d+)", t)
        add("feinstaub", "OK" if a else "FEHLER", f"PM2.5 {a.group(1)}, PM10 {a.group(2)}" if a else "Kein PM-Ergebnis im Log")

    # Speicher und Zeichendauer (nur Design-Firmware)
    mem = [(n, int(h), int(mh), int(st)) for n, h, mh, st in re.findall(r"\[MEM\] (\w+) heap=(\d+) minheap=(\d+) stackfrei=(\d+)", t)]
    if "mem" in expect:
        if mem:
            mh, st = min(x[2] for x in mem), min(x[3] for x in mem)
            add("speicher", "WARNUNG" if mh < 20000 or st < 1024 else "OK", f"Kleinster freier Heap {mh} Byte, kleinster freier Task-Stack {st} Byte")
        else:
            add("speicher", "FEHLER", "Keine [MEM]-Zeile: TLS-Task nicht fertig geworden?")
    disp = [(int(p), int(ms)) for p, ms in re.findall(r"\[DISP\] page=(\d) ms=(\d+)", t)]
    if "disp" in expect:
        if disp:
            pages = sorted({p for p, _ in disp})
            mx = max(ms for _, ms in disp)
            add("zeichnen", "WARNUNG" if mx > 150 else "OK", f"Längste Zeichendauer {mx} ms, Seiten {pages}")
            if len(pages) < 5:
                add("seiten", "FEHLER", f"Nur Seiten {pages} gezeichnet")
            else:
                add("seiten", "OK", "Alle 5 Seiten gezeichnet")
        else:
            add("zeichnen", "FEHLER", "Keine [DISP]-Zeile im Log")

    # Web
    probes = [s for s in samples if "probe" in s and s.get("t", 99) >= web_ab]  # davor startet der Server noch
    ok = [s for s in probes if s["probe"] == 200]
    if not probes:
        add("web", "FEHLER", "Web nicht abgefragt: keine IP gefunden")
    else:
        add("web", "OK" if len(ok) == len(probes) else "WARNUNG" if ok else "FEHLER", f"Status {len(ok)}/{len(probes)} erreichbar, Dashboard HTTP {home_status}")
    infos = [s for s in samples if "info_ok" in s]
    if infos:
        bad_info = [s for s in infos if not s["info_ok"]]
        if bad_info:
            add("status_json", "WARNUNG", f"{len(bad_info)}/{len(infos)} ungültig, zuerst nach {bad_info[0]['t']} s (bestätigt E6)")
        else:
            add("status_json", "OK", f"{len(infos)}/{len(infos)} gültig")
    hrs = [s["hr"] for s in ok if s.get("hr") is not None and s["t"] > 30]
    if hrs:
        add("hashrate", "OK" if mean(hrs) > 0 else "FEHLER", f"Mittel {mean(hrs):.0f} H/s, Spanne {min(hrs)}–{max(hrs)} ({len(hrs)} Proben ab 30 s)")
    else:
        add("hashrate", "WARNUNG", "Keine Hashrate-Proben ab 30 s")
    for ph in ("ruhe", "wechsel"):
        v = [s["hr"] for s in ok if s.get("phase") == ph and s.get("hr") is not None]
        if v:
            add("hashrate_" + ph, "INFO", f"Phase {ph}: Mittel {mean(v):.0f} H/s ({len(v)} Proben)")
    data = [s["data"] for s in samples if s.get("data")]
    if data:
        last = data[-1]
        temps = [s["data"].get("temp") or 0 for s in samples if s.get("data")]
        add("temperatur", "WARNUNG" if max(temps) >= 85 else "INFO", f"Temperatur {last.get('temp')} °C, Spanne {min(temps)}–{max(temps)}")
        if last.get("ntpOk") is False:
            add("zeit", "WARNUNG", "NTP noch nicht synchron")
        if baseline:
            diff = [k for k in SETTINGS if baseline.get(k) != data[0].get(k)]
            # Feste Nennung der Felder, nicht der Werte: WLAN-Name und Wallet gehoeren nicht in den Bericht
            add("einstellungen", "FEHLER" if diff else "OK", "Abweichend: " + ", ".join(diff) if diff else "Alle Einstellungen erhalten")
    return R


def print_results(results):
    for i, st, tx in results:
        print(f"{st:8} {i:16} {tx}")
    n = sum(1 for r in results if r[1] == "FEHLER")
    w = sum(1 for r in results if r[1] == "WARNUNG")
    print(f"--> {n} Fehler, {w} Warnungen")
    return n


# ----------------------------------------------------------------
# Aufzeichnung
# ----------------------------------------------------------------
def capture(name, port, secs, version=None, base_url=None, cycle=False, expect=(), baseline_name=None,
            probe_s=5.0, info_s=15.0, settle=45.0, page_s=5.0, reset=True, use_probe=True, web_ab=15):
    LOGS.mkdir(parents=True, exist_ok=True)
    log_path = LOGS / f"{name}.log"
    ser = SerialLog(port, log_path)
    if reset:
        ser.reset()
    ser.start()
    web = WebSampler(ser, base_url, probe_s, info_s, use_probe)
    web.start()
    print(f"Aufzeichnung {name}: {secs} s, Port {port}", flush=True)
    t_end = time.time() + secs
    t_cycle = time.time() + settle + (secs - settle) / 2
    t_ruhe = time.time() + settle
    page = 0
    next_page = 0.0
    next_note = time.time() + 15
    while time.time() < t_end:
        now = time.time()
        if now >= t_ruhe and web.phase == "start":
            web.phase = "ruhe"
        if cycle and now >= t_cycle:
            web.phase = "wechsel"
            if now >= next_page:
                page = (page + 1) % 5
                web.set_page(page)
                next_page = now + page_s
        if now >= next_note:
            hr = next((s.get("hr") for s in reversed(web.samples) if s.get("hr") is not None), None)
            print(f"  noch {int(t_end - now)} s, {len(ser.lines)} Logzeilen, Hashrate {hr}", flush=True)
            next_note = now + 15
        time.sleep(0.2)
    if cycle:
        web.set_page(0)
    web.stop_flag.set()
    web.join(timeout=8)
    log_text = ser.text()
    ser.close()

    baseline = None
    if baseline_name:
        p = LOGS / f"{baseline_name}.web.json"
        if p.exists():
            for s in json.loads(p.read_text())["samples"]:
                if s.get("data"):
                    baseline = s["data"]
                    break
    results = analyze(log_text, web.samples, web.home_status, version, baseline, expect, web_ab)
    (LOGS / f"{name}.web.json").write_text(json.dumps({"base": web.base, "home": web.home_status, "samples": web.samples}, indent=0))
    (LOGS / f"{name}.ergebnis.json").write_text(json.dumps({"name": name, "version": version, "zeit": datetime.datetime.now().isoformat(timespec="seconds"), "ergebnisse": results}, indent=1, ensure_ascii=False))
    errors = print_results(results)
    print(f"Log: {log_path}")
    return errors


# ----------------------------------------------------------------
# Flash-Abbild
# ----------------------------------------------------------------
def parse_flash(data):
    """Liest Partitionstabelle und App-Beschreibung aus einem Flash-Abbild oder merged.bin."""
    parts = []
    for off in range(0x8000, 0x8000 + 0xC00, 32):
        e = data[off:off + 32]
        if len(e) < 32 or e[:2] != b"\xaa\x50":
            continue
        typ, sub, addr, size = e[2], e[3], *struct.unpack("<II", e[4:12])
        parts.append({"name": e[12:28].split(b"\0")[0].decode("ascii", "replace"), "type": typ, "subtype": sub, "offset": addr, "size": size})
    apps = []
    for p in parts:
        if p["type"] != 0:
            continue
        d = data[p["offset"] + 0x20:p["offset"] + 0x20 + 256]
        if len(d) >= 256 and struct.unpack("<I", d[:4])[0] == 0xABCD5432:
            apps.append({"partition": p["name"], "version": d[16:48].split(b"\0")[0].decode("ascii", "replace"),
                         "projekt": d[48:80].split(b"\0")[0].decode("ascii", "replace"),
                         "datum": d[96:112].split(b"\0")[0].decode("ascii", "replace"),
                         "idf": d[112:144].split(b"\0")[0].decode("ascii", "replace")})
    nvs = next((p for p in parts if p["type"] == 1 and p["subtype"] == 2), None)
    return {"partitionen": parts, "apps": apps, "nvs": nvs,
            "nvs_passt": bool(nvs and (nvs["offset"], nvs["size"]) == ZIEL_NVS),
            "nvs_nicht_leer": bool(nvs and data[nvs["offset"]:nvs["offset"] + nvs["size"]].count(0xFF) < nvs["size"])}


def run_cmd(cmd, retries=None):
    print("$ " + shlex.join(cmd), flush=True)
    return subprocess.call(cmd)


def sketch_hash(sketch):
    h = hashlib.sha256()
    d = ROOT / "firmware" / sketch
    for p in sorted(d.iterdir()):
        if p.is_file():
            h.update(p.name.encode())
            h.update(p.read_bytes())
    return h.hexdigest()


def build_files(key):
    s = SKETCHES[key]
    d = BUILD / s
    return d, d / f"{s}.ino.bin", d / f"{s}.ino.partitions.bin", d / f"{s}.ino.bootloader.bin"


def write_manifest():
    m = {}
    for key, s in SKETCHES.items():
        d, bin_, _, _ = build_files(key)
        if bin_.exists():
            m[key] = {"sketch": s, "version": VERSIONS[key], "bin_sha256": hashlib.sha256(bin_.read_bytes()).hexdigest(),
                      "bytes": bin_.stat().st_size, "quelle_sha256": sketch_hash(s),
                      "gebaut": datetime.datetime.fromtimestamp(bin_.stat().st_mtime).isoformat(timespec="seconds")}
    MANIFEST.write_text(json.dumps(m, indent=1))
    print(f"Manifest: {', '.join(m)}")


def build_problems(key):
    """Leere Liste, wenn der Build fertig ist und zu den Quellen passt."""
    d, bin_, parts, boot = build_files(key)
    out = [f"{f.name} fehlt" for f in (bin_, parts, boot) if not f.exists()]
    if out:
        return out
    if not MANIFEST.exists():
        return ["Manifest fehlt"]
    m = json.loads(MANIFEST.read_text()).get(key)
    if not m:
        return ["Nicht im Manifest"]
    if hashlib.sha256(bin_.read_bytes()).hexdigest() != m["bin_sha256"]:
        out.append("Binärdatei passt nicht zum Manifest")
    if sketch_hash(m["sketch"]) != m["quelle_sha256"]:
        out.append("Quellen seit dem Build geändert, neu kompilieren")
    return out


def latest_backup():
    p = BACKUP / "analyse.json"
    if not p.exists():
        return None
    a = json.loads(p.read_text())
    f = pathlib.Path(a.get("datei", ""))
    return a if f.exists() and f.stat().st_size == FLASH_SIZE else None


# ----------------------------------------------------------------
# Befehle
# ----------------------------------------------------------------
def cmd_check(a):
    bad = 0
    def line(ok, tx):
        nonlocal bad
        print(("OK      " if ok else "FEHLER  ") + tx)
        bad += 0 if ok else 1
    line(os.path.exists(ARDUINO_CLI), f"arduino-cli: {ARDUINO_CLI}")
    et = esptool_path()
    line(bool(et), f"esptool: {et}")
    for key in SKETCHES:
        p = build_problems(key)
        line(not p, f"Build {key}: " + ("; ".join(p) if p else "fertig, passt zu den Quellen"))
    ports = find_ports()
    if ports:
        for dev, vp, chip in ports:
            print(f"OK      Port {dev} ({vp}, {chip}), Zugriff: {'ja' if os.access(dev, os.R_OK | os.W_OK) else 'nein'}")
    else:
        print("INFO    Kein Port gefunden (Miner nicht angesteckt?)")
        bad += 1 if a.brauche_port else 0
    b = latest_backup()
    print(("OK      Backup: " + b["datei"]) if b else "INFO    Noch kein Backup")
    print("--> bereit" if not bad else f"--> {bad} Problem(e)")
    return 1 if bad else 0


def cmd_ausgangsstand(a):
    capture("00_ausgangsstand", pick_port(a.port), a.secs, base_url=a.url, expect=())
    return 0  # Der Ausgangsstand darf Fehler haben, er ist nur die Vergleichsbasis


def cmd_backup(a):
    et = esptool_path()
    if not et:
        sys.exit("esptool fehlt")
    BACKUP.mkdir(parents=True, exist_ok=True)
    os.chmod(BACKUP, stat.S_IRWXU)
    port = pick_port(a.port)
    path = BACKUP / f"flash_{datetime.datetime.now():%Y%m%d_%H%M%S}.bin"
    rc = -1
    for baud in (460800, 115200):
        rc = run_cmd([et, "--chip", "esp32", "--port", port, "--baud", str(baud), "read-flash", "0", hex(FLASH_SIZE), str(path)])
        if rc == 0:
            break
    if rc != 0:
        sys.exit("BACKUP FEHLGESCHLAGEN. Falls der Chip nicht antwortet: BOOT-Taste beim Start halten.")
    return finish_backup(path)


def finish_backup(path):
    os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
    data = path.read_bytes()
    if len(data) != FLASH_SIZE:
        sys.exit(f"BACKUP UNVOLLSTÄNDIG: {len(data)} Byte")
    info = parse_flash(data)
    info["datei"] = str(path)
    info["sha256"] = hashlib.sha256(data).hexdigest()
    (BACKUP / "analyse.json").write_text(json.dumps(info, indent=1))
    print(f"Backup: {path} ({len(data)} Byte, nur für dich lesbar, enthält WLAN-Passwort)")
    for p in info["partitionen"]:
        print(f"  {p['name']:10} typ {p['type']}/{p['subtype']:<3} {p['offset']:#9x} {p['size']:#9x}")
    for x in info["apps"]:
        print(f"  App in {x['partition']}: {x['projekt']} {x['version']} vom {x['datum']}, IDF {x['idf']}")
    if not info["nvs_passt"]:
        print(f"WARNUNG: NVS liegt nicht an {ZIEL_NVS[0]:#x}/{ZIEL_NVS[1]:#x}. WLAN und Einstellungen gehen beim Flashen verloren.")
        return 2
    print("NVS liegt an derselben Stelle wie im Ziel-Layout." + ("" if info["nvs_nicht_leer"] else " NVS ist aber leer."))
    return 0


def cmd_flash(a):
    problems = build_problems(a.was)
    if problems:
        sys.exit("BUILD NICHT BEREIT: " + "; ".join(problems))
    b = latest_backup()
    if not b:
        sys.exit("KEIN BACKUP: Erst `backup` ausführen. Ohne Sicherung wird nicht geflasht.")
    if not b.get("nvs_passt") and not a.nvs_egal:
        sys.exit("NVS PASST NICHT: Einstellungen gingen verloren. Nur mit --nvs-egal, nach Rückfrage beim Nutzer.")
    port = "/dev/ttyDRYRUN" if a.dry_run else pick_port(a.port)
    d = build_files(a.was)[0]
    for speed in (921600, 460800, 115200):
        cmd = [ARDUINO_CLI, "upload", "-p", port, "--fqbn", FQBN.format(speed=speed), "--input-dir", str(d), "--verify"]
        if a.dry_run:
            print("TROCKENLAUF: " + shlex.join(cmd))
            return 0
        if run_cmd(cmd) == 0:
            print(f"Geflasht: {a.was} v{VERSIONS[a.was]} mit {speed} Baud")
            return 0
        print(f"Upload mit {speed} Baud fehlgeschlagen.")
        time.sleep(1)
    sys.exit("FLASHEN FEHLGESCHLAGEN. BOOT-Taste halten und erneut versuchen, sonst Kabel und Port prüfen.")


def cmd_test(a):
    expect = set(a.erwarte.split(",")) if a.erwarte else set()
    e = capture(a.name, pick_port(a.port), a.secs, a.version, a.url, a.wechsel, expect, a.basis, use_probe=not a.ohne_probe)
    return 1 if e else 0


def cmd_seite(a):
    base = a.url
    if not base:
        for f in sorted(LOGS.glob("*.web.json"), key=lambda p: p.stat().st_mtime, reverse=True):
            base = json.loads(f.read_text()).get("base")
            if base:
                break
    if not base:
        sys.exit("IP unbekannt, bitte --url angeben")
    code, _ = http_get(f"{base}/api/display/page?p={a.n}")
    print(f"Seite {a.n}: HTTP {code}")
    return 0 if code == 200 else 1


def ws_send_text(base, text, path="/ws"):
    """Kleiner WebSocket-Client: Handshake, ein Textframe, Antwort lesen (falls sie kommt)."""
    host = base.split("//", 1)[1]
    name, _, port = host.partition(":")
    key = base64.b64encode(os.urandom(16)).decode()
    with socket.create_connection((name, int(port or 80)), timeout=8) as c:
        c.sendall((f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n" +
                  (f"Authorization: Basic {base64.b64encode(('admin:' + os.environ['TOMESSINO_WEB_PW']).encode()).decode()}\r\n" if os.environ.get("TOMESSINO_WEB_PW") else "") +
                  "\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = c.recv(1024)
            if not chunk:
                break
            head += chunk
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise RuntimeError("Kein WebSocket-Handshake: " + head[:60].decode("latin1"))
        data = text.encode()
        mask = os.urandom(4)
        frame = bytes([0x81])
        frame += bytes([0x80 | len(data)]) if len(data) < 126 else bytes([0x80 | 126]) + struct.pack(">H", len(data))
        frame += mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        c.sendall(frame)
        time.sleep(0.5)


def cmd_einstellen(a):
    """Speichert Einstellungen ueber das Dashboard-Protokoll. Der Miner startet danach neu."""
    base = a.url
    if not base:
        for f in sorted(LOGS.glob("*.web.json"), key=lambda p: p.stat().st_mtime, reverse=True):
            base = json.loads(f.read_text()).get("base")
            if base:
                break
    if not base:
        sys.exit("IP unbekannt, bitte --url angeben")
    msg = {"action": "save"}
    for kv in a.werte:
        k, _, v = kv.partition("=")
        msg[k] = int(v) if v.lstrip("-").isdigit() else v
    ws_send_text(base, json.dumps(msg))
    print("Gespeichert: " + ", ".join(k for k in msg if k != "action") + ". Der Miner startet neu.")
    return 0


def cmd_bericht(a):
    OUT.mkdir(exist_ok=True)
    lines = ["# Gerätetest: Bericht", "", f"Erstellt {datetime.datetime.now():%d.%m.%Y %H:%M}.", ""]
    b = latest_backup()
    if b:
        apps = "; ".join(f"{x['projekt']} {x['version']}" for x in b["apps"]) or "unbekannt"
        lines += [f"**Backup:** `{pathlib.Path(b['datei']).name}`. Vorher geflasht: {apps}. NVS passt: {'ja' if b['nvs_passt'] else 'NEIN'}.", ""]
    for f in sorted(LOGS.glob("*.ergebnis.json")):
        d = json.loads(f.read_text())
        lines += [f"## {d['name']}", "", f"Version {d['version'] or 'nicht vorgegeben'}, {d['zeit']}.", "", "| Status | Prüfung | Ergebnis |", "|---|---|---|"]
        lines += [f"| {st} | {i} | {tx.replace('|', '/')} |" for i, st, tx in d["ergebnisse"]]
        lines.append("")
    p = OUT / "BERICHT.md"
    p.write_text("\n".join(lines))
    print(p)
    return 0


def cmd_restore(a):
    if not a.ja:
        sys.exit("Überschreibt den ganzen Flash. Nur mit --ja.")
    path = pathlib.Path(a.datei)
    if not path.exists() or path.stat().st_size != FLASH_SIZE:
        sys.exit("Datei fehlt oder hat nicht 4 MB")
    return run_cmd([esptool_path(), "--chip", "esp32", "--port", pick_port(a.port), "--baud", "460800", "write-flash", "0", str(path)])


def cmd_manifest(a):
    write_manifest()
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--url", help="Basis-URL statt der IP aus dem Log")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("check"); p.add_argument("--brauche-port", action="store_true"); p.set_defaults(fn=cmd_check)
    p = sub.add_parser("ausgangsstand"); p.add_argument("--secs", type=int, default=90); p.set_defaults(fn=cmd_ausgangsstand)
    p = sub.add_parser("backup"); p.set_defaults(fn=cmd_backup)
    p = sub.add_parser("flash"); p.add_argument("was", choices=SKETCHES); p.add_argument("--dry-run", action="store_true"); p.add_argument("--nvs-egal", action="store_true"); p.set_defaults(fn=cmd_flash)
    p = sub.add_parser("test"); p.add_argument("name"); p.add_argument("--version"); p.add_argument("--secs", type=int, default=180)
    p.add_argument("--wechsel", action="store_true"); p.add_argument("--ohne-probe", action="store_true"); p.add_argument("--erwarte", default=""); p.add_argument("--basis", default="00_ausgangsstand"); p.set_defaults(fn=cmd_test)
    p = sub.add_parser("seite"); p.add_argument("n", type=int, choices=range(5)); p.set_defaults(fn=cmd_seite)
    p = sub.add_parser("einstellen"); p.add_argument("werte", nargs="+", help="schluessel=wert"); p.set_defaults(fn=cmd_einstellen)
    p = sub.add_parser("bericht"); p.set_defaults(fn=cmd_bericht)
    p = sub.add_parser("restore"); p.add_argument("datei"); p.add_argument("--ja", action="store_true"); p.set_defaults(fn=cmd_restore)
    p = sub.add_parser("manifest"); p.set_defaults(fn=cmd_manifest)
    a = ap.parse_args(argv)
    if a.cmd in ("ausgangsstand", "backup", "flash", "test", "restore") and not getattr(a, "dry_run", False):
        ensure_dialout(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
