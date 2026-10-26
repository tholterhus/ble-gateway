// govee-ble-gateway
// -----------------
// Empfängt die BLE-Advertisements eines Govee H5075/H5077
// passiv (keine Verbindung, keine App) und publisht Temperatur,
// Luftfeuchte, Batterie und RSSI per MQTT.
//
// Konfiguration: include/config.h (WLAN) + include/config_mqtt.h (MQTT)

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <set>

#include "config.h"
#include "config_mqtt.h"

// ================= Debug-Schalter (serielles Log) =================
// 0 = aus, 1 = an. Aktuell ist nur [BR-Roh] an, um der Zahnbürsten-
// Payload ein Nutzungs-Signal (Standby vs. Motor) zu entlocken.
#define DEBUG_ADV   0   // [ADV] Erstkontakt/Rohdaten je BLE-Gerät
#define DEBUG_STAT  0   // [BLE-Stat] Empfangs-Zähler alle 10 s
#define DEBUG_QP    0   // [QP-Roh] Qingping-Rohframe alle 30 s
#define DEBUG_VAL   0   // "MQTT -> ..."-Zeile bei jedem Publish
#define DEBUG_BR    0   // [BR-Roh] Bürsten-Rohframe alle 15 s (Payload-Analyse)
// ==================================================================

// Versionsnummer kommt aus platformio.ini (-DPROJECT_VERSION='"x.y.z"')
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "dev"
#endif

// Onboard-LED des C3 SuperMini (GPIO 8, active LOW)
#define LED_PIN 8

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
SemaphoreHandle_t stateMutex;
Preferences  prefs;   // NVS: persistente Geräte-Liste

// ------------------------------------------------------------
// Govee-Dekodierung (Format H5072/H5075, s. ble_monitor)
//
// Der Sensor hängt 6 Bytes als Service Data an die UUID 0xEC88:
//   [0]    0x00 Padding
//   [1..3] 24-Bit-Wert (big-endian): (Temperatur*100 + Feuchte*10)
//          mit Bit 23 als Minus-Vorzeichen. Beispiel 21.5 C / 45.0 %:
//          215 * 1000 + 450 = 215450
//   [4]    Batterie in Prozent
//   [5]    0x00 Padding
// ------------------------------------------------------------
struct GoveeReading {
    float temperature;
    float humidity;
    uint8_t battery;
};

