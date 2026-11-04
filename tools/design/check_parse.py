#!/usr/bin/env python3
"""Rechnet die Erwartung aus den echten API-Antworten unabhaengig nach
und vergleicht sie mit der Ausgabe von net_parse.cpp (parse.txt des PC-Tests)."""
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "tools/design/testdata"
IDS = ["digibyte", "bitcoin", "cardano", "dogecoin", "ecash", "shiba-inu", "pepe"]


def rnd(v):
    return int(math.floor(v + 0.5))


got = (ROOT / "tools/design/out/parse.txt").read_text().split("\n")
fails = 0


def check(name, ok):
    global fails
    print(f"{name:<52} {'OK' if ok else 'FEHLER'}")
    fails += 0 if ok else 1


prices = json.load(open(DATA / "prices.json"))
for i, cid in enumerate(IDS):
    _, ok, usd, chg = got[i].split()
    e = prices[cid]
    check(f"Kurs {cid}: {float(usd):.8g} {float(chg):+.2f}%",
          ok == "1" and math.isclose(float(usd), e["usd"], rel_tol=1e-5)
          and math.isclose(float(chg), e["usd_24h_change"], rel_tol=1e-4))

air = json.load(open(DATA / "air.json"))
h = air["hourly"]
line = got[7].split()
check(f"PM jetzt {line[2]}/{line[3]}", line[1] == "1" and int(line[2]) == rnd(air["current"]["pm2_5"]) and int(line[3]) == rnd(air["current"]["pm10"]))
for d in range(3):
    f = 24 * (d + 1)
    assert h["time"][f].endswith("T00:00")
    e25 = rnd(sum(h["pm2_5"][f:f + 24]) / 24)
    e10 = rnd(sum(h["pm10"][f:f + 24]) / 24)
    line = got[8 + d].split()
    check(f"PM Tag {h['time'][f][:10]}: {line[2]}/{line[3]}", line[1] == "1" and int(line[2]) == e25 and int(line[3]) == e10)

markets = {m["id"]: m["sparkline_in_7d"]["price"] for m in json.load(open(DATA / "markets.json"))}
for i, cid in enumerate(IDS):
    raw = [v for v in markets[cid] if v is not None][:200]
    n = min(len(raw), 48)
    exp = []
    for k in range(n):
        a, b = k * len(raw) // n, (k + 1) * len(raw) // n
        b = max(b, a + 1)
        exp.append(sum(raw[a:b]) / (b - a))
    f = got[11 + i].split()
    vals = [float(v) for v in f[2:]]
    check(f"Chart {cid}: {f[1]} Punkte aus {len(raw)}",
          f[0] == "spark" and int(f[1]) == n and all(math.isclose(v, e, rel_tol=1e-5) for v, e in zip(vals, exp)))

print("PARSER OK" if not fails else "PARSER FEHLGESCHLAGEN")
sys.exit(1 if fails else 0)
