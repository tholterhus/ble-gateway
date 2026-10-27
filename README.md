# BLE-Gateway (Govee | Qingping | Oclean)

Ein ESP32-C3 (z. B. der 5-Euro-"SuperMini") hört die BLE-Advertisements
im Haus ab und gibt die Daten per MQTT an dein Smarthome weiter.
Version: siehe [PlattformIO-Flag](platformio.ini) (`PROJECT_VERSION`,
derzeit 1.1.0) — erscheint beim Start im Serial-Monitor und als
retained Topic `govee/version`. Änderungshistorie:
[CHANGELOG.md](CHANGELOG.md).

## Worum geht es?

Viele günstige Geräte funken ihren Zustand schon unablässig ins
Funknetz — als BLE-Advertisement, unabhängig von der App des
Herstellers. Dieses Projekt lauscht dieser Funkerei passiv zu: Das
Gateway **koppelt sich nie mit einem Gerät**, braucht keine App, keine
Cloud und keinen Account. Es dekodiert die Advertisements und legt die
Werte als MQTT-Topics in dein Smarthome (ioBroker, Home Assistant,
FHEM, openHAB ...). Ein 5-Euro-Board, ein USB-Stecker, fertig.

## Entstehung — und warum alles "govee" heißt

Das Projekt startete als reines Govee-Gateway (daher Repo-Name und der
MQTT-Basispfad `govee`, der aus Kompatibilität zum bestehenden Setup
erhalten blieb). Dann kamen aus Neugier die Oclean-Zahnbürsten dazu —
ihre Auswertung entpuppte sich als praktisch nutzlos, das Ergebnis ist
trotzdem sauber dokumentiert. Zuletzt zog der Qingping Air Monitor
ein und rundete das Bild ab: drei Hersteller, ein Board.

## Wie dieser Code entstanden ist

Der Code ist zu 100 % KI-generiert (ZCode mit dem GLM-Modell). Der
Mensch dahinter hat gelenkt: Anforderungen formuliert, das Board an
den USB-Stecker gehängt, Messreihen aufgenommen, Fehlannahmen
widerlegt (etwa die Hoffnung auf ein Putz-Erkennungssignal) und
entschieden, was bleiben darf. Was hier funktioniert, funktioniert
nur, weil jede Behauptung am Gerät verifiziert wurde — README und
CHANGELOG dokumentieren den Weg inklusive Irrwege.

## Was brauche ich?

- **Board:** ESP32-C3 (z. B. SuperMini, DevKitM-1) — ~5 Euro
- **USB-Kabel mit Datenleitung** (nicht nur Ladekabel) und 5-V-Netzteil
- **Software:** VSCode mit [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode)
- **Netz:** 2,4-GHz-WLAN (der ESP32-C3 kann **kein** 5 GHz) und einen
  MQTT-Broker (ioBroker mqtt-Adapter, Mosquitto, Home Assistant ...)
- **Geräte (optional, je nach Interesse):** Govee H5075/H5077,
  Qingping Air Monitor Lite, Oclean-Zahnbürsten
- **Nicht nötig:** Cloud-Zugang, Hersteller-App, Hub, Guthaben

## Was kann es?

- **Passives Mitlesen:** keine Kopplung mit den Geräten; sie senden
  ohnehin, das Gateway hört nur zu. Die Hersteller-App bleibt
  parallel nutzbar.
- **Automatische Geräteerkennung:** neue Geräte dieser Familien
  erscheinen ohne Code-Änderung; die Liste wird im Flash persistiert
  (überlebt Neustart und Reflash).
- **Effizientes MQTT:** Werte nur bei Änderung, mit Retain, Last Will
  (`govee/status`) und Versions-Topic (`govee/version`).
- **Modular:** jeder Geräte-Bereich (Govee / Qingping / Oclean) lässt
  sich per Config einzeln abschalten.
- **OTA:** Firmware-Updates übers WLAN, ohne USB-Kabel.
- **Ehrliche Grenzen** (gemessen, nicht geraten): die Oclean-Bürsten
  unterscheiden Standby und Nutzung weder in der Payload noch in der
  Frame-Rate — es gibt ein Aktivitäts-/Anwesenheitssignal, aber kein
  "wird geputzt". Govee/Qingping liefern dagegen vollwertige
  Messwerte.

| Gerät | Daten |
|---|---|
| Govee H5075 / H5077 | Temperatur, Luftfeuchte, Batterie, RSSI |
| Qingping Air Monitor Lite | Temperatur, Feuchte, CO₂, PM2.5, PM10, RSSI |
| Oclean-Zahnbürsten | Frames/min + RSSI je Bürste (Namen oder MAC-Präfix; Xiaomi-0xFEAF-Sender werden nicht gezählt) |

