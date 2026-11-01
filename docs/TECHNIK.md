# Technik

## Aufbau

| Datei | Inhalt |
|---|---|
| `tomessino_dgb.ino` | `setup()`, App-Task mit den Timern |
| `mining_core.*` | SHA-256 mit Midstate, Diff, Merkle, Header, Selbsttest (ohne Arduino) |
| `mining.*` | SHA-Hardware, Miner-Tasks, Queue für Lösungen |
| `stratum_core.*` | Stratum-Nachrichten, Submit (ohne Arduino) |
| `stratum.*` | Socket und Verbindung |
| `net_parse.*`, `net_services.*` | Kurse, Wetter, Luftqualität, Zeit, LAN-Swarm |
| `ui.*`, `ui_assets*` | Display-Seiten und Renderer (ohne Arduino) |
| `display.*` | Touch, BOOT-Taste, Daten für das Display |
| `web.*`, `dashboard_html.cpp` | Status-JSON, WebSocket, Dashboard |

- **Kern 1:** Miner über die SHA-Hardware.
- **Kern 0:** App-Task (Socket, Display, Web, Abrufe) und ein Software-Miner mit niedriger Priorität.
- Jeder Miner rechnet auf einer eigenen Job-Kopie. Die Miner unterscheiden sich im Extranonce2, rechnen also nie dieselben Hashes.

## Hashrate

Am Testboard gemessen:

| Stufe | Hashrate |
|---|---|
| mbedTLS | 12 kH/s |
| Eigene SHA-256 mit Midstate | 32 kH/s |
| SHA-Hardware direkt, mit `volatile` | 377 kH/s |
| SHA-Hardware ohne `memw` je Zugriff | 557 kH/s |
| Dazu Software-Miner auf Kern 0 | rund 600 kH/s |

Die Hardware kann einen Zwischenstand nur auslesen, nicht laden. Pro Nonce braucht sie also drei Blöcke, ein Midstate geht nur in Software. Gemessen sind je Block rund 83 Takte, je `LOAD` rund 29, dazu 34 Schreibzugriffe. Das ergibt mindestens rund 375 Takte je Hash, also höchstens rund 640 kH/s über die eine Hardware bei 240 MHz.

Beim Start vergleicht `HWTEST` die Hardware über 256 Nonces mit der Software. Bei Abweichung oder einem Chip unter Revision 3 läuft nur die Software. Jeder Treffer des Vorfilters wird zusätzlich vollständig in Software nachgerechnet.

## Bekannte Punkte

- **Unbestätigte Hardware-Treffer:** Die Hardware meldet gelegentlich einen Treffer, den die Software nicht bestätigt, in den Tests zwischen 0,3 und 3 pro Minute. Falsche Shares gehen dadurch nicht raus. Die Ursache ist nicht geklärt, die Share-Rate passt zur Hashrate.
- **`Job not found`:** Ein kleiner Teil der Shares wird abgelehnt, wenn sie sich mit einem neuen Job kreuzen, im Test rund 2 %.
- **Ungeklärter Neustart:** Bei einem Lasttest ohne serielles Log startete das Board einmal neu. Die Ursache ist unbekannt. Weitere 410 Abrufe mit gleicher Last liefen ohne Neustart. Der kleinste freie Heap lag dabei bei rund 25 KB, im Normalbetrieb bei rund 39 KB.
- **Temperatur:** Der Chip-Sensor ist nicht abgeglichen und liefert oft den Fehlwert 53,33 °C, der verworfen wird. Der angezeigte Wert ist nur grob.
- **Sommerzeit:** Die Zeitzone ist ein fester Wert.
- **Speicher:** In 30 Minuten blieb der freie Heap stabil bei rund 106 KB, längere Läufe sind nicht geprüft.
- **Web-Sicherheit:** Das Dashboard läuft über unverschlüsseltes HTTP und ist ohne Passwort für jeden im Netz erreichbar. `setInsecure()`: Die HTTPS-Abrufe prüfen keine Zertifikate.