bool decodeGovee(const uint8_t *d, GoveeReading &out) {
    if (d[0] != 0x00 || d[5] != 0x00) return false;

    uint32_t v = ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    bool negative = v & 0x800000;
    v &= 0x7FFFFF;

    out.temperature = (int)(v / 1000) / 10.0f;
    if (negative) out.temperature = -out.temperature;
    out.humidity = (v % 1000) / 10.0f;
    out.battery  = d[4];

    // Plausibilität: danebengegriffene Frames verwerfen
    if (out.battery > 100 || out.humidity > 100.0f ||
        out.temperature > 80.0f || out.temperature < -60.0f) {
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Zustand pro gefundenem Gerät
// ------------------------------------------------------------
enum DeviceKind { DEV_GOVEE = 0, DEV_QINGPING = 1 };

struct DeviceState {
    float    temperature = NAN;
    float    humidity    = NAN;
    float    battery     = NAN;
    float    co2         = NAN;
    float    pm25        = NAN;
    float    pm10        = NAN;
    float    light       = NAN;
    int      rssi        = 0;
    uint8_t  kind        = DEV_GOVEE;
    uint32_t lastPublish = 0;
    bool     pending     = false;
    bool     persisted   = false;  // bereits im NVS vermerkt
};

std::map<std::string, DeviceState> devices;  // Schlüssel: MAC-String

// ------------------------------------------------------------
// BLE-Diagnose: Empfangs-Zähler + Erstkontakt-Log je Gerät
// ------------------------------------------------------------
volatile uint32_t advCount = 0, sdCount = 0, mfgCount = 0;
std::set<std::string> loggedMacs;

String hexDump(const std::string &s, size_t maxBytes = 24) {
    String out;
    size_t n = s.size() < maxBytes ? s.size() : maxBytes;
    for (size_t i = 0; i < n; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", (uint8_t)s[i]);
        out += b;
        if ((i & 3) == 3) out += ' ';
    }
    if (s.size() > maxBytes) out += " ...";
    return out;
}

// ------------------------------------------------------------
// Zahnbürsten-Beobachtung (Oclean): Frames von Geräten mit
// "Oclean" im Namen, Xiaomi-0xFEAF-Frames (Oclean = Xiaomi-
// Ecosystem, bei uns die N0xx-Bürsten) und dem beobachteten
// Oclean-MAC-Präfix. Je Bürste ein eigener MQTT-Ordner.
// ------------------------------------------------------------
#define BRUSH_TS_SIZE 32   // Ringpuffer: max. Frames je Bürste im Fenster

struct BrushState {
    std::string name;                // beworbener Name (Modell)
    int      rssi = 0;
    uint32_t ts[BRUSH_TS_SIZE];      // alle Frames
    uint16_t write = 0;
};

std::map<std::string, BrushState> brushes;  // Schlüssel: MAC

bool isToothbrushFrame(NimBLEAdvertisedDevice *dev) {
    std::string name = dev->haveName() ? dev->getName() : "";
    if (name.find("Oclean") != std::string::npos) return true;
    std::string mac = dev->getAddress().toString();
    if (mac.rfind("70:28:45:", 0) == 0) return true;  // Oclean-Präfix (beobachtet)
    NimBLEUUID feaf((uint16_t)0xFEAF);
    for (uint8_t i = 0; i < dev->getServiceDataCount(); i++) {
        if (dev->getServiceDataUUID(i) == feaf) return true;
    }
    return false;
}

String brushIdFor(const std::string &mac, const std::string &name) {
    std::string tail = mac.substr(mac.size() - 5);
    tail.erase(std::remove(tail.begin(), tail.end(), ':'), tail.end());
    std::transform(tail.begin(), tail.end(), tail.begin(), ::toupper);
    std::string n = name;
    if (n.empty()) n = "Unbekannt";
    for (auto &c : n) {
        if (!isalnum((unsigned char)c)) c = '_';
    }
    return String(n.c_str()) + "_" + tail.c_str();
}

void brushFrame(NimBLEAdvertisedDevice *dev, const std::string &mac) {  // BLE-Task
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    BrushState &b = brushes[mac];
    if (b.name.empty() && dev->haveName()) b.name = dev->getName();
    b.rssi = dev->getRSSI();
    b.ts[b.write % BRUSH_TS_SIZE] = millis();
    b.write++;
    xSemaphoreGive(stateMutex);
}

uint32_t countInWindow(const uint32_t *ts, uint32_t now) {
    uint32_t n = 0;
    for (uint16_t i = 0; i < BRUSH_TS_SIZE; i++) {
        if (now - ts[i] <= (uint32_t)BRUSH_WINDOW_S * 1000UL) n++;
    }
    return n;
}

void loadBrushes() {
    String reg = prefs.getString("brushes", "");
    if (!reg.length()) return;

    Serial.println("Bekannte Bürsten (aus NVS):");
    int start = 0;
    while (start < (int)reg.length()) {
        int comma = reg.indexOf(',', start);
        if (comma < 0) comma = reg.length();
        String entry = reg.substring(start, comma);
        int eq = entry.indexOf('=');
        if (eq > 0) {
            std::string mac(entry.substring(0, eq).c_str());
            BrushState &b = brushes[mac];
            b.name = entry.substring(eq + 1).c_str();
            Serial.printf("  %s  (%s)\n",
                          brushIdFor(mac, b.name).c_str(), mac.c_str());
        }
        start = comma + 1;
    }
}

void brushPublish() {  // loop-Kontext
    static uint32_t lastWindow = 0;
    static bool firstRun = true;
    static std::map<std::string, uint32_t> lastTotal;
    static String lastWrittenReg;

    if (!mqtt.connected()) return;
    uint32_t now = millis();
    if (now - lastWindow < (uint32_t)BRUSH_WINDOW_S * 1000UL) return;
    lastWindow = now;

    // Snapshot ziehen (die map wird im BLE-Task erweitert)
    struct Snap { uint32_t count; int rssi; std::string id; };
    std::vector<Snap> snaps;
    uint32_t total = 0;

    String reg;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (auto &kv : brushes) {
        Snap s;
        s.count = countInWindow(kv.second.ts, now);
        s.rssi  = kv.second.rssi;
        s.id    = brushIdFor(kv.first, kv.second.name).c_str();
        total += s.count;
        snaps.push_back(s);

        // NVS-Registry "mac=Modellname" (Namen können nachträglich dazukommen)
        if (reg.length()) reg += ",";
        reg += kv.first.c_str();
        reg += "=";
        reg += kv.second.name.c_str();
    }
    xSemaphoreGive(stateMutex);

    // neue/veränderte Einträge dauerhaft sichern (nur bei tatsächlicher Änderung)
    if (reg != lastWrittenReg && reg.length()) {
        lastWrittenReg = reg;
        prefs.putString("brushes", reg);
        Serial.println("Bürsten-Liste im NVS aktualisiert.");
    }

    char buf[12];
    snprintf(buf, sizeof(buf), "%u", (unsigned)total);
    mqtt.publish(MQTT_TOPIC_OCLEAN "/adv_pro_min", buf, MQTT_RETAIN);

    for (auto &s : snaps) {
        String base = String(MQTT_TOPIC_OCLEAN) + "/" + s.id.c_str();

        auto pt = lastTotal.find(s.id);
        if (firstRun || pt == lastTotal.end() || pt->second != s.count) {
            lastTotal[s.id] = s.count;
            snprintf(buf, sizeof(buf), "%u", (unsigned)s.count);
            mqtt.publish((base + "/adv_pro_min").c_str(), buf, MQTT_RETAIN);
            snprintf(buf, sizeof(buf), "%d", s.rssi);
            mqtt.publish((base + "/rssi").c_str(), buf, MQTT_RETAIN);
        }
    }
    firstRun = false;
}

// Geräte-ID "H5075_XXXX" bzw. "Qingping_XXXX" = letzte 4 Hex-Stellen
// der MAC (Govee-Benennung wie die Govee-App)
String deviceIdFor(const std::string &mac, uint8_t kind) {
    std::string tail = mac.substr(mac.size() - 5);
    tail.erase(std::remove(tail.begin(), tail.end(), ':'), tail.end());
    std::transform(tail.begin(), tail.end(), tail.begin(), ::toupper);
    return String(kind == DEV_QINGPING ? "Qingping_" : "H5075_") + tail.c_str();
}

String baseTopicFor(const std::string &mac, uint8_t kind) {
    if (kind == DEV_QINGPING) {
        return String(MQTT_TOPIC_QINGPING) + "/" + deviceIdFor(mac, kind);
    }
    return String(MQTT_BASETOPIC) + "/" + deviceIdFor(mac, kind);
}

// ------------------------------------------------------------
// Persistente Geräte-Liste im NVS (überlebt Neustart & Reflash).
// Alle Geräte in einem Key: "mac=H5075_XXXX,mac=H5075_YYYY,..."
// Es wird nur beim ERSTMALIGEN Auffinden geschrieben (kein Verschleiss).
// ------------------------------------------------------------
void persistNewDevices() {  // nur mit gehaltener stateMutex aufrufen
    String reg;
    for (auto &kv : devices) {
        if (kv.second.persisted) continue;
        if (reg.length()) reg += ",";
        reg += kv.first.c_str();
        reg += "=";
        reg += deviceIdFor(kv.first, kv.second.kind);
        kv.second.persisted = true;
    }
    if (!reg.length()) return;

    String old = prefs.getString("devices", "");
    if (old.length()) reg = old + "," + reg;
    prefs.putString("devices", reg);
    Serial.printf("Neues Gerät im NVS gespeichert: %s\n", reg.c_str());
}

void loadKnownDevices() {
    String reg = prefs.getString("devices", "");
    if (!reg.length()) return;

    Serial.println("Bekannte Geräte (aus NVS):");
    int start = 0;
    while (start < (int)reg.length()) {
        int comma = reg.indexOf(',', start);
        if (comma < 0) comma = reg.length();
        String entry = reg.substring(start, comma);
        int eq = entry.indexOf('=');
        if (eq > 0) {
            std::string mac(entry.substring(0, eq).c_str());
            DeviceState &st = devices[mac];
            st.persisted = true;
            st.kind = entry.substring(eq + 1).startsWith("Qingping_")
                          ? DEV_QINGPING : DEV_GOVEE;
            Serial.printf("  %s  (%s)\n", entry.substring(eq + 1).c_str(),
                          mac.c_str());
        }
        start = comma + 1;
    }
}

// ------------------------------------------------------------
// Qingping Air Monitor: Service Data an UUID 0xFDCD
//   [0] 0x08 (Bit 7 optional gesetzt), [1] Flags,
//   [2..7] MAC (reversed), danach TLV-Ketten [Typ][Länge][Wert]:
//     0x01 len4: Temperatur int16/10, Feuchte uint16/10
//     0x02 len1: Batterie % (Bit 7 = Laedt)
//     0x07/0x12 len4: PM2.5, PM10 (ug/m3)
//     0x08/0x13 len2: CO2 (ppm)
//     0x09 len2: Beleuchtung (Lux)
// ------------------------------------------------------------
bool decodeQingping(const std::string &sd, DeviceState &out) {
    if (sd.size() < 11 || ((uint8_t)sd[0] & 0x7F) != 0x08) return false;

    size_t i = 8;
    bool found = false;
    while (i + 2 <= sd.size()) {
        uint8_t type = (uint8_t)sd[i];
        uint8_t len  = (uint8_t)sd[i + 1];
        if (i + 2 + len > sd.size()) break;
        const uint8_t *v = (const uint8_t *)sd.data() + i + 2;

        switch (type) {
            case 0x01:
                if (len >= 4) {
                    out.temperature = (int16_t)(v[0] | (v[1] << 8)) / 10.0f;
                    out.humidity    = (v[2] | (v[3] << 8)) / 10.0f;
                    found = true;
                }
                break;
            case 0x02:
                if (len >= 1) { out.battery = (float)(v[0] & 0x7F); found = true; }
                break;
            case 0x07:  // aeltere Qingping-Modelle
            case 0x12:  // Air Monitor Lite (durch Roh-Frames bestätigt)
                if (len >= 4) {
                    out.pm25 = v[0] | (v[1] << 8);
                    out.pm10 = v[2] | (v[3] << 8);
                    found = true;
                }
                break;
            case 0x08:
            case 0x13:
                if (len >= 2 && isnan(out.co2)) {
                    out.co2 = v[0] | (v[1] << 8);
                    found = true;
                }
                break;
            case 0x09:
                if (len >= 2) { out.light = v[0] | (v[1] << 8); found = true; }
                break;
        }
        i += 2 + len;
    }
    return found;
}

// ------------------------------------------------------------
// Lesung in den Geräte-Zustand übernehmen (thread-sicher)
// ------------------------------------------------------------
void applyReading(const std::string &mac, DeviceKind kind, const DeviceState &r) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool wasNew = (devices.find(mac) == devices.end());
    DeviceState &st = devices[mac];
    if (wasNew) st.persisted = false;  // neu -> beim nächsten publishPending im NVS sichern
    st.kind = kind;
    st.rssi = r.rssi;

    // Nur echte Messwerte lösen ein MQTT-Update aus. Batterie- und
    // RSSI-Änderungen werden stillschweigend übernommen und laufen mit
    // der nächsten Messwert-Änderung (oder dem Herzschlag) mit.
    bool changed = (r.temperature != st.temperature) ||
                   (r.humidity    != st.humidity) ||
                   (r.co2         != st.co2) ||
                   (r.pm25        != st.pm25) ||
                   (r.pm10        != st.pm10) ||
                   (r.light       != st.light);
    // Nur echte Messwert-Änderungen (oder die erste Lesung) publizieren.
    if (changed || isnan(st.temperature)) {
        st.temperature = r.temperature;
        st.humidity    = r.humidity;
        st.battery     = r.battery;
        st.co2         = r.co2;
        st.pm25        = r.pm25;
        st.pm10        = r.pm10;
        st.light       = r.light;
        st.pending     = true;
    }
    xSemaphoreGive(stateMutex);
}

// ------------------------------------------------------------
// WLAN / MQTT
// ------------------------------------------------------------
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.persistent(false);
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // C3 SuperMini: Standard-TX-Leistung (20 dBm) überfordert die kleine
    // Stromversorgung -> Radio "hört" nichts (leere Scan-Liste). Bekannter Fix:
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Verbinde mit WLAN \"%s\"", WIFI_SSID);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WLAN ok, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("WLAN \"%s\" nicht verbunden - sichtbare Netze:\n", WIFI_SSID);
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; i++) {
            Serial.printf("  %-24s Ch%2d %4d dBm  Auth %d\n",
                          WiFi.SSID(i).c_str(), WiFi.channel(i),
                          WiFi.RSSI(i), WiFi.encryptionType(i));
        }
        WiFi.scanDelete();
        Serial.println("(steht das Ziel-SSID nicht in der Liste -> Band/Kanal-"
                       "Problem; steht es mit Auth 3 (WPA2/WPA3) -> Sicherheits-"
                       "Modus prüfen)");
    }
}

