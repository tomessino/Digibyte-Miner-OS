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
| `display.*` | Touch, BOOT-Taste, Daten für das Display, Leuchten der Symbole, RGB-LED |
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
| Block 2 schreiben, während Block 1 rechnet (7.6.0-t3) | rund 670 kH/s |
| Feste Wartezeiten statt Pollen (7.6.0-t6) | rund 790 kH/s |
| Keine Pause je Runde auf Kern 1, Wartezeiten 52/54/2 (7.6.0-t10) | rund 840 kH/s |
| Schleife im IRAM, exakte NOP-Wartezeiten, Block 1 während LOAD 2, w3 statt Nonce zählen (7.6.0-t38) | rund 960 kH/s |

Die Hardware kann einen Zwischenstand nur auslesen, nicht laden. Pro Nonce braucht sie also drei Blöcke und zwei `LOAD`, ein Midstate geht nur in Software.

Gemessen am 03.11.2026 mit eigenen Mess-Sketches, Takte bei 240 MHz:

| Messung | Ergebnis |
|---|---|
| Block mit Pollen bis Busy=0 | 89 Takte |
| Block fertig nach (ab Schreiben des Startbefehls) | 68 bis 71 Takte |
| `LOAD` mit Pollen / fertig nach | 25 / 19 Takte |
| Ein Lesezugriff auf ein SHA-Register | rund 7 Takte |
| 16 Schreibzugriffe mit `memw` | 37 Takte |
| Kleinste fehlerfreie Wartezeit ab dem `memw` | Block 50, CONTINUE 51, `LOAD` 0 |

Was daraus folgt:

- Pollen kostet je Warten rund 15 Takte. Bei fünf Wartestellen je Nonce ist das der größte Verlust.
- START übernimmt den Text während des `memw`. Danach darf der nächste Text geschrieben werden, `T[15]` erst 2 Takte später.
- CONTINUE übernimmt den Text nicht sofort. Wer danach `T[8]` oder `T[15]` schreibt, bekommt falsche Hashes. Daran scheiterte die erste Fassung (7.6.0-t1).
- Ein `LOAD` schreibt nur `T[0..7]`. Die hintere Hälfte von Block 1 lässt sich deshalb schreiben, während Block 3 rechnet.
- Ohne `memw` nach dem Startbefehl rechnet die Hardware falsch.
- Zu knappe Wartezeiten machen fast jeden Hash falsch. Bei einer Wartezeit 2 Takte unter dem Mindestwert waren 1 bis 3 von 1024 Hashes falsch, bei 4 Takten darunter rund 1000.

Ab 7.6.0-t6 wartet die Schleife bei 240 MHz feste Zeiten statt zu pollen. Bis t10 wurden die Zeiten mit ccount gemessen, ab t11 sind die reinen Wartestellen exakte NOPs. Ein zweiter Mess-Sketch hat jede Stelle einzeln abgetastet (Werte ab dem `memw`, im IRAM):

| Stelle | fehlerfrei ab | in der Firmware (t38) |
|---|---|---|
| Block 1 und 3 (ccount) | 52, darunter schleichend falsch | 54 |
| CONTINUE: `T[8]`/`T[15]` für Block 3 schreiben | 8 NOPs | 9 |
| CONTINUE gesamt (inklusive der zwei Zugriffe) | 56 NOPs | 9 + 48 |
| `LOAD` 1 vor Block 3 | 4 NOPs oder weniger | 5 |
| `LOAD` 2, dann `b[0..6]` schreiben | 3 NOPs, darunter Rennen | 5 |
| `LOAD` 2 vor dem Lesen von `H7` | 8 NOPs | durch die Zugriffe erfüllt |

Was sonst noch Takte brachte, jeweils 2 Minuten am Gerät gemessen:

| Änderung | Kern 1 | Takte je Hash in der Schleife |
|---|---|---|
| t10: ccount-Wartezeiten 52/54/2 | 812 kH/s | rund 294 |
| t11/t12: NOPs, `T[8]`/`T[15]` während CONTINUE | 860 kH/s | – |
| t13: vordere Hälfte von Block 1 während `LOAD` 2 schreiben | 864 kH/s | 264 |
| t21: Schleife, `minerTask` und Prüf-Hash im IRAM, Software auf Kern 0 aus dem Flash | 888 kH/s | 268 |
| t23/t26: NOP-Reserven auf +1 | 900 kH/s | 264 |
| t31/t38: Schleife zählt `w3` (den byte-vertauschten Nonce) statt des Nonce | 941 kH/s | 252 |

