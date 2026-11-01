# Anleitung

DigiByte-SHA-256d-Solo-Miner für ESP32-2432S028R (CYD).

Ein Solo-Miner ist eine Lotterie. Bei rund 600 kH/s und der aktuellen Net-Diff dauert ein Block im Mittel über 100.000 Jahre. Akzeptierte Shares bringen im Solo-Pool keinen Ertrag.

## Erster Start

1. Firmware aufspielen, siehe [INSTALLATION.md](INSTALLATION.md).
2. Ohne gespeichertes WLAN öffnet der Miner das WLAN `DGB-Miner-Setup`. Verbinden, dann `192.168.4.1` im Browser öffnen und das eigene WLAN eintragen. Dort lassen sich auch Wallet und Pool 1 setzen.
3. Die IP steht auf der Miner-Seite des Displays. Das Dashboard liegt unter `http://IP`.

## Bedienung am Gerät

| Aktion | Wirkung |
|---|---|
| Aufs Display tippen | Nächste Seite, weckt ein gedimmtes Display |
| BOOT-Taste kurz | Nächste Seite, weckt ein gedimmtes Display |
| BOOT-Taste 3 s halten | Config-Modus: WLAN-Portal `DGB-Miner-Setup` |
| BOOT-Taste 10 s halten | Dashboard-Passwort löschen, Neustart |

## Display-Seiten

| Seite | Inhalt |
|---|---|
| Miner | Uhrzeit, Laufzeit, WLAN-Stärke, BTC-Kurs, Pool-Diff, Net-Diff, Best Diff (Session / gesamt), Shares (OK / gesamt), IP, Hashrate, Worker und Gesamt-Hashrate im LAN |
| Uhr | BTC-Kurs mit 24-h-Änderung, Hashrate, Uhrzeit, Datum, Pool |
| Kurse | DGB und bis zu 5 Coins der Watchlist |
| Wetter | Uhrzeit, aktuelles Wetter, Sonnenauf- und -untergang, Feinstaub, 3 Tage Vorschau |
| Rang | Best Diff aller Tomessino-Geräte im eigenen LAN. Ein weltweites Ranking gibt es nicht. |

## Dashboard

Das Dashboard hat eine Seitenleiste, oben stehen Pool-Status, Uhrzeit und Version. Auf dem Handy liegt die Navigation oben.

**Dashboard:** Hashrate, Shares, Pool-Diff, Net-Diff, Blockhöhe, Speicher, Hashrate je Kern.
**Swarm:** Andere Tomessino-Geräte im LAN (UDP-Port 55123).
**Logs:** Die letzten 25 Zeilen.
**Settings:** Jedes Speichern startet den Miner neu.

- **Pool:** Pool 1 ist geprüft. Pool 2 ist nur als Ausweichpool eingetragen und ungeprüft. Ein leeres Feld „Pool Password“ lässt das Passwort unverändert.
- **WLAN:** SSID und Passwort eintragen, dann „WLAN speichern“. Der Miner testet das Netz bis zu 15 Sekunden lang. Klappt es, speichert er es und startet neu. Klappt es nicht, geht er zurück ins alte WLAN, im Log steht dann `Neues WLAN nicht erreichbar, altes bleibt`. Das Passwort wird über HTTP unverschlüsselt übertragen. Getestet ist bisher nur der Fehlerfall.
- **Display dimmen nach:** Sekunden ohne Bedienung, bis das Display dunkler wird. 0 heißt nie. Gedimmt läuft die Anzeige weiter. Einen automatischen Seitenwechsel gibt es nicht.
- **Zeitzone:** Fester Wert, im Sommer von Hand auf 2 stellen, im Winter auf 1.
- **Market Watchlist:** Bis zu 5 Coins mit CoinGecko-ID (klein, z. B. `shiba-inu`) und Symbol (höchstens 5 Zeichen). Eine unbekannte ID bleibt auf dem Display bei `--`, im Log steht dann etwa `Coins 6/7`.
- **Wetter:** Der Standort kommt normal aus der IP und kann daneben liegen. Mit „Auto-Standort: Nein“ lassen sich Ort und Koordinaten fest eintragen.
- **Dashboard-Passwort:** Siehe unten.

## Dashboard-Passwort

Ohne Passwort kann jeder in deinem WLAN das Dashboard öffnen, Einstellungen ändern und deine Wallet-Adresse sehen.

Mit Passwort fragt der Browser danach, der Benutzername ist `admin`. Es gilt für das Dashboard, den WebSocket, `/api/system/info`, `/api/display/*` und `/api/miner/mode`. Offen bleibt `/probe`, dort stehen Hashrate, Worker-Name und IP, aber keine Wallet.

Die Übertragung läuft über HTTP und ist nicht verschlüsselt. Nimm kein Passwort, das du anderswo verwendest.

**Am Gerät noch nicht getestet.** Falls die Werte nach dem Setzen stillstehen, sendet der Browser die Anmeldung vermutlich nicht an den WebSocket. Die BOOT-Taste löscht das Passwort wieder (10 s), auch das ist ungetestet.

## Grenzen

- Die Hashrate liegt bei rund 600 kH/s. Über die eine SHA-Hardware sind laut Messung höchstens ca. 640 kH/s möglich, dazu kommen ca. 27 kH/s von Kern 0.
- Die Temperatur ist die Chip-Temperatur ohne Abgleich, also nur grob. Auf dem Testboard zeigt sie unter Volllast rund 77 °C. Ab 85 °C steht eine Warnung im Log, die Firmware drosselt aber nicht.
- Ein kleiner Teil der Shares wird mit `Job not found` abgelehnt, wenn sich Share und neuer Job kreuzen.

## Störungen

| Problem | Maßnahme |
|---|---|
| `DGB Wallet fehlt` im Log | Wallet im Dashboard unter Settings eintragen |
| Pool zählt nichts | Log prüfen: `Authorize abgelehnt`, `Share REJ` nennen den Grund |
| `HWTEST FAIL` beim Start | Der Miner rechnet dann nur in Software mit ca. 50 kH/s. Log sichern. |
| Falsche Farben, weißer Hintergrund | Das Panel braucht andere Werte für Inversion und Farbreihenfolge in `setup()`. Prüfen mit `/api/display/color?invert=0&bgr=1&bars=1`. |
| Flash-, Backup- und USB-Probleme | [TESTS.md](TESTS.md) |
