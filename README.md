# BLE-Gateway (Govee | Qingping | Oclean)

Ein ESP32-C3 (z. B. der 5-Euro-"SuperMini") lauscht passiv den BLE-
Advertisements im Haus zu und publisht alles per MQTT in dein
Smarthome — **ohne Cloud, ohne App, ohne Kopplung**. Einmal
konfigurieren, an den USB-Stecker, fertig.

**Version: siehe [PlattformIO-Flag](platformio.ini) (`PROJECT_VERSION`,
derzeit 1.1.0)** — erscheint beim Start im Serial-Monitor und als
retained Topic `govee/version` im MQTT-Broker. Aenderungshistorie:
[CHANGELOG.md](CHANGELOG.md).

## Was wird ausgelesen?

| Geraet | Daten | MQTT-Pfad |
|---|---|---|
| Govee H5075 / H5077 (Thermo-/Hygrometer) | Temperatur, Luftfeuchte, Batterie, RSSI | `govee/H5075_XXXX/...` |
| Qingping Air Monitor Lite | Temperatur, Feuchte, CO₂, PM2.5, PM10, RSSI | `qingping/Qingping_XXXX/...` |
| Oclean-Zahnbuersten | Frames/min + RSSI je Buerste (Aktivitaets-/Anwesenheitssignal) | `oclean/<Modell_XXXX>/...` |

`XXXX` = letzte 4 Hex-Stellen der MAC-Adresse (Govee benennt seine
Geraete in der App genauso). Geraete werden automatisch erkannt und
ihre Liste im NVS-Flash persistiert — sie ueberlebt Neustart und
Reflash. Neue Geraete dieser Familien erscheinen ohne Aenderung am
Code; unbekannte BLE-Geraete siehst du mit den Debug-Logschaltern
(siehe unten).

Bekannte Grenze (10/2026 gemessen): Die Oclean-Buersten senden im
Standby und beim Putzen identische Frames — ein echtes "wird gerade
geputzt"-Signal existiert per passivem Mithoeren nicht, daher gibt es
bewusst kein "aktiv"-Topic.

## Schnellstart (ca. 10 Minuten)

**Voraussetzungen:** VSCode mit [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode),
ein ESP32-C3-Board (SuperMini, DevKitM o. ae.), USB-Datenkabel,
2,4-GHz-WLAN (der ESP32-C3 kann **kein** 5 GHz!).

1. Dieses Repository klonen oder herunterladen und den Ordner in
   VSCode oeffnen (PlatformIO erkennt `platformio.ini` automatisch und
   laedt beim ersten Build die Toolchain — das dauert einmalig ein paar
   Minuten).
2. **`include/config.h.example` nach `include/config.h` kopieren** und
   ausfuellen: WLAN-Name, WLAN-Passwort, OTA-Passwort.
3. **`include/config_mqtt.h.example` nach `include/config_mqtt.h`
   kopieren** und ausfuellen: Adresse/Port/Login deines MQTT-Brokers
   (ioBroker mqtt-Adapter, Mosquitto, Home Assistant ...).
4. ESP per USB anstecken, in der PlatformIO-Leiste auf **Upload** (→)
   klicken, danach den **Serial Monitor** oeffnen (115200 Baud).
   Dort erscheinen Versionsnummer, WLAN- und MQTT-Status und alle
   automatisch erkannten Geraete.
5. Im MQTT-Broker die Topics ansehen: `govee/#`, `qingping/#`,
   `oclean/#`.

Update-Hinweis: Bei neuen Versionen dieses Repos einfach den geaenderten
Code ersetzen — deine beiden `config`-Dateien bleiben unberuehrt.

## OTA: Updates ohne USB-Kabel

Nach dem ersten USB-Flash laesst sich jede neue Firmware uebers Netz
spielen:

    pio run -t upload --upload-port ble-gateway.local

(statt `.local` geht auch die IP-Adresse; Passwort = `OTA_PASSWORD`).
**IP statt Hostname empfohlen:** PlatformIO waehlt nur bei einer IP
automatisch das OTA-Protokoll (`espota`) — bei reinen Hostnamen
(`.at.home` & Co.) versucht es sonst, den Namen als seriellen Port zu
oeffnen und bricht ab.

Das OTA-Passwort kommt nicht interaktiv, sondern aus der **lokalen,
nicht oeffentlichen** Datei `platformio_local.ini` (Vorlage:
`platformio_local.ini.example` — kopieren, Passwort eintragen). Sie
muss mit `OTA_PASSWORD` in `include/config.h` uebereinstimmen — und
zwar mit dem Passwort der **laufenden** Firmware: Eine Aenderung wird
erst mit dem naechsten erfolgreichen Upload aktiv. Die Firmware stoppt
beim Upload selbststaendig den BLE-Scan und startet danach neu. Einmal
pro Firmware-Version empfiehlt sich trotzdem der Blick in den
Serial-Monitor.