Was nichts brachte oder schadete: zwei Nonces je Durchlauf (+3 Takte), START für den nächsten Nonce vor Filter und Schleife (+4), Vertauschen des Nonce in die CONTINUE-Pause verlegt (+18), 50 000 statt 20 000 Nonces je Runde (Kern 0 verlor 5 kH/s).

Wichtige Befunde dabei:

- **Block 1 ist CPU-begrenzt.** Die 16 Schreibzugriffe für Block 2 dauern länger, solange die Hardware rechnet. Jeder zusätzliche Befehl dort kostet direkt einen Takt (6 NOPs: +6 Takte). Deshalb zählt die Schleife `w3` hoch, das Textwort `T[3]`. Der echte Nonce ist `w3` byte-vertauscht und wird nur bei einem Treffer berechnet. Abgedeckt ist derselbe Nonce-Raum in anderer Reihenfolge.
- **IRAM statt Flash.** Aus dem Flash-Cache hängt die Dauer der NOPs von der Code-Ausrichtung ab. Ein bloß anders übersetzter Build war 2,5 % schneller oder langsamer. Im IRAM ist die Dauer fest.
- **Kern 0 nicht im IRAM.** Rechnet der Software-Miner auf Kern 0 aus dem IRAM, bremst er die Schleife auf Kern 1 über den gemeinsamen Speicher um rund 10 %. Aus dem Flash schafft Kern 0 22 statt 27 kH/s, das ist netto besser.
- **`__builtin_bswap32`** wird hier zum Aufruf von `__bswapsi2` im ROM.
- **`async_tcp`** lief auf beliebigem Kern mit Priorität 10, also auch zwischen den Runden auf Kern 1. `build_opt.h` bindet ihn an Kern 0 (`CONFIG_ASYNC_TCP_RUNNING_CORE=0`). Messbar war das nicht, es passt aber zur Aufteilung.

Drei Sicherungen:

1. `HWTEST` prüft beim Start 4096 Nonces in der Schleife mit festen Wartezeiten. Schlägt das fehl oder läuft der Chip nicht mit 240 MHz, wird gepollt. Mit Block-Wartezeit 52 fiel der Test einmal durch, dann läuft der Miner bis zum Neustart mit rund 690 kH/s.
2. Jede Runde (20 000 Nonces) rechnet den letzten Nonce in Software nach. Sind in 256 Runden mindestens 25 % falsch, schaltet die Firmware auf Pollen um.
3. Nach dem Sperren der Hardware wartet `hwWaitIdle()`, bis keine SHA-Art mehr rechnet. Das verlangt ESP-IDF für direkte Registerzugriffe (`sha_parallel_engine.h`). Bis 7.6.0-t4 fehlte das.

**Unbestätigte Treffer (HW-Fehler).** Ab t31 steigt der Zähler deutlich: in den ersten rund 40 Sekunden nach dem Start um rund 4000, danach um 1 bis 7 je Sekunde (15 Minuten t38: 6220). Der gelesene Wert von `H7` ist dabei genau `0x00000000` oder `0x02000000`, nicht der Hash. Ein Lesezugriff auf das SHA-Register ist also verfälscht, vermutlich wenn der andere Kern gleichzeitig auf Peripherie zugreift (in der Startphase viele TLS-Verbindungen). Bei rund 940 000 Lesezugriffen je Sekunde ist das etwa 1 bis 7 zu einer Million. Jeder solche Treffer wird in Software verworfen, Shares sind also nie falsch. Verloren geht höchstens ein Treffer in dieser Größenordnung. Abhilfe wäre ein `memw` vor dem Lesen, das kostet nach Schätzung rund 2 % Hashrate.

Pool-Messungen (Shares × Pool-Diff × 2^32 / Zeit, ± eine Standardabweichung):

