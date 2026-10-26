# BLE-Gateway (Govee | Qingping | Oclean)

Ein ESP32-C3 (z. B. der 5-Euro-"SuperMini") hoert die BLE-Advertisements
im Haus ab und gibt die Daten per MQTT an dein Smarthome weiter.
Version: siehe [PlattformIO-Flag](platformio.ini) (`PROJECT_VERSION`,
derzeit 1.1.0) — erscheint beim Start im Serial-Monitor und als
retained Topic `govee/version`. Aenderungshistorie:
[CHANGELOG.md](CHANGELOG.md).

## Worum geht es?

Viele Guenstige Geraete funken ihren Zustand schon unablaessig ins
Funknetz — als BLE-Advertisement, unabhaengig von der App des
Herstellers. Dieses Projekt lauscht dieser Funkerei passiv zu: Das
Gateway **koppelt sich nie mit einem Geraet**, braucht keine App, keine
Cloud und keinen Account. Es dekodiert die Advertisements und legt die
Werte als MQTT-Topics in dein Smarthome (ioBroker, Home Assistant,
FHEM, openHAB ...). Ein 5-Euro-Board, ein USB-Stecker, fertig.

## Entstehung — und warum alles "govee" heisst

Das Projekt startete als reines Govee-Gateway (daher Repo-Name und der
MQTT-Basispfad `govee`, der aus Kompatibilitaet zum bestehenden Setup
erhalten blieb). Dann kamen aus Neugier die Oclean-Zahnbuersten dazu —
ihre Auswertung entpuppte sich als praktisch nutzlos, das Ergebnis ist
trotzdem sauber dokumentiert. Zuletzt zog der Qingping Air Monitor
ein und rundete das Bild ab: drei Hersteller, ein Board.

## Wie dieser Code entstanden ist

Der Code ist zu 100 % KI-generiert (ZCode mit dem GLM-Modell). Der
Mensch dahinter hat gelenkt: Anforderungen formuliert, das Board an
den USB-Stecker gehaengt, Messreihen aufgenommen, Fehlannahmen
widerlegt (etwa die Hoffnung auf ein Putz-Erkennungssignal) und
entschieden, was bleiben darf. Was hier funktioniert, funktioniert
nur, weil jede Behauptung am Geraet verifiziert wurde — README und
CHANGELOG dokumentieren den Weg inklusive Irrwege.

## Was brauche ich?

- **Board:** ESP32-C3 (z. B. SuperMini, DevKitM-1) — ~5 Euro
- **USB-Kabel mit Datenleitung** (nicht nur Ladekabel) und 5-V-Netzteil
- **Software:** VSCode mit [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode)
- **Netz:** 2,4-GHz-WLAN (der ESP32-C3 kann **kein** 5 GHz) und einen
  MQTT-Broker (ioBroker mqtt-Adapter, Mosquitto, Home Assistant ...)
- **Geraete (optional, je nach Interesse):** Govee H5075/H5077,
  Qingping Air Monitor Lite, Oclean-Zahnbuersten
- **Nicht noetig:** Cloud-Zugang, Hersteller-App, Hub, Guthaben

## Was kann es?

- **Passives Mitlesen:** keine Kopplung mit den Geraeten; sie senden
  ohnehin, das Gateway hoert nur zu. Die Hersteller-App bleibt
  parallel nutzbar.
- **Automatische Geraeteerkennung:** neue Geraete dieser Familien
  erscheinen ohne Code-Aenderung; die Liste wird im Flash persistiert
  (ueberlebt Neustart und Reflash).
- **Effizientes MQTT:** Werte nur bei Aenderung, mit Retain, Last Will
  (`govee/status`) und Versions-Topic (`govee/version`).
- **Modular:** jeder Geraete-Bereich (Govee / Qingping / Oclean) laesst
  sich per Config einzeln abschalten.
- **OTA:** Firmware-Updates uebers WLAN, ohne USB-Kabel.
- **Ehrliche Grenzen** (gemessen, nicht geraten): die Oclean-Buersten
  unterscheiden Standby und Nutzung weder in der Payload noch in der
  Frame-Rate — es gibt ein Aktivitaets-/Anwesenheitssignal, aber kein
  "wird geputzt". Govee/Qingping liefern dagegen vollwertige
  Messwerte.

| Geraet | Daten |
|---|---|
| Govee H5075 / H5077 | Temperatur, Luftfeuchte, Batterie, RSSI |
| Qingping Air Monitor Lite | Temperatur, Feuchte, CO₂, PM2.5, PM10, RSSI |
| Oclean-Zahnbuersten | Frames/min + RSSI je Buerste |

## Schnellstart (ca. 10 Minuten)