void mqttConnect() {
    if (mqtt.connected()) return;

    String clientId = String(HOSTNAME) + "-" +
                      String((uint32_t)ESP.getEfuseMac(), HEX);
    const char *user = (MQTT_USER[0] != '\0') ? MQTT_USER : nullptr;
    const char *pass = (MQTT_PASSWORD[0] != '\0') ? MQTT_PASSWORD : nullptr;

    // Last Will: bei Ausfall des Gateways erscheint "offline" im Broker
    // (MQTT_BASETOPIC leer -> kein Status-/Version-Topic, connect ohne LWT)
    bool hasBase = strlen(MQTT_BASETOPIC) > 0;
    String statusTopic = String(MQTT_BASETOPIC) + "/status";
    bool ok = hasBase
                  ? mqtt.connect(clientId.c_str(), user, pass,
                                 statusTopic.c_str(), 1, true, "offline")
                  : mqtt.connect(clientId.c_str(), user, pass);
    if (ok) {
        Serial.printf("MQTT verbunden: %s:%d\n", MQTT_SERVER, MQTT_PORT);
        if (hasBase) {
            mqtt.publish(statusTopic.c_str(), "online", true);
            // Versionsnummer mit published (retained), damit sie im ioBroker ablesbar ist
            mqtt.publish((String(MQTT_BASETOPIC) + "/version").c_str(),
                         PROJECT_VERSION, true);
        }
    } else {
        Serial.printf("MQTT fehlgeschlagen (rc=%d), wird weiter versucht ...\n",
                      mqtt.state());
    }
}

