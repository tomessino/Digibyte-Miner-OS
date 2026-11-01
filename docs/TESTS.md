# Tests

## Ohne Gerät

```bash
python3 tools/test_hash_vektoren.py
```

```bash
python3 tools/mining_test/check_mining.py
```

```bash
python3 tools/device/test_geraetetest.py
```

```bash
PY=python3 bash tools/design/preview.sh
```

Alle enden mit `ALLE TESTS OK`, `preview.sh` zusätzlich mit `PARSER OK`.

- `check_mining.py` spielt den Pool: Es sendet Stratum-Nachrichten an denselben Code wie in der Firmware und rechnet jede Einreichung unabhängig mit `hashlib` nach. Dazu kommen Bitcoin-Block 1 als echter Testvektor und Fehlerantworten.
- `test_geraetetest.py` prüft das Gerätetest-Skript gegen einen simulierten Miner. Ein Teil davon braucht einen fertigen Build, dann zuerst bauen und `geraetetest.py manifest` ausführen.
- `preview.sh` rendert die Display-Seiten am PC und prüft die Auswertung von echten API-Antworten. Es braucht Pillow und numpy.

## Am Gerät

`tools/device/geraetetest.py` nutzt nur die Python-Standardbibliothek und speichert nach `geraetetest/`. Der Ordner ist in `.gitignore` und **darf nicht veröffentlicht werden**: Das Backup enthält das WLAN-Passwort, die Logs enthalten Wallet und WLAN-Namen.

```bash
python3 tools/device/geraetetest.py check --brauche-port
```

```bash
python3 tools/device/geraetetest.py backup
```

```bash
python3 tools/device/geraetetest.py flash firmware
```

```bash
python3 tools/device/geraetetest.py test lauf1 --version 7.4.5 --secs 180 --erwarte phase1,preise7,pm,mem --basis keine
```

- `flash` verweigert die Arbeit ohne Backup und mit veraltetem Build.
- `test` startet das Board neu, zeichnet Log und Web-Status auf und wertet aus. `--wechsel` schaltet ab der Hälfte alle 5 Sekunden die Display-Seite um und weckt das Display.
- `python3 tools/device/messreihe.py NAME --url http://IP` misst die Rechenwege einzeln.
- `TOMESSINO_WEB_PW` in der Umgebung liefert das Dashboard-Passwort, falls eines gesetzt ist.

## Störungen

| Problem | Maßnahme |
|---|---|
| Kein Port gefunden | Kabel prüfen, reine Ladekabel gibt es oft. `dmesg \| tail -20` zeigt den Adapter. |
| Port taucht auf und verschwindet sofort | Unter Linux greift `brltty` CH340-Adapter ab. `sudo apt remove brltty`. |
| `Permission denied` | Benutzer in die Gruppe `dialout`. |
| Esptool: „Failed to connect“ | BOOT-Taste beim Start des Befehls halten. |
| Kein WLAN nach dem Flashen | Portal `DGB-Miner-Setup` öffnen, `192.168.4.1`. |
