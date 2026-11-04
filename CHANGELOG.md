# Änderungen

## 7.6.0
- Hashrate rund 960 kH/s statt rund 600. Am Pool über 2 Stunden gemessen: 929 ± 42 kH/s mit abgelehnten Shares. Die Hardware-Schleife wartet feste Taktzahlen, statt das Busy-Register abzufragen, liegt im IRAM und zählt das Nonce-Wort direkt in Hardware-Byte-Reihenfolge. Der Software-Miner auf Kern 0 läuft dafür aus dem Flash, aus dem IRAM bremste er Kern 1. Details in `docs/TECHNIK.md`.
- Sicherungen für die schnelle Schleife: Selbsttest mit 4096 Nonces beim Start, Prüfung des letzten Nonce jeder Runde, Rückfall auf die langsamere Schleife bei gehäuften Fehlern. Das Startlog meldet `HWTEST OK 4096/4096 fest`.
- Der Zähler „HW-Fehler“ steigt jetzt deutlich, nach dem Start um einige Tausend, danach um bis zu 7 je Sekunde. Das sind verfälschte Lesezugriffe auf die SHA-Hardware. Jeder solche Treffer wird in Software verworfen, Shares sind nie falsch.
- Status-JSON: neue Diagnosewerte `hwChecks`, `hwCheckFails`, `hwTimed`, `hwCycLoop`, `hwCycAll`.
- Der Webserver läuft fest auf Kern 0 (`build_opt.h`). Der Pool-Socket sendet ohne Nagle-Verzögerung.
- Kursseite: Steigende Kurse zeigen die Änderung grün statt blau.
- Uhrenseite: BTC-Kurs und 24-h-Änderung sind grün bei steigendem und rot bei fallendem Kurs.
- WLAN-Start: Bis zu vier Verbindungsversuche, bevor das Setup-Portal aufgeht. Vorher öffnete ein einzelner Fehlschlag, etwa direkt nach einem Reset, sofort das Portal.
- Swarm-Seite im Dashboard: neue Spalte „Jobs“. Die Geräte senden ihre Jobs dafür im LAN-Broadcast mit.
- Swarm-Seite: Worker-Namen und IPs anderer Geräte werden als Text eingesetzt. Vorher konnte ein Gerät im LAN über seinen Namen HTML und Skripte ins Dashboard schleusen.
- Wetterseite: Der Feinstaubwert steht mit Einheit da („PM2.5 6µg/m³“).

## 7.5.0
- Neustartschleife bei festem Wetter-Standort behoben. Die Abrufe warteten blockierend auf Antworten, auf Kern 0 lief dann der Task-Watchdog ab.
- Wetter: Ein geänderter Ortsname reicht, die Koordinaten sucht der Miner bei Open-Meteo. Koordinaten außerhalb von ±90/±180 werden abgelehnt. Ein Fehler beim Abruf behält die alten Werte.
- Nach dem Einrichten über das WLAN-Portal startet der Miner neu. Vorher war das Dashboard erst nach einem Neustart von Hand erreichbar.
- Miner-Seite: Die erste Zeile zählt die Jobs. Die vier Symbole leuchten bei neuen Werten kurz grün. Gefundene Blöcke stehen in einer Plakette am DigiByte-Logo. Fußzeile neu als drei Kacheln.
- Kursseite: 7-Tage-Charts, wählbar als Kurse, Charts oder im Wechsel alle 10 Sekunden.
- RGB-LED auf der Rückseite: aus, feste Farbe oder Farbwechsel, mit Helligkeit. Vorschau im Dashboard ohne Speichern.
- Kurs- und Rangseite in denselben dunklen Farben wie die übrigen Seiten.
- Status-JSON maskiert Texte. Ein Ortsname mit Anführungszeichen machte es vorher ungültig.

## 7.4.5
- Eigene Grafiken: Roboter mit Chip, Symbole und Schriftzug sind jetzt per Code gezeichnet (`tools/design/emblem.py`).

## 7.4.4
- Dashboard kommt direkt aus dem Flash. Vorher kam je nach Speicherzustand eine leere Seite mit „200 OK“.

## 7.4.3
- WLAN mit Passwort im Dashboard. Das neue Netz wird erst getestet, sonst bleibt das alte.
- Temperatur in Device Status und Swarm. Fehlwerte des Sensors werden verworfen.
- Display zeichnet auch gedimmt weiter. „Nie dimmen“ lässt sich speichern.

## 7.4.2
- Falsche Block-Zählerstände aus früheren Versionen werden nicht mehr übernommen.

## 7.4.1
- Neues Web-Dashboard in DigiByte-Optik.

## 7.4.0
- Watchlist im Dashboard, optionales Dashboard-Passwort, Blockhöhe und letzte Share-Diff.
- Speichern löscht das Pool-Passwort nicht mehr.

## 7.2.0
- Hashrate von 12 auf rund 600 kH/s: direkter Zugriff auf die SHA-Hardware, eigene SHA-256 mit Midstate, zweiter Miner, eigene Tasks je Kern.

## 7.1.x
- Hash-Prüfung, Extranonce-Länge, Share- und Block-Bedingung korrigiert. Ab hier akzeptiert der Pool Shares.
- Selbsttest beim Start, Antworten über die ID zugeordnet, Ablehnungsgründe im Log.
- Status-JSON läuft nicht mehr über, Zeitzone nicht mehr doppelt, Speicherleck in den Abruf-Tasks behoben.

## 7.0.x
- Aufteilung in Module, neues Display mit fünf Seiten, Kurse und Feinstaubwerte.
