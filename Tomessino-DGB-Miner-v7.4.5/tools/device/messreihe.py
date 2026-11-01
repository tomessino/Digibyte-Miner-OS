#!/usr/bin/env python3
"""Misst die Rechenwege der Firmware am Geraet (Plan 2.6). Nur Standardbibliothek.

  python3 tools/device/messreihe.py NAME --url http://IP [--secs 90] [--modi 0:0,1:0,2:0,1:1,2:1]

Modus b:c0 = Rechenweg (0 mbedTLS, 1 Software, 2 Hardware) : zweiter Miner auf Kern 0.
Ergebnis: Tabelle und geraetetest/logs/messung_NAME.json. Die Hashrate kommt aus dem Status-JSON.
"""
import argparse
import json
import pathlib
import time

import geraetetest as g

NAMEN = {0: "mbedTLS", 1: "Software", 2: "Hardware"}


def info(base):
    code, body = g.http_get(base + "/api/system/info")
    if code != 200:
        return None
    try:
        return json.loads(body)
    except ValueError:
        return None


def measure(base, b, c0, secs, settle=15):
    code, body = g.http_get(f"{base}/api/miner/mode?b={b}&c0={c0}")
    if code != 200:
        raise SystemExit(f"Modus nicht gesetzt: HTTP {code}")
    print(f"Modus {NAMEN[b]} {'+ Kern 0' if c0 else 'nur Kern 1'}: Antwort {body}", flush=True)
    time.sleep(settle)
    start = info(base)
    t0 = time.time()
    hr, c1, c0v, bad = [], [], [], 0
    while time.time() - t0 < secs:
        d = info(base)
        if d is None:
            bad += 1
        else:
            hr.append(d["hashrate"])
            c1.append(d["hrCore1"])
            c0v.append(d["hrCore0"])
        time.sleep(5)
    end = info(base) or start
    r = {"backend": NAMEN[b], "core0": bool(c0), "hr": sum(hr) / max(1, len(hr)), "hr_min": min(hr or [0]), "hr_max": max(hr or [0]),
         "hr_kern1": sum(c1) / max(1, len(c1)), "hr_kern0": sum(c0v) / max(1, len(c0v)), "proben": len(hr), "ausfaelle": bad,
         "gesendet": end["sharesSent"] - start["sharesSent"], "ok": end["sharesOk"] - start["sharesOk"],
         "abgelehnt": end["sharesRej"] - start["sharesRej"], "hw_fehler": end["hwErrors"] - start["hwErrors"],
         "temp": end.get("temp"), "pool_diff": end.get("difficulty")}
    print(f"  {r['hr'] / 1000:8.1f} kH/s (K1 {r['hr_kern1'] / 1000:.1f}, K0 {r['hr_kern0'] / 1000:.1f}), Shares {r['ok']}/{r['gesendet']} ok, "
          f"abgelehnt {r['abgelehnt']}, HW-Fehler {r['hw_fehler']}, Ausfaelle {bad}, Pool-Diff {r['pool_diff']}", flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--url", required=True, help="z. B. http://192.168.1.50")
    ap.add_argument("--secs", type=int, default=90)
    ap.add_argument("--modi", default="0:0,1:0,2:0,1:1,2:1")
    a = ap.parse_args()
    out = []
    for m in a.modi.split(","):
        b, c0 = m.split(":")
        out.append(measure(a.url, int(b), int(c0), a.secs))
    g.LOGS.mkdir(parents=True, exist_ok=True)
    p = pathlib.Path(g.LOGS) / f"messung_{a.name}.json"
    p.write_text(json.dumps(out, indent=1))
    print(p)


if __name__ == "__main__":
    main()