// ------------------------------------------------------------
// BLE-Scan: nur Werte empfangen und zwischenspeichern.
// (Publishing passiert bewusst in loop(), nicht im BLE-Task.)
// ------------------------------------------------------------
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        // --- Diagnose: zählt alles mit ---
        advCount++;
        if (dev->haveServiceData()) sdCount++;
        if (dev->haveManufacturerData()) mfgCount++;

        std::string mac = dev->getAddress().toString();

#if DEBUG_ADV
        std::string name = dev->haveName() ? dev->getName() : "";
        bool goveeish = (name.find("GV") != std::string::npos);
        bool firstTime = loggedMacs.insert(mac).second;

        if (goveeish || firstTime) {
            Serial.printf("[ADV] %s name='%s' svc=%u mfg=%u rssi=%d\n",
                          mac.c_str(), name.c_str(),
                          dev->getServiceDataCount(),
                          dev->getManufacturerDataCount(),
                          dev->getRSSI());
            for (uint8_t i = 0; i < dev->getServiceDataCount(); i++) {
                std::string sd = dev->getServiceData(i);
                Serial.printf("      svcUUID=%s len=%u data=%s\n",
                              dev->getServiceDataUUID(i).toString().c_str(),
                              (unsigned)sd.size(), hexDump(sd).c_str());
            }
            for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
                Serial.printf("      mfgData=%s\n",
                              hexDump(dev->getManufacturerData(i)).c_str());
            }
        }
