# Installation

## Fertige Datei flashen

`Tomessino-DGB-Miner-v7.4.5-firmware.bin` enthält den ganzen Flash und löscht vorhandene Einstellungen.

```bash
esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write-flash 0x0 Tomessino-DGB-Miner-v7.4.5-firmware.bin
```

Antwortet der Chip nicht, beim Start des Befehls die BOOT-Taste halten.

## Selbst bauen

Getestet mit `arduino-cli` 1.5.1 unter Linux.

```bash
arduino-cli core install esp32:esp32@3.3.12
```

```bash
arduino-cli lib install TFT_eSPI@2.5.43 XPT2046_Touchscreen@1.4.0 ArduinoJson@7.4.3 WiFiManager@2.0.17 NTPClient@3.2.1 "Async TCP@3.5.0" "ESP Async WebServer@3.12.1"
```

`firmware/tft_espi/User_Setup.h` ersetzt die Datei `User_Setup.h` im TFT_eSPI-Bibliotheksordner, meist `~/Arduino/libraries/TFT_eSPI/`. Das ist nach jedem Update von TFT_eSPI nötig.

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:CPUFreq=240,FlashSize=4M,PartitionScheme=huge_app,UploadSpeed=921600" --output-dir firmware/build/tomessino_dgb firmware/tomessino_dgb
```

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn "esp32:esp32:esp32:CPUFreq=240,FlashSize=4M,PartitionScheme=huge_app,UploadSpeed=921600" --input-dir firmware/build/tomessino_dgb
```

`upload` lässt WLAN und Einstellungen stehen. Der erste Bau dauert wegen der Bilddaten einige Minuten.

**Partition:** `huge_app` (3 MB App, kein OTA) ist Pflicht, die Firmware belegt rund 2,6 MB.

## Serielles Log

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Nach dem Start sollten dort `SELFTEST … OK` und `HWTEST OK 256/256` stehen.

## Grafiken und Dashboard ändern

Erzeugte Dateien nicht von Hand ändern.

| Quelle | Erzeugt | Befehl |
|---|---|---|
| `tools/web/dashboard.html` | `dashboard_html.cpp` | `python3 tools/web/build_dashboard.py` |
| `tools/design/gen_assets.py`, `tools/design/emblem.py`, `tools/design/assets/` | `ui_assets*` | `python3 tools/design/gen_assets.py` |

`build_dashboard.py` braucht Pillow, `gen_assets.py` zusätzlich numpy und DejaVu-Schriften in `/usr/share/fonts/truetype/dejavu/`.
