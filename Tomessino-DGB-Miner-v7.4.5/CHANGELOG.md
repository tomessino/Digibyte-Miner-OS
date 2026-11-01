# Änderungen

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