#endif

        // MQTT_TOPIC_OCLEAN leer -> Bürsten-Bereich deaktiviert
        if (strlen(MQTT_TOPIC_OCLEAN) > 0 && isToothbrushFrame(dev)) {
#if DEBUG_BR
            // Diagnose: alle 15 s Roh-Daten einer Bürste - damit suchen wir
            // ein Nutzungs-Signal in der Payload (Standby vs. Motor läuft)
            static uint32_t lastBrDump = 0;
            if (dev->haveServiceData() && millis() - lastBrDump > 15000) {
                lastBrDump = millis();
                std::string sd = dev->getServiceData(NimBLEUUID((uint16_t)0xFEAF));
                Serial.printf("[BR-Roh] rssi=%d name='%s' data=%s\n",
                              dev->getRSSI(),
                              (dev->haveName() ? dev->getName() : "").c_str(),
                              hexDump(sd, 24).c_str());
            }
#endif
            brushFrame(dev, mac);
        }

        // --- Qingping Air Monitor: Service Data an UUID 0xFDCD ---
        // MQTT_TOPIC_QINGPING leer -> Bereich deaktiviert
        if (strlen(MQTT_TOPIC_QINGPING) > 0 && dev->haveServiceData()) {
            std::string sd = dev->getServiceData(NimBLEUUID((uint16_t)0xFDCD));
            if (sd.size() >= 11 && ((uint8_t)sd[0] & 0x7F) == 0x08) {
                DeviceState r;
                if (decodeQingping(sd, r)) {
                    // Diagnose: alle 30 s ein Roh-Frame, damit wir sehen,
                    // welche TLV-Varianten das Gerät tatsächlich sendet
#if DEBUG_QP
                    static uint32_t lastQpDump = 0;
                    if (millis() - lastQpDump > 30000) {
                        lastQpDump = millis();
                        Serial.printf("[QP-Roh] rssi=%d: %s\n",
                                      dev->getRSSI(), hexDump(sd, 24).c_str());
                    }
#endif
                    r.rssi = dev->getRSSI();
                    applyReading(mac, DEV_QINGPING, r);
                    return;
                }
            }
        }

        // --- Govee H5075/H5077 ---
        // MQTT_BASETOPIC leer -> Bereich deaktiviert
        if (strlen(MQTT_BASETOPIC) == 0) return;
        // Format FW 1.04.x: Manufacturer Data "88 EC" (Govee) + 6 Bytes
        // [00, T2, T1, T0, Batt, 00]. Aeltere Firmwares legen die 6 Bytes
        // stattdessen als Service Data an die UUID 0xEC88 - beides akzeptieren.
        std::string payload;
        if (dev->haveManufacturerData()) {
            std::string mfg = dev->getManufacturerData();
            if (mfg.size() >= 8 && (uint8_t)mfg[0] == 0x88 && (uint8_t)mfg[1] == 0xEC) {
                payload = mfg.substr(2, 6);
            }
        }
        if (payload.empty() && dev->haveServiceData()) {
            std::string sd = dev->getServiceData(NimBLEUUID((uint16_t)0xEC88));
            if (sd.size() == 6) payload = sd;
        }
        if (payload.size() != 6) return;

        GoveeReading r;
        if (!decodeGovee((const uint8_t *)payload.data(), r)) return;

        DeviceState g;
        g.temperature = r.temperature;
        g.humidity    = r.humidity;
        g.battery     = r.battery;
        g.rssi        = dev->getRSSI();
        applyReading(mac, DEV_GOVEE, g);
    }
};

