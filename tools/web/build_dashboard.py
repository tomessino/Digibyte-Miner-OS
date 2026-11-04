#!/usr/bin/env python3
"""Baut aus tools/web/dashboard.html das Dashboard der Firmware (nur Pillow noetig).

Quelle bearbeiten, dann:  python3 tools/web/build_dashboard.py
Ausgabe: firmware/tomessino_dgb/dashboard_html.cpp
         tools/web/out/preview.html (mit Beispieldaten, ohne Miner zu oeffnen)
"""
import base64
import io
import json
import pathlib
import re
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "design"))
import emblem  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "tools/design/assets"
OUT_CPP = ROOT / "firmware/tomessino_dgb/dashboard_html.cpp"
OUT_PREVIEW = ROOT / "tools/web/out/preview.html"


def png_uri(im):
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def logo():
    src = Image.open(ASSETS / "2_miner_80_225_354_499.png").convert("RGB").resize((304, 304), Image.LANCZOS)
    mask = Image.new("L", src.size, 0)
    ImageDraw.Draw(mask).ellipse((2, 2, 301, 301), fill=255)
    src.putalpha(mask)
    im = src.resize((76, 76), Image.LANCZOS)
    return png_uri(im.quantize(48, method=Image.FASTOCTREE))


def figure():
    return png_uri(emblem.robot(168, 148).quantize(32, method=Image.FASTOCTREE))


def world_map():
    # Punktraster mit Kontinent-Umrissen aus dem Display-Generator
    gen = (ROOT / "tools/design/gen_assets.py").read_text()
    m = re.search(r"^CONTINENTS = (\[.*?^\])", gen, re.S | re.M)
    conts = eval(m.group(1), {})  # eigene Datei, nur Zahlenlisten
    poly = "".join('<polygon points="%s"/>' % " ".join(f"{lon + 180:g},{82 - lat:g}" for lon, lat in c) for c in conts)
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 360 140"><defs>'
           '<pattern id="d" width="3" height="3" patternUnits="userSpaceOnUse"><circle cx="1.5" cy="1.5" r=".85" fill="#0d3f9e"/></pattern>'
           f'<clipPath id="c">{poly}</clipPath></defs><rect width="360" height="140" fill="url(#d)" clip-path="url(#c)"/></svg>')
    return "data:image/svg+xml;base64," + base64.b64encode(svg.encode()).decode()


MOCK = {"hashrate": 583240, "hrCore1": 556000, "hrCore0": 27240, "minerMode": 2, "minerCore0": True, "hwErrors": 2, "sharesSent": 153, "sharesOk": 149,
        "sharesRej": 4, "uptime": 93784, "temp": 77.2, "freeHeap": 118608, "poolConn": True, "activePool": 1, "difficulty": 0.0016, "netDiff": 699064625.49,
        "height": 24488166, "lastDiff": 0.003943, "bestDiff": 0.1013, "bestDiffAllTime": 181.45, "blockHits": 0, "jobs": 1503, "lastNonce": "0x3a0a15cc",
        "poolDisp": "dgb-stratum.solominer.net:3333", "ip": "192.168.1.50", "wallet": "DBeispielAdresseNurFuerDieVorschau00000", "pool1Host": "dgb-stratum.solominer.net",
        "pool1Port": 3333, "pool2Host": "eu.digi.hmpool.io", "pool2Port": 3337, "worker": "dgb_worker1", "cpuMhz": 240, "brightness": 200, "screenTimeout": 60, "timezone": 1,
        "rssi": -57, "ssid": "Beispiel-WLAN", "time": "22:10:48", "date": "01.11.2026", "ntpOk": True, "dgbUsd": 0.003217, "dgbChg": -0.45, "btcUsd": 77853, "btcChg": 0.75,
        "weatherValid": True, "weatherTemp": 9.3, "weatherFeels": 7, "weatherHumidity": 77, "weatherPressure": 1013, "weatherWind": 12, "weatherCode": 3, "sunrise": "07:12", "sunset": "16:48",
        "weatherCity": "Berlin, Germany", "weatherLat": 52.52, "weatherLon": 13.40, "weatherAutoLoc": True, "weatherTempUnit": "C", "weatherSpeedUnit": "kmh", "weatherIntervalMin": 15,
        "watchlist": "cardano:ADA,dogecoin:DOGE,ecash:XEC,shiba-inu:SHIB,pepe:PEPE", "webPassSet": False, "fwName": "Tomessino-DGB", "fwVersion": "7.4.5",
        "log": ["[00:00:04][POOL] Authorize OK", "[00:00:05][POOL] Diff: 0.001", "[00:00:05][MINE] Job:2d6c5e br:0 cb:68/90 net:688729551", "[00:00:42][MINE] Share: 0xe63e4c1b", "[00:00:43][POOL] Share OK",
                "[00:02:37][POOL] Share REJ: 21 Job not found", "[00:03:00][DIAG] PoolDiff=0.0016 NetDiff=662238769 Best=0.1013 Shares/min=6 HR=583000H/s"],
        "swarmPeers": [{"worker": "werkstatt-esp32", "ip": "192.168.1.51", "hr": 583000, "ok": 412, "rej": 9, "bestDiffAT": 512.3, "blocks": 0, "jobs": 2210, "temp": 71.2},
                       {"worker": "<b>alt</b><img src=x>", "ip": "192.168.1.52", "hr": 12900, "ok": 8, "rej": 0, "bestDiffAT": 3.1, "blocks": 0, "jobs": -1, "temp": 64.0}]}


def main():
    html = (ROOT / "tools/web/dashboard.html").read_text()
    html = html.replace("@@LOGO@@", logo()).replace("@@FIGURE@@", figure()).replace("@@MAP@@", world_map())
    assert ")TOMFW" not in html and "@@" not in html
    OUT_CPP.write_text('// ERZEUGT von tools/web/build_dashboard.py aus tools/web/dashboard.html, nicht von Hand aendern.\n'
                       '#include "web.h"\nconst char DASH_HTML[] PROGMEM = R"TOMFW(' + html + ')TOMFW";\n')
    mock = ("<script>window.WebSocket=class{constructor(){this.readyState=1;setTimeout(()=>this.onopen&&this.onopen(),10)}"
            "send(m){if(m.includes('save'))console.log('save',m);setTimeout(()=>this.onmessage&&this.onmessage({data:JSON.stringify(" + json.dumps(MOCK) + ")}),20)}};</script>")
    OUT_PREVIEW.parent.mkdir(exist_ok=True)
    OUT_PREVIEW.write_text(html.replace("<script>", mock + "<script>", 1))
    print(f"Dashboard: {len(html.encode())} Byte")


if __name__ == "__main__":
    main()
