# Fremde Bestandteile

Der eigene Code steht unter GPL-3.0, siehe [LICENSE](LICENSE). Für die folgenden Teile gelten deren eigene Lizenzen.

## Bibliotheken

Nicht in diesem Repository enthalten, sie werden beim Bauen eingebunden. Fertige Firmware-Dateien enthalten sie aber.

| Bibliothek | Version | Lizenz laut Bibliothek |
|---|---|---|
| ESP32 Arduino Core | 3.3.12 | LGPL-2.1, ESP-IDF unter Apache-2.0, WLAN-Treiber von Espressif als vorkompilierte Bibliotheken ohne Quellcode |
| TFT_eSPI | 2.5.43 | MIT und BSD, siehe deren `license.txt` |
| XPT2046_Touchscreen | 1.4.0 | MIT |
| ArduinoJson | 7.4.3 | MIT |
| WiFiManager | 2.0.17 | MIT |
| NTPClient | 3.2.1 | MIT |
| Async TCP (ESP32Async) | 3.5.0 | LGPL-3.0 |
| ESP Async WebServer (ESP32Async) | 3.12.1 | LGPL-3.0 |

## Schriften

Die Bitmap-Schriften in `firmware/tomessino_dgb/ui_assets_fonts.cpp` sind aus DejaVu Sans Condensed erzeugt. Lizenz: [licenses/DejaVu-Fonts-LICENSE.txt](licenses/DejaVu-Fonts-LICENSE.txt).

## Marken und Grafiken

Roboter, Symbole und Schriftzug sind für dieses Projekt per Code gezeichnet, siehe `tools/design/emblem.py`, und stehen unter der Lizenz des Projekts.

DigiByte und das DigiByte-Logo (`tools/design/assets/`, eingebaut in Display und Dashboard) gehören ihren Rechteinhabern und sind nicht von der GPL-3.0 dieses Projekts umfasst. Das Projekt steht in keiner Verbindung zum DigiByte-Projekt.

## Dienste

Die Firmware ruft zur Laufzeit CoinGecko (Kurse, 7-Tage-Verlauf), Open-Meteo (Wetter, Luftqualität, Ortssuche) und ip-api.com (Standort aus der IP, abschaltbar) auf. Für deren Nutzung gelten deren Bedingungen.