ScanCallbacks scanCallbacks;

void startBle() {
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&scanCallbacks, true /* Wiederholungen auch melden */);
    scan->setActiveScan(true);    // aktiv: holt auch Scan-Responses (Gerätenamen)
    scan->setInterval(0x40);      // 64 ms Intervall, 48 ms Fenster:
    scan->setWindow(0x30);        // lässt dem WLAN Funkzeit für Coexistence
    // Dauer 0 = endloser Scan; Variante mit Callback läuft nicht-blockierend
    scan->start(0, [](NimBLEScanResults results) {
        Serial.println("BLE-Scan endete, wird neu gestartet ...");
    }, false);
    Serial.println("BLE-Dauerscan gestartet.");
}

// ------------------------------------------------------------
// Publish der gemerkten Werte
// ------------------------------------------------------------
void publishPending() {
    std::vector<std::pair<std::string, DeviceState>> toSend;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    persistNewDevices();
    for (auto &kv : devices) {
        if (!kv.second.pending) continue;
        if (millis() - kv.second.lastPublish < (uint32_t)MIN_PUBLISH_INTERVAL_S * 1000UL) {
            continue;  // bleibt pending, nächster Aufruf
        }
        toSend.emplace_back(kv.first, kv.second);
        kv.second.pending = false;
        kv.second.lastPublish = millis();
    }
    xSemaphoreGive(stateMutex);

    for (auto &e : toSend) {
        if (isnan(e.second.temperature)) continue;
        String base = baseTopicFor(e.first, e.second.kind);
        char buf[16];

        snprintf(buf, sizeof(buf), "%.1f", e.second.temperature);
        mqtt.publish((base + "/temperature").c_str(), buf, MQTT_RETAIN);
        snprintf(buf, sizeof(buf), "%.1f", e.second.humidity);
        mqtt.publish((base + "/humidity").c_str(), buf, MQTT_RETAIN);
        if (!isnan(e.second.battery)) {
            snprintf(buf, sizeof(buf), "%.0f", e.second.battery);
            mqtt.publish((base + "/battery").c_str(), buf, MQTT_RETAIN);
        }
        snprintf(buf, sizeof(buf), "%d", e.second.rssi);
        mqtt.publish((base + "/rssi").c_str(), buf, MQTT_RETAIN);

        if (e.second.kind == DEV_QINGPING) {
            if (!isnan(e.second.co2)) {
                snprintf(buf, sizeof(buf), "%.0f", e.second.co2);
                mqtt.publish((base + "/co2").c_str(), buf, MQTT_RETAIN);
            }
            if (!isnan(e.second.pm25)) {
                snprintf(buf, sizeof(buf), "%.0f", e.second.pm25);
                mqtt.publish((base + "/pm25").c_str(), buf, MQTT_RETAIN);
            }
            if (!isnan(e.second.pm10)) {
                snprintf(buf, sizeof(buf), "%.0f", e.second.pm10);
                mqtt.publish((base + "/pm10").c_str(), buf, MQTT_RETAIN);
            }
            if (!isnan(e.second.light)) {
                snprintf(buf, sizeof(buf), "%.0f", e.second.light);
                mqtt.publish((base + "/light").c_str(), buf, MQTT_RETAIN);
            }
#if DEBUG_VAL
            Serial.printf("MQTT -> %s: %.1f C | %.1f %% | CO2 %.0f | PM2.5 %.0f | PM10 %.0f | %d dBm\n",
                          base.c_str(), e.second.temperature, e.second.humidity,
                          e.second.co2, e.second.pm25, e.second.pm10, e.second.rssi);
#endif
        } else {
#if DEBUG_VAL
            Serial.printf("MQTT -> %s: %.1f C | %.1f %% | %.0f %% Batt | %d dBm\n",
                          base.c_str(), e.second.temperature, e.second.humidity,
                          e.second.battery, e.second.rssi);
#endif
        }

#if LED_FEEDBACK
        digitalWrite(LED_PIN, LOW);   // kurze LED-Bestätigung
        delay(15);
        digitalWrite(LED_PIN, HIGH);
#endif
    }
}