| Stand | Dauer | angezeigt | am Pool, nur angenommene | mit abgelehnten (veraltet) |
|---|---|---|---|---|
| t6 | 15 min | 794 kH/s | 811 ± 111 kH/s | 826 kH/s |
| t10 | 15 min | 840 kH/s | 685 ± 101 kH/s | 730 kH/s |
| t38 | 15 min | 971 kH/s | 779 ± 109 kH/s | 811 kH/s |
| t38 | 2 h | 968 kH/s | 913 ± 42 kH/s | 929 kH/s |
| t38 | 9,1 h seit Start | rund 965 kH/s | 938 ± 20 kH/s | 963 kH/s |

Die 2-Stunden-Messung lief am 04.11.2026 von 07:04 bis 09:04. Sie zeigt keinen Neustart, die Diff lag durchgehend bei 0,0032 und der Pool war durchgehend verbunden. Angenommen wurden 478 Shares, abgelehnt 8. In halbstündigen Abschnitten lag der Pool-Wert zwischen 798 und 1026 kH/s, die Anzeige blieb bei 967 bis 969. So stark streuen kurze Messungen.

Die letzte Zeile beruht auf den Zählern seit dem Start: 2248 Shares angenommen, 60 abgelehnt, in 32 935 s. Dabei ist angenommen, dass die Diff die ganze Zeit 0,0032 war. Gesehen ist das nur in den beiden Messungen, zwischen 00:13 und 07:04 nicht. Mit den abgelehnten Shares trifft dieser Wert die Anzeige genau.

Die Anzeige stimmt also. Am Pool fehlen die abgelehnten Shares: 2,6 % seit dem Start, 1,6 % in den 2 Stunden. Den Grund liefert der Status nicht, ein serielles Log lief nicht mit. Im 6-Stunden-Lauf von 7.5.0 waren es alle `Job not found` bei ähnlicher Quote (siehe Bekannte Punkte). Die niedrigen 15-Minuten-Werte von t10 und t38 waren Streuung. Die laufende Prüfung fand in den 2 Stunden 124 falsche Runden von 339 918 (0,036 %).

Freier Heap bei t38: rund 111 KB nach dem Start, 104 KB nach 7 Stunden, 103 KB nach 9 Stunden. Kurze Einbrüche bis 39 KB kommen von den HTTPS-Abrufen. 7.5.0 verhielt sich im 6-Stunden-Lauf ähnlich (106 auf 105 KB, kleinster Wert 35 KB). Ob der Grundwert über Tage weiter sinkt, ist nicht geprüft.

Die Untergrenze mit drei Blöcken liegt bei rund 3 × 68 + 2 × 19 = 242 Takten nur für die Hardware. t38 braucht 252 in der Schleife und rund 255 von Runde zu Runde.

Die fremde Firmware NMMiner 2.0.04 (Quellcode nicht offen) lief am 03.11.2026 eine Stunde auf demselben Board am selben Pool. Der Pool nahm 172 Shares bei Diff 0,005 an, das sind 1028 ± 78 kH/s, angezeigt waren 1041 kH/s. Mit unserer Messung der Hardware passt das nur zusammen, wenn NMMiner einen guten Teil davon in Software auf Kern 0 rechnet oder am oberen Rand der Streuung lag. Geklärt ist das nicht.

Kern 0 rechnet in Software. Ein Hash braucht dort auf einem freien Kern 7336 Takte (32,7 kH/s, gemessen am 03.11.2026). Kern 0 bekommt davon rund 83 %, der Rest geht an WLAN, Netz und Abrufe. Ab t19 läuft der Software-Miner aus dem Flash (22 kH/s), siehe oben. Der erzeugte Code liegt nahe an dem, was ohne Rotationsbefehl möglich ist (rund 6000 Befehle je Hash). Vorberechnen fester Teile je Job brächte nach Schätzung nur rund 5 %, also etwa 1,5 kH/s.

Jeder Treffer des Vorfilters wird zusätzlich vollständig in Software nachgerechnet. Bei einem Chip unter Revision 3 läuft nur die Software.

## Abrufe

