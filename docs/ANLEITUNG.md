# Anleitung

DigiByte-SHA-256d-Solo-Miner für ESP32-2432S028R (CYD).

Ein Solo-Miner ist eine Lotterie. Bei rund 960 kH/s und der aktuellen Net-Diff dauert ein Block im Mittel über 100.000 Jahre. Akzeptierte Shares bringen im Solo-Pool keinen Ertrag.

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
| Miner | Uhrzeit, Laufzeit, WLAN-Stärke, BTC-Kurs, Jobs seit dem Start, Net-Diff, Best Diff (Session / gesamt), Shares (abgelehnt / angenommen, abgelehnte rot), IP, Hashrate, Best Diff gesamt, Zahl der Miner und Gesamt-Hashrate im LAN. Die Zahl in der Plakette am DigiByte-Logo zählt gefundene Blöcke. |
| Uhr | BTC-Kurs mit 24-h-Änderung, Hashrate, Uhrzeit, Datum, Pool |
| Kurse | DGB und bis zu 5 Coins der Watchlist. Als Kurse mit 24-h-Änderung, als 7-Tage-Charts oder im Wechsel alle 10 Sekunden. |
| Wetter | Uhrzeit, aktuelles Wetter, Sonnenauf- und -untergang, Feinstaub, 3 Tage Vorschau |
| Rang | Best Diff aller Tomessino-Geräte im eigenen LAN. Ein weltweites Ranking gibt es nicht. |

Auf der Miner-Seite leuchten die vier Symbole links neben den Zählern kurz grün, wenn ein neuer Wert kommt: neuer Job, neue Net-Diff, neue Best Diff, akzeptierter Share.

Kursänderungen sind grün, wenn der Kurs gestiegen ist, und rot, wenn er gefallen ist. Auf der Uhrenseite gilt das für den BTC-Kurs und seine 24-h-Änderung. In der Chart-Ansicht ist die Linie grün, wenn der Kurs über 7 Tage gestiegen ist, sonst rot. Rechts steht die Änderung über 7 Tage („7T“). Die Charts kommen alle 30 Minuten von CoinGecko.

Auf der Wetterseite steht rechts „PM2.5“ mit dem aktuellen Feinstaubwert in µg/m³. In den drei Vorschauzeilen stehen in Klammern die Tagesmittel für PM2.5 und PM10, ebenfalls in µg/m³. Die Werte kommen von der Luftqualitäts-API von Open-Meteo. Das sind Modellwerte für den Standort, keine Messstation.

## Dashboard

Das Dashboard hat eine Seitenleiste, oben stehen Pool-Status, Uhrzeit und Version. Auf dem Handy liegt die Navigation oben.

**Dashboard:** Hashrate, Shares, Pool-Diff, Net-Diff, Blockhöhe, Jobs seit dem Start, Speicher, Hashrate je Kern.
**Swarm:** Andere Tomessino-Geräte im LAN (UDP-Port 55123). Sie müssen im selben Netz hängen, Broadcasts gehen nicht über Router. Angezeigt mit Hashrate, Temperatur, Best Diff, Blöcken, Jobs seit dem Start und Shares. Geräte mit Firmware vor 7.6.0 senden keine Jobs, dort steht „--“.
**Logs:** Die letzten 25 Zeilen.
**Settings:** Jedes Speichern startet den Miner neu.