// ------------------------------------------------------------
void setup() {
#if LED_TEST
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);   // Test: blaue LED dauerhaft an (active-LOW)
#elif LED_FEEDBACK
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED aus (active LOW)
#else
    // Pin hochohmig legen: LED garantiert aus, unabhängig davon, ob die
    // Board-LED active-LOW oder (wie bei manchen SuperMini-Klonen) active-HIGH
    // verdrahtet ist.
    pinMode(LED_PIN, INPUT);
#endif

    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== BLE-Gateway  v" PROJECT_VERSION "  (Govee | Qingping | Oclean) ===");

    stateMutex = xSemaphoreCreateMutex();

    prefs.begin("govee", false);
    loadKnownDevices();
    if (strlen(MQTT_TOPIC_OCLEAN) > 0) loadBrushes();

    connectWiFi();
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);
    mqttConnect();

    startBle();

    // OTA: Firmware-Updates übers Netzwerk
    ArduinoOTA.setHostname(HOSTNAME);
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        // BLE-Scan anhalten, damit das WLAN die volle Funkzeit bekommt
        NimBLEDevice::getScan()->stop();
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Firmware" : "Filesystem";
        Serial.println("\nOTA-Update (" + type + ") gestartet ...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA abgeschlossen, Neustart ...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA-Fortschritt: %u%%\r", (progress * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA-Fehler [%u]: ", error);
        if (error == OTA_AUTH_ERROR)         Serial.println("Auth fehlgeschlagen");
        else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin fehlgeschlagen");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Verbindung fehlgeschlagen");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Empfang fehlgeschlagen");
        else if (error == OTA_END_ERROR)     Serial.println("Ende fehlgeschlagen");
    });
    ArduinoOTA.begin();
    Serial.println("OTA bereit (Host: " HOSTNAME ").");
}

void loop() {
#if DEBUG_STAT
    static uint32_t lastStat = 0;
    if (millis() - lastStat > 10000) {
        lastStat = millis();
        Serial.printf("[BLE-Stat] %u Adv gesamt | %u mit ServiceData | %u mit MfgData\n",
                      advCount, sdCount, mfgCount);
    }
#endif
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected()) mqttConnect();
    mqtt.loop();
    ArduinoOTA.handle();
    if (strlen(MQTT_TOPIC_OCLEAN) > 0) brushPublish();
    publishPending();
    delay(20);
}
