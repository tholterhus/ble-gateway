# Changelog

Alle relevanten Änderungen dieses Projekts werden hier dokumentiert.
Format angelehnt an [Keep a Changelog](https://keepachangelog.com/),
Versionsnummern nach [SemVer](https://semver.org/).

## [1.3.0] - 2026-10-27

### Added

- Eigenüberwachung per MQTT: `govee/zeit` (lokale Uhrzeit, NTP über
  FritzBox/Pool, Zone Europa/Berlin), `govee/uptime_s` (Laufzeit in
  Sekunden) und `govee/reboots` (Neustart-Zähler, im NVS persistiert —
  überlebt Neustart und Reflash). Alle 60 s aktualisiert.

### Fixed

- Bürsten-Erkennung korrigiert: Die Xiaomi-0xFEAF-Geräte (N0xx-Namen)
  sind KEINE Zahnbürsten (Nutzer-Beobachtung: nur 3 sichtbare Geräte,
  alle mit Oclean-Namen) und werden nicht mehr gezählt. Erkannt werden
  nur Geräte mit "Oclean" im Namen oder dem MAC-Präfix 70:28:45:.
  Altlasten im NVS werden beim Boot aussortiert; die Bürsten-Map ist
  auf 12 Einträge begrenzt (rotierende MAC-Adressen ließen sie
  ungebremst wachsen — Ursache des Serial-Log-Spams und plausibler
  Mitverursacher der Reboot-Schleife).

## [1.2.0] - 2026-10-27

### Added

- Wert-Glättung nach dem Prinzip des Heizungs-Monitors (2xDalles):
  ein Wert wird nur neu gepublished, wenn er sich seit der letzten
  Veröffentlichung um mindestens X % des alten Werts UND einen
  absoluten Mindestbetrag geändert hat (SMOOTH_* in
  include/config_mqtt.h). Unterdrückt das Flackern von PM2.5/PM10
  (1↔2 µg/m³) und der Temperatur (0,1 °C) und liefert ruhige
  Graphen; Schwellwerte je Werttyp konfigurierbar.

## [1.1.1] - 2026-10-27

### Added

- Eigen-Beobachtung per MQTT: `govee/reset` (Grund des letzten
  Neustarts, retained — z. B. "Software reset", "Brownout",
  "Task watchdog") und `govee/heap` (freier Heap, alle 60 s). Damit
  lässt sich eine Instabilität des Gateways direkt aus dem Broker
  lesen, ohne Serial-Kabel. Hintergrund: Der Gateway startete im Test
  sporadisch von selbst (identische Erst-Lesungs-Bursts im
  MQTT-Log) — die Ursache wird mit dem Reset-Grund sichtbar.

## [1.1.0] - 2026-10-26

### Added

- Monitoring-Bereiche einzeln deaktivierbar: in `include/config_mqtt.h`
  den zugehörigen Topic-Pfad auf `""` setzen — Govee, Qingping oder
  Oclean wird dann komplett übersprungen (kein Dekodieren, Zählen,
  NVS-Einträge oder Log). Bei leerem `MQTT_BASETOPIC` entfallen auch
  die Topics `govee/status` und `govee/version` (MQTT-Connect ohne
  Last Will).
- OTA-Passwort-Workaround: der espressif32-Builder hängt die eigenen
  `upload_flags` beim espota-Protokoll nicht an (auth bleibt leer,
  "Authentication Failed") — das Post-Script `ota_auth.py` liest das
  Passwort aus der lokalen `platformio_local.ini` (durch `.gitignore`
  vom Repo ausgeschlossen; Vorlage: `platformio_local.ini.example`)
  und hängt es an.
- README: OTA-Upload mit IP statt Hostname (PlatformIO wählt bei
  Hostnamen sonst fälschlich `esptool` und bricht ab).

## [1.0.0] - 2026-10-25

### Added

- Erste öffentliche Version: ESP32-C3 hört BLE-Advertisements passiv
  mit (keine Kopplung, keine App, keine Cloud) und publisht per MQTT.
- Govee H5075/H5077: Temperatur, Luftfeuchte, Batterie, RSSI
  (FW 1.04.x: Manufacturer Data `88 EC` + 6 Bytes mit
  Vorzeichen-Bit; ältere Firmwares: Service Data an UUID 0xEC88).
- Qingping Air Monitor Lite: Temperatur, Feuchte, CO2, PM2.5, PM10,
  RSSI (Service Data an UUID 0xFDCD, TLV-Struktur).
- Oclean-Zahnbürsten: Frames/min + RSSI je Bürste (Modellname +
  MAC-Suffix als Ordner), Erkennung über Name, Xiaomi-0xFEAF-Frames
  und Oclean-MAC-Präfix `70:28:45:`.
- Automatische Geräteerkennung; Geräteliste im NVS-Flash persistiert
  (überlebt Neustart und Reflash).
- MQTT: Retain, change-only-Publish (Messwerte als Trigger, Batterie/
  RSSI laufen passiv mit), Last Will `govee/status`.
- Versionsnummer beim Start im Terminal und als `govee/version`.
- OTA-Updates (min_spiffs-Partitionen, Passwort), BLE-Scan wird beim
  Upload automatisch angehalten.
- Debug-Logschalter ([ADV], [BLE-Stat], [QP-Roh], [BR-Roh],
  "MQTT ->") einzeln in `src/main.cpp` zuschaltbar.
- README mit Laien-Schnellstart, Topic-Tabelle und Fehlerbehebung.

### Erkenntnisse (Hardware/Protokoll, gemessen 10/2026)

- ESP32-C3 SuperMini: mit Standard-Sendeleistung (20 dBm) ist das
  Radio praktisch taub (leere Scan-Liste trotz 2 m Distanz) —
  `WiFi.setTxPower(WIFI_POWER_8_5dBm)` behebt es.
- ESP32-C3 kann nur 2,4-GHz-WLAN; Hostnamen wie `xyz.at.home` sind
  dafür unrelevant, aber als OTA-Upload-Ziel ungeeignet (IP nutzen).
- LED-Verdrahtung variiert je Klon-Exemplar: blaue LED an GPIO 8
  (active-LOW) und/oder rote Power-LED fest an 3,3 V (nicht
  steuerbar); manche Exemplare haben nur die rote.
- Govee: eine aktive App-Verbindung kann das Broadcasting unterbrechen.
- Qingping: alle Werte (Temperatur/Feuchte, PM2.5/PM10, CO2) liegen in
  jedem Frame gemeinsam; PM steckt unter TLV-Typ 0x12 (ältere Modelle:
  0x07), CO2 unter 0x13. (Die urspruengliche Annahme "Werte rotieren
  über mehrere Frame-Varianten" war falsch — die zeitweise fehlenden
  PM-Werte hatten einen Decoder-Bug als Ursache, behoben in v1.1.0.)
- Oclean-Bürsten senden im Standby ~11-22 Frames/min mit eingefrorener
  Payload; weder Payload noch Frame-Rate unterscheiden sich, wenn der
  Motor läuft — ein "wird benutzt"-Signal ist per passivem Mithören
  nicht vorhanden (daher kein "aktiv"-Topic).
