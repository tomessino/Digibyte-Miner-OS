# Tomessino DGB Miner

DigiByte-SHA-256d-Solo-Miner für das ESP32-2432S028R („Cheap Yellow Display“, 2,8 Zoll, 320×240, ST7789, XPT2046).

![Web-Dashboard](docs/img/dashboard.jpg)

- **Rund 600 kH/s** über die SHA-Hardware des ESP32 und einen Software-Miner auf dem zweiten Kern
- **Stratum v1** mit Hauptpool und Ausweichpool, die Pool-Difficulty wird beim Start als Wunsch gesendet
- **Fünf Display-Seiten:** Miner, Uhr, Kurse, Wetter, LAN-Rang
- **Web-Dashboard** mit Echtzeit-Werten, WLAN, Pool, Watchlist und optionalem Passwort
- **Selbsttest beim Start:** Genesis-Hash und Vergleich der SHA-Hardware mit der Software

> **Das ist ein Lotterie-Miner.** Bei rund 600 kH/s und der aktuellen DigiByte-Difficulty dauert ein Block im Mittel über 100.000 Jahre. Akzeptierte Shares bringen im Solo-Pool keinen Ertrag. Das Projekt ist zum Basteln und Lernen gedacht.

## Schnellstart

Fertige Firmware flashen (löscht **alle** Einstellungen auf dem Board):

```bash
esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write-flash 0x0 Tomessino-DGB-Miner-v7.4.5-firmware.bin
```

Die Datei gibt es unter den Releases des Repositorys, falls dort eine eingestellt ist. Der passende Quellcode ist dieses Repository beim Tag `v7.4.5`. Selbst bauen: [docs/INSTALLATION.md](docs/INSTALLATION.md).

Danach:
1. Mit dem WLAN `DGB-Miner-Setup` verbinden und `192.168.4.1` öffnen.
2. Eigenes WLAN und DigiByte-Wallet eintragen.
3. Die IP vom Display ablesen und das Dashboard im Browser öffnen.

Bedienung: [docs/ANLEITUNG.md](docs/ANLEITUNG.md).

## Display

![Die fünf Display-Seiten, links mit Beispieldaten, rechts vor den ersten Daten](docs/img/display_seiten.png)

Am PC gerendert mit Beispieldaten, nicht vom Gerät fotografiert.

## Stand

Getestet auf einem einzelnen Board (ESP32-D0WD-V3, Revision 3.1) mit dem Pool `dgb-stratum.solominer.net`.

| Geprüft am Gerät | Ergebnis |
|---|---|
| Dauerlauf 30 Minuten (7.2.0) | kein Absturz, keine Trennung, 149 von 153 Shares akzeptiert |
| Lauf 12 Minuten (7.4.3) | kein Absturz, 66 von 66 Shares akzeptiert, ca. 600 kH/s |
| Dashboard unter Dauerlast (7.4.4): 410 Abrufe, nebenbei alle paar Sekunden HTTPS-Abrufe | alle vollständig, kein Neustart |

**Noch nicht geprüft:**
- Laufzeiten über 30 Minuten am Stück
- Ausfälle von WLAN und Pool über längere Zeit, Wechsel auf den Ausweichpool
- Ausweichpool `eu.digi.hmpool.io`
- Dashboard-Passwort und Erfolgsfall beim WLAN-Wechsel
- Andere Boards und Displays. Die Farbeinstellung in `setup()` gilt für das getestete Panel.

Details: [docs/TECHNIK.md](docs/TECHNIK.md), Tests: [docs/TESTS.md](docs/TESTS.md), Änderungen: [CHANGELOG.md](CHANGELOG.md).

## Lizenz

Copyright (C) 2026 Tomessino

Dieses Projekt steht unter der [GNU General Public License Version 3](LICENSE) (GPL-3.0-only). Du darfst es nutzen, ändern und weitergeben, auch kommerziell. Wer eine veränderte oder darauf aufbauende Version weitergibt, muss deren Quellcode ebenfalls unter GPL-3.0 bereitstellen. Es gibt keine Garantie und keine Haftung, im Einzelnen gilt der Lizenztext.

Das DigiByte-Logo ist davon ausgenommen. Bibliotheken und Schriften haben eigene Lizenzen: [THIRD_PARTY.md](THIRD_PARTY.md).