1. Dieses Repository klonen oder herunterladen und den Ordner in
   VSCode oeffnen (PlatformIO laedt beim ersten Build die Toolchain —
   einmalig ein paar Minuten).
2. **`include/config.h.example` nach `include/config.h` kopieren** und
   ausfuellen: WLAN-Name, WLAN-Passwort, OTA-Passwort.
3. **`include/config_mqtt.h.example` nach `include/config_mqtt.h`
   kopieren** und ausfuellen: Adresse/Port/Login deines MQTT-Brokers
   sowie die Topic-Pfae (Bereiche mit leerem Namen = aus).
4. ESP per USB anstecken, in der PlatformIO-Leiste **Upload** (→)
   klicken, danach **Serial Monitor** (115200 Baud): dort erscheinen
   Versionsnummer, WLAN-/MQTT-Status und alle erkannten Geraete.
5. Im MQTT-Broker die Topics abonnieren: `govee/#`, `qingping/#`,
   `oclean/#` (bzw. nur die aktivierten Bereiche).

Update-Hinweis: Bei neuen Versionen dieses Repos den Code ersetzen —
deine beiden `config`-Dateien bleiben unberuehrt.

## Was im MQTT ankommt?

| Topic | Inhalt |
|---|---|
| `govee/status` | `online`/`offline` (Last Will des Gateways) |
| `govee/version` | Firmware-Version (retained) |
| `govee/H5075_XXXX/temperature` | Grad C |
| `govee/H5075_XXXX/humidity` | rel. Feuchte % |
| `govee/H5075_XXXX/battery` | Batterie % |
| `govee/H5075_XXXX/rssi` | Empfangsstaerke dBm |
| `qingping/Qingping_XXXX/temperature|humidity|co2|pm25|pm10|rssi` | Luftdaten |
| `oclean/adv_pro_min` | Summe aller Buersten-Frames (60-s-Fenster) |
| `oclean/<Modell_XXXX>/adv_pro_min` | Frames je Buerste (nur bei Aenderung) |
| `oclean/<Modell_XXXX>/rssi` | Empfangsstaerke je Buerste |

`XXXX` = letzte 4 Hex-Stellen der MAC-Adresse (Govee benennt seine
Geraete in der App genauso). Alle Werte werden mit Retain gepublished
und nur bei Aenderung gesendet (Batterie/RSSI laufen passiv mit).

## Einstellungen

- `include/config.h` — WLAN, Hostname, LED, OTA-Passwort
- `include/config_mqtt.h` — Broker, Topic-Pfae, Sende-Intervalle.
  **Jeder Monitoring-Bereich laesst sich einzeln abschalten:** den
  zugehoerigen Topic-Pfad (`MQTT_BASETOPIC`, `MQTT_TOPIC_QINGPING`,
  `MQTT_TOPIC_OCLEAN`) auf `""` setzen — dann wird der Bereich
  komplett uebersprungen (auch keine NVS-Eintraege, kein Logspam).
  Standard: alle drei aktiv.
- `src/main.cpp` (oberster Block) — Debug-Logzeilen einzeln
  zuschaltbar: `[ADV]` (alle BLE-Geraete im Funkraum), `[BLE-Stat]`
  (Empfangszahlen), `[QP-Roh]`/`[BR-Roh]` (Rohdaten von Qingping/
  Buersten), `MQTT ->` (jeder Publish). Standard: alles aus.
- `platformio.ini` — Board, Version (`PROJECT_VERSION`), Partitionen

## OTA: Updates ohne USB-Kabel

Nach dem ersten USB-Flash laesst sich jede neue Firmware uebers Netz
spielen — am einfachsten mit dem beiliegenden Skript (loest den
Geraetenamen auf und ruft PlatformIO mit der IP auf):

    ./ota.sh               # Geraet automatisch suchen
    ./ota.sh 192.168.1.50  # oder explizit per IP

Direkt per PlatformIO:

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
Serial-Monitor. Bei "Error Uploading" mitten im Transfer: nochmal
versuchen (z. B. wenn parallel ein zweiter OTA-Client lief) — der ESP
faehrt bei Abbruch automatisch mit der alten Firmware weiter.

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
`board`-Feld an. Wichtige Erfahrungswerte sind im Code dokumentiert:
TX-Leistungsfalle der SuperMinis, LED-Varianten, Partitionstabelle fuer
OTA, und die BLE-Formate von Govee (FW 1.04.x: Manufacturer Data `88 EC`,
aelter: Service Data UUID 0xEC88) und Qingping (Service Data UUID 0xFDCD,
TLV-Struktur, PM unter Typ 0x12). Details und Messwerte:
[CHANGELOG.md](CHANGELOG.md).

## Lizenz

MIT — siehe [LICENSE](LICENSE).