## Was im MQTT ankommt

| Topic | Inhalt |
|---|---|
| `govee/status` | `online`/`offline` (Last Will des Gateways) |
| `govee/version` | Firmware-Version (retained) |
| `govee/H5075_XXXX/temperature` | Grad C |
| `govee/H5075_XXXX/humidity` | rel. Feuchte % |
| `govee/H5075_XXXX/battery` | Batterie % |
| `govee/H5075_XXXX/rssi` | Empfangsstärke dBm |
| `qingping/Qingping_XXXX/temperature|humidity|co2|pm25|pm10|rssi` | Luftdaten |
| `oclean/adv_pro_min` | Summe aller Buersten-Frames (60-s-Fenster) |
| `oclean/<Modell_XXXX>/adv_pro_min` | Frames je Buerste (nur bei Aenderung) |
| `oclean/<Modell_XXXX>/rssi` | Empfangsstärke je Buerste |

Alle Werte werden mit Retain gepublished und nur bei Aenderung
gesendet (Batterie/RSSI laufen passiv mit). Die Versionsnummer findet
man unter `govee/version`.

## Einstellungen

- `include/config.h` — WLAN, Hostname, LED, OTA-Passwort
- `include/config_mqtt.h` — Broker, Topic-Pfade, Sende-Intervalle.
  **Jeder Monitoring-Bereich laesst sich einzeln abschalten:** den
  zugehoerigen Topic-Pfad (`MQTT_BASETOPIC`, `MQTT_TOPIC_QINGPING`,
  `MQTT_TOPIC_OCLEAN`) auf `""` setzen — dann wird der Bereich
  komplett uebersprungen (auch keine NVS-Eintraege, kein Funkmuelleim
  im Log). Standard: alle drei aktiv.
- `src/main.cpp` (oberster Block) — Debug-Logzeilen einzeln
  zuschaltbar: `[ADV]` (alle BLE-Geraete im Funkraum), `[BLE-Stat]`
  (Empfangszahlen), `[QP-Roh]`/`[BR-Roh]` (Rohdaten von Qingping/
  Buersten), `MQTT ->` (jeder Publish). Standard: alles aus.
- `platformio.ini` — Board, Version (`PROJECT_VERSION`), Partitionen

## Fehlerbehebung

- **WLAN verbindet nicht:** Der ESP32-C3 kann nur 2,4 GHz — ein
  5-GHz-SSID-Name alleine reicht nicht, das Netz muss wirklich 2,4 GHz
  senden. Bei Fehlschlag listet die Firmware automatisch alle
  sichtbaren WLANs mit Kanal und Verschluesselung im Serial-Monitor.
- **Kurz nach dem Boot leere WLAN-Liste / kein Empfang:** Auf
  ESP32-C3-SuperMini-Klonen ist die Standard-Sendeleistung zu hoch —
  dieser Code setzt bereits `WiFi.setTxPower(WIFI_POWER_8_5dBm)`.
  Solltest du eigene Erweiterungen bauen: nicht entfernen.
- **Govee-Werte kommen nicht:** Govee-App schliessen (eine aktive
  App-Verbindung kann das Broadcasting unterbrechen) und den Sensor
  naeher ans Gateway setzen; die Empfangsstärke (`.../rssi`) im Blick
  behalten.
- **Qingping-PM-Werte fehlen anfangs:** Das Geraet verteilt die Werte
  auf mehrere Frame-Varianten; Temperatur/Feuchte/CO₂ kommen
  sofort, PM2.5/PM10 mit dem naechsten passenden Frame.
- **LED:** Blaue LED (falls dein Board eine hat): GPIO 8, active-LOW —
  `LED_FEEDBACK 1` = kurzer Blitz bei jedem Publish, `LED_TEST 1` =
  Dauertest. Die rote Power-LED ist fest verdrahtet und nicht
  abschaltbar.

## Hardware-Basis

Getestet mit einem ESP32-C3 SuperMini (Klon). Der Code arbeitet mit
jedem ESP32-C3-Board mit Bluetooth; `platformio.ini` passt ggf. das
`board`- Feld an. Wichtige Erfahrungswerte sind im Code dokumentiert:
TX-Leistungsfalle der SuperMinis, LED-Varianten, Partitionstabelle fuer
OTA, und die BLE-Formate von Govee (FW 1.04.x: Manufacturer Data `88 EC`,
aelter: Service Data UUID 0xEC88) und Qingping (Service Data UUID 0xFDCD,
TLV-Struktur, PM unter Typ 0x12).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