Alle HTTP-Abrufe laufen in eigenen Tasks auf Kern 0 mit HTTP/1.0, die Antwort kommt also ohne Chunked-Encoding. Das Lesen gibt die CPU ab, sobald keine Daten da sind (`vTaskDelay`). Bis 7.4.5 nutzten Wetter und Standort `readStringUntil()`, das bis zum Timeout ohne Pause wartet. Bei ungewohnten Antworten lief dann auf Kern 0 der Task-Watchdog ab (`task_wdt … IDLE0 … CPU 0: weather`), der Miner startete etwa alle 20 bis 30 Sekunden neu.

| Abruf | Takt | Größe |
|---|---|---|
| Kurse, CoinGecko `simple/price` | 5 Minuten | rund 0,5 KB |
| 7-Tage-Verlauf, CoinGecko `coins/markets?sparkline=true` | 30 Minuten, nach Fehler 10 Minuten | rund 24 bis 30 KB, wird nicht gepuffert |
| Wetter, Open-Meteo | einstellbar, ab 5 Minuten | bis 8 KB |
| Ortssuche, Open-Meteo Geocoding | einmal nach Änderung des Ortsnamens | unter 1 KB |

Der Verlauf läuft Zeichen für Zeichen durch `SparkParser` (`net_parse.cpp`). Er merkt sich die Coin-ID und liest die Zahlen hinter `"price":[`. Aus 168 Stundenwerten werden 48 gemittelte Punkte je Coin. Steht eine ID doppelt in der Liste, etwa `bitcoin` fest und in der Watchlist, bekommen beide Plätze denselben Verlauf.

## Bekannte Punkte

- **Unbestätigte Hardware-Treffer:** Die Hardware meldet gelegentlich einen Treffer, den die Software nicht bestätigt. Bis 7.5.0 waren es 0,3 bis 3 pro Minute, ab 7.6.0-t31 rund 4000 in der Startphase und danach 1 bis 7 pro Sekunde. Der gelesene Wert ist dabei `0x00000000` oder `0x02000000`, ein verfälschter Lesezugriff (siehe Hashrate). Falsche Shares gehen dadurch nicht raus.
- **`Job not found`:** Ein kleiner Teil der Shares wird abgelehnt, wenn sie sich mit einem neuen Job kreuzen, im 6-Stunden-Lauf 2,7 %. Das liegt an der Entfernung zum Pool: `dgb-stratum.solominer.net` antwortet vom Nutzer-LAN mit rund 170 ms Ping, `eu.digi.hmpool.io` mit rund 28 ms (gemessen am 03.11.2026 vom PC). Bei rund 15 Sekunden je DigiByte-Block und rund 0,4 Sekunden bis Notify und Share beim Pool sind ungefähr 2,7 % zu erwarten. Ein näherer Pool senkt das, geprüft ist es nicht. Seit 7.6.0-t7 sendet der Socket ohne Nagle-Verzögerung (`setNoDelay`).
- **Ungeklärter Neustart:** Bei einem Lasttest ohne serielles Log startete das Board einmal neu. Die Ursache ist unbekannt. Weitere 410 Abrufe mit gleicher Last liefen ohne Neustart. Der kleinste freie Heap lag dabei bei rund 25 KB, im Normalbetrieb bei 35 bis 39 KB.
- **Temperatur:** Der Chip-Sensor ist nicht abgeglichen und liefert oft den Fehlwert 53,33 °C, der verworfen wird. Der angezeigte Wert ist nur grob.
- **Sommerzeit:** Die Zeitzone ist ein fester Wert.
- **Speicher:** Im 6-Stunden-Lauf (7.5.0) lag der freie Heap zu Beginn bei 106 KB und am Ende bei 105 KB. Dazwischen schwankte er zwischen 98 und 106 KB. Der kleinste freie Heap lag bei 35 KB, der größte freie Block am Ende bei 29,7 KB. Das serielle Log deckt nur die ersten 95 Minuten ab, danach stammen die Werte aus dem Web-Status.
- **WLAN-Portal:** Nach dem Speichern im Portal startet der Miner neu. Vorher blieb das Dashboard unerreichbar, vermutlich weil der Portal-Webserver von WiFiManager noch Port 80 belegte. Der Neustart nach dem Portal ist am Gerät noch nicht geprüft.
- **Web-Sicherheit:** Das Dashboard läuft über unverschlüsseltes HTTP und ist ohne Passwort für jeden im Netz erreichbar. `setInsecure()`: Die HTTPS-Abrufe prüfen keine Zertifikate.