## Schnellstart (ca. 10 Minuten)

1. Dieses Repository klonen oder herunterladen und den Ordner in
   VSCode öffnen (PlatformIO lädt beim ersten Build die Toolchain —
   einmalig ein paar Minuten).
2. **`include/config.h.example` nach `include/config.h` kopieren** und
   ausfüllen: WLAN-Name, WLAN-Passwort, OTA-Passwort.
3. **`include/config_mqtt.h.example` nach `include/config_mqtt.h`
   kopieren** und ausfüllen: Adresse/Port/Login deines MQTT-Brokers
   sowie die Topic-Pfade (Bereiche mit leerem Namen = aus).
4. ESP per USB anstecken, in der PlatformIO-Leiste **Upload** (→)
   klicken, danach **Serial Monitor** (115200 Baud): dort erscheinen
   Versionsnummer, WLAN-/MQTT-Status und alle erkannten Geräte.
5. Im MQTT-Broker die Topics abonnieren: `govee/#`, `qingping/#`,
   `oclean/#` (bzw. nur die aktivierten Bereiche).

Update-Hinweis: Bei neuen Versionen dieses Repos den Code ersetzen —
deine beiden `config`-Dateien bleiben unberührt.

## Was im MQTT ankommt?

| Topic | Inhalt |
|---|---|
| `govee/status` | `online`/`offline` (Last Will des Gateways) |
| `govee/version` | Firmware-Version (retained) |
| `govee/zeit` | Lokale Uhrzeit (NTP, Europa/Berlin), alle 60 s |
| `govee/uptime_s` | Laufzeit des Gateways in Sekunden |
| `govee/reboots` | Neustart-Zähler (NVS-persistiert) |
| `govee/heap` / `govee/reset` | Diagnose: freier Heap, Grund des letzten Neustarts |
| `govee/H5075_XXXX/temperature` | Grad C |
| `govee/H5075_XXXX/humidity` | rel. Feuchte % |
| `govee/H5075_XXXX/battery` | Batterie % |
| `govee/H5075_XXXX/rssi` | Empfangsstärke dBm |
| `qingping/Qingping_XXXX/temperature` | Grad C |
| `qingping/Qingping_XXXX/humidity` | rel. Feuchte % |
| `qingping/Qingping_XXXX/co2` | CO₂ in ppm |
| `qingping/Qingping_XXXX/pm25` und `.../pm10` | Feinstaub in µg/m³ |
| `qingping/Qingping_XXXX/rssi` | Empfangsstärke dBm |
| `oclean/adv_pro_min` | Summe aller Bürsten-Frames (60-s-Fenster) |
| `oclean/<Modell_XXXX>/adv_pro_min` | Frames je Bürste (nur bei Änderung) |
| `oclean/<Modell_XXXX>/rssi` | Empfangsstärke je Bürste |

`XXXX` = letzte 4 Hex-Stellen der MAC-Adresse (Govee benennt seine
Geräte in der App genauso). Alle Werte werden mit Retain gepublished
und nur bei Änderung über der Glättungsschwelle gesendet (Batterie/RSSI
laufen passiv mit).

## Einstellungen

- `include/config.h` — WLAN, Hostname, LED, OTA-Passwort
- `include/config_mqtt.h` — Broker, Topic-Pfade, Sende-Intervalle.
  - **Glättung:** Werte werden nur neu gesendet, wenn sie sich um die
  in `SMOOTH_*` definierten Schwellen ändern (Prozent des alten Werts
  und absoluter Mindestbetrag) — kein 0,1-Grad-Flackern mehr in den
  Graphen. 0 % = nur Mindestbetrag.
  - **Bereiche abschalten:** den zugehörigen Topic-Pfad
  (`MQTT_BASETOPIC`, `MQTT_TOPIC_QINGPING`, `MQTT_TOPIC_OCLEAN`) auf
  `""` setzen — dann wird der Bereich komplett übersprungen (auch
  keine NVS-Einträge, kein Logspam). Standard: alle drei aktiv.
- `src/main.cpp` (oberster Block) — Debug-Logzeilen einzeln
  zuschaltbar: `[ADV]` (alle BLE-Geräte im Funkraum), `[BLE-Stat]`
  (Empfangszahlen), `[QP-Roh]`/`[BR-Roh]` (Rohdaten von Qingping/
  Bürsten), `MQTT ->` (jeder Publish). Standard: alles aus.
- `platformio.ini` — Board, Version (`PROJECT_VERSION`), Partitionen

## OTA: Updates ohne USB-Kabel

Nach dem ersten USB-Flash lässt sich jede neue Firmware übers Netz
spielen — am einfachsten mit dem beiliegenden Skript (löst den
Gerätenamen auf und ruft PlatformIO mit der IP auf):

    ./ota.sh               # Gerät automatisch suchen
    ./ota.sh 192.168.1.50  # oder explizit per IP