- **Worker-Name:** Trennt mehrere Miner im Pool, im Swarm und in der Rangliste. Zwei Wege: den Namen hinter die Wallet schreiben (`Adresse.name`, so wie es Pools erwarten) oder ins Feld „Worker-Name“. Steht ein Name hinter der Wallet, gilt dieser. Ist beides leer, heißt der Miner `dgb_worker1`. Erlaubt sind Buchstaben, Ziffern, `_` und `-`, höchstens 32 Zeichen.
- **Zähler:** „Jobs und Shares zurücksetzen“ und „Best Diff zurücksetzen“ wirken sofort, ohne Neustart. Best Diff gilt dabei für die Sitzung und den gespeicherten Gesamtwert. Gefundene Blöcke bleiben.
- **Pool:** Pool 1 ist geprüft. Pool 2 ist nur als Ausweichpool eingetragen und ungeprüft. Ein leeres Feld „Pool Password“ lässt das Passwort unverändert.
- **WLAN:** SSID und Passwort eintragen, dann „WLAN speichern“. Der Miner testet das Netz bis zu 15 Sekunden lang. Klappt es, speichert er es und startet neu. Klappt es nicht, geht er zurück ins alte WLAN, im Log steht dann `Neues WLAN nicht erreichbar, altes bleibt`. Das Passwort wird über HTTP unverschlüsselt übertragen. Getestet ist bisher nur der Fehlerfall.
- **Display dimmen nach:** Sekunden ohne Bedienung, bis das Display dunkler wird. 0 heißt nie. Gedimmt läuft die Anzeige weiter. Einen automatischen Seitenwechsel gibt es nicht.
- **Zeitzone:** Fester Wert, im Sommer von Hand auf 2 stellen, im Winter auf 1.
- **RGB-LED (Rückseite):** Aus, feste Farbe oder Farbwechsel, dazu die Helligkeit in Prozent. Die LED zeigt Änderungen sofort, gespeichert wird erst mit Save.
- **Market Watchlist:** Bis zu 5 Coins mit CoinGecko-ID (klein, z. B. `shiba-inu`) und Symbol (höchstens 5 Zeichen). Eine unbekannte ID bleibt auf dem Display bei `--`, im Log steht dann etwa `Coins 6/7`. „Kursseite am Display“ wählt Kurse, 7-Tage-Charts oder den Wechsel alle 10 Sekunden.
- **Wetter:** Der Standort kommt normal aus der IP und kann daneben liegen. Mit „Auto-Standort: Nein“ gilt ein fester Ort. Es reicht, den Ortsnamen zu ändern: Das Dashboard leert dann die Koordinaten, der Miner sucht den Ort bei Open-Meteo und speichert die gefundenen Koordinaten. Im Log steht `Ort gefunden: …` oder `Ort nicht gefunden, Koordinaten bleiben`. Eingetragene Koordinaten gelten direkt, erlaubt sind Breite −90 bis 90 und Länge −180 bis 180.
- **Dashboard-Passwort:** Siehe unten.

## Dashboard-Passwort

Ohne Passwort kann jeder in deinem WLAN das Dashboard öffnen, Einstellungen ändern und deine Wallet-Adresse sehen.

Mit Passwort fragt der Browser danach, der Benutzername ist `admin`. Es gilt für das Dashboard, den WebSocket, `/api/system/info`, `/api/display/*` und `/api/miner/mode`. Offen bleibt `/probe`, dort stehen Hashrate, Worker-Name und IP, aber keine Wallet.

Die Übertragung läuft über HTTP und ist nicht verschlüsselt. Nimm kein Passwort, das du anderswo verwendest.

**Am Gerät noch nicht getestet.** Falls die Werte nach dem Setzen stillstehen, sendet der Browser die Anmeldung vermutlich nicht an den WebSocket. Die BOOT-Taste löscht das Passwort wieder (10 s), auch das ist ungetestet.

## Grenzen

- Die Hashrate liegt bei rund 960 kH/s: rund 940 kH/s über die SHA-Hardware auf Kern 1, rund 22 kH/s in Software auf Kern 0. Die Schleife braucht 252 Takte je Hash, die Hardware allein rund 242. Mehr ist mit diesem Aufbau kaum möglich, siehe `docs/TECHNIK.md`.
- Der Pool zählt etwas weniger als die Anzeige, weil ein kleiner Teil der Shares als veraltet abgelehnt wird (`Job not found`, im 2-Stunden-Lauf 1,6 %).
- Die Temperatur ist die Chip-Temperatur ohne Abgleich, also nur grob. Auf dem Testboard zeigt sie unter Volllast rund 77 °C. Ab 85 °C steht eine Warnung im Log, die Firmware drosselt aber nicht.
- Ein kleiner Teil der Shares wird mit `Job not found` abgelehnt, wenn sich Share und neuer Job kreuzen.

## Störungen

| Problem | Maßnahme |
|---|---|
| `DGB Wallet fehlt` im Log | Wallet im Dashboard unter Settings eintragen |
| Pool zählt nichts | Log prüfen: `Authorize abgelehnt`, `Share REJ` nennen den Grund |
| `HWTEST FAIL` beim Start | Der Miner rechnet dann nur in Software mit ca. 55 kH/s. Log sichern. |
| Falsche Farben, weißer Hintergrund | Das Panel braucht andere Werte für Inversion und Farbreihenfolge in `setup()`. Prüfen mit `/api/display/color?invert=0&bgr=1&bars=1`. |
| Dashboard nach dem WLAN-Portal nicht erreichbar | Ab 7.5.0 startet der Miner nach dem Portal selbst neu. Sonst einmal vom Strom nehmen. |
| Portal `DGB-Miner-Setup` geht auf, obwohl das WLAN gespeichert ist | Der Miner hat das WLAN beim Start nicht erreicht. Ab 7.6.0 versucht er es viermal, das Portal kommt nach spätestens rund 85 s. Nach 3 Minuten ohne Eingabe startet er neu und versucht es wieder. Das gespeicherte WLAN bleibt erhalten. |
| Flash-, Backup- und USB-Probleme | [TESTS.md](TESTS.md) |