Direkt per PlatformIO:

    pio run -t upload --upload-port esp32-ble-gateway.local

(statt `.local` geht auch die IP-Adresse; Passwort = `OTA_PASSWORD`).
**IP statt Hostname empfohlen:** PlatformIO wählt nur bei einer IP
automatisch das OTA-Protokoll (`espota`) — bei reinen Hostnamen
(`.at.home` & Co.) versucht es sonst, den Namen als seriellen Port zu
öffnen und bricht ab.

Das OTA-Passwort kommt nicht interaktiv, sondern aus der **lokalen,
nicht öffentlichen** Datei `platformio_local.ini` (Vorlage:
`platformio_local.ini.example` — kopieren, Passwort eintragen). Sie
muss mit `OTA_PASSWORD` in `include/config.h` übereinstimmen — und
zwar mit dem Passwort der **laufenden** Firmware: Eine Änderung wird
erst mit dem nächsten erfolgreichen Upload aktiv. Die Firmware stoppt
beim Upload selbstständig den BLE-Scan und startet danach neu. Einmal
pro Firmware-Version empfiehlt sich trotzdem der Blick in den
Serial-Monitor. Bei "Error Uploading" mitten im Transfer: nochmal
versuchen (z. B. wenn parallel ein zweiter OTA-Client lief) — der ESP
fährt bei Abbruch automatisch mit der alten Firmware weiter.

## Fehlerbehebung

- **WLAN verbindet nicht:** Der ESP32-C3 kann nur 2,4 GHz — ein
  5-GHz-SSID-Name alleine reicht nicht, das Netz muss wirklich 2,4 GHz
  senden. Bei Fehlschlag listet die Firmware automatisch alle
  sichtbaren WLANs mit Kanal und Verschlüsselung im Serial-Monitor.
- **Kurz nach dem Boot leere WLAN-Liste / kein Empfang:** Auf
  ESP32-C3-SuperMini-Klonen ist die Standard-Sendeleistung zu hoch —
  dieser Code setzt bereits `WiFi.setTxPower(WIFI_POWER_8_5dBm)`.
  Solltest du eigene Erweiterungen bauen: nicht entfernen.
- **Govee-Werte kommen nicht:** Govee-App schließen (eine aktive
  App-Verbindung kann das Broadcasting unterbrechen) und den Sensor
  näher ans Gateway setzen; die Empfangsstärke (`.../rssi`) im Blick
  behalten.
- **Qingping-Werte unvollständig:** Alle Werte kommen in jedem Frame
  gemeinsam an (Temperatur/Feuchte, CO₂ und PM2.5/PM10 in einem).
  Fehlen einzelne Werte dauerhaft, liegt es am Empfang (RSSI prüfen) —
  bei der Entwicklung dieses Gateways war PM zeitweise NaN, weil der
  Decoder zunächst den falschen TLV-Typ las (PM steckt unter 0x12,
  nicht 0x07 wie bei älteren Qingping-Modellen); seit v1.1.0 behoben.
- **LED:** Blaue LED (falls dein Board eine hat): GPIO 8, active-LOW —
  `LED_FEEDBACK 1` = kurzer Blitz bei jedem Publish, `LED_TEST 1` =
  Dauertest. Die rote Power-LED ist fest verdrahtet und nicht
  abschaltbar.

## Hardware-Basis

Getestet ausführlich mit einem ESP32-C3 SuperMini (Klon). Der Code
nutzt nur Standardbausteine (NimBLE, WiFi, ArduinoOTA, Preferences) und
läuft damit auf **jedem ESP32 mit WLAN und Bluetooth an Bord** —
klassischer ESP32, S3, C3, C6 — wobei nur `board` in `platformio.ini`
angepasst werden muss. **Nicht geeignet:** der ESP32-H2 (Bluetooth,
aber kein WLAN — kein MQTT). Der im Code gesetzte
`WiFi.setTxPower(WIFI_POWER_8_5dBm)` ist ein C3-SuperMini-Fix; auf
klassischen ESP32-Boards ist er unnötig und kostet nur etwas
Reichweite. Wichtige Erfahrungswerte sind im Code dokumentiert:
TX-Leistungsfalle der SuperMinis, LED-Varianten, Partitionstabelle für
OTA, und die BLE-Formate von Govee (FW 1.04.x: Manufacturer Data `88 EC`,
älter: Service Data UUID 0xEC88) und Qingping (Service Data UUID 0xFDCD,
TLV-Struktur, PM unter Typ 0x12). Details und Messwerte:
[CHANGELOG.md](CHANGELOG.md).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
