#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <cJSON.h>
#include <driver/twai.h>
#include <lvgl.h>
#include "dispatch.h"

struct Config {
    char ssid[33]{}, password[65]{}, meter[64]{};
    char soyoMeter[64]{};
    int soyoChannel = 0;
    int soyoPolarity = -1;
    int channel = -1;
    int polarity = 1;
    int soyoCount = 0;
    int soyoUnit = 0;
    int huaweiCount = 0;
    int volts10 = 0;
    int amps10 = 0;
    int ampScale10 = 0;   // nur fuer Modell 0 ("Eigener Faktor"), Faktor x 10
    int reserve = 25;
    int model = 3;        // Index in rectifierModels, Standard R4875G1
    // Verhalten des Huawei, wenn keine CAN-Befehle mehr vom CYD kommen
    // (CYD aus, Kabel ab). Die Werte speichert der Huawei selbst (Offline-Werte).
    int fallbackMode = 0;     // 0 = Laden stoppen (0 A), 1 = mit festen Werten weiterladen
    int fallbackVolts10 = 0;  // Offline-Ladespannung in 0,1 V
    int fallbackAmps10 = 0;   // Offline-Strom je Geraet in 0,1 A
    // Netzmessung: Shelly per HTTP oder ein beliebiges MQTT-Abo mit JSONPath
    int meterSource = 0;          // 0 = Shelly, 1 = MQTT
    char meterTopic[128]{};       // z. B. tele/stromzaehler/SENSOR
    char meterPath[96]{};         // z. B. $.ENERGY.Power, leer = Nachricht ist nur eine Zahl
    int meterTimeout = 15;        // s, aeltere MQTT-Werte gelten als ausgefallen
    // MQTT-Broker (fuer Abo und Home-Assistant-Discovery)
    char mqttHost[64]{};
    int mqttPort = 1883;
    char mqttUser[33]{}, mqttPass[65]{};
    char mqttBase[33] = "huaweisoyo";
    int haDiscovery = 1;
};

// Huawei-Gleichrichter: Der CAN-Stromwert ist Ampere x (1024 / Nennstrom),
// wie in craigpeacock/Huawei_R4850G2_CAN und mjpalmowski/CAN-BUS-control-R4875G1.
struct RectifierModel { const char *name; int ratedAmps; };
const RectifierModel rectifierModels[] = {
    {"Eigener Faktor (unten eintragen)", 75},
    {"R4830G (30 A)", 30},
    {"R4850G2 (50 A)", 50},
    {"R4875G1 (75 A)", 75},
};
constexpr int modelCount = sizeof(rectifierModels) / sizeof(rectifierModels[0]);

float ampFactor(const Config &c) {
    if (c.model <= 0 || c.model >= modelCount) return c.ampScale10 / 10.0f;
    return 1024.0f / rectifierModels[c.model].ratedAmps;
}

Config cfg;
Dispatch dispatch;
Mode mode = Mode::Idle;
Preferences prefs;
WebServer web(80);
DNSServer dns;
// ---- Display ESP32-2432S028R (gleiche Einstellungen wie Simply Open Camper, sat_display.cpp) ----
// Display ILI9341 an HSPI (SCK 14, MOSI 13, MISO 12, CS 15, DC 2), Licht GPIO 21.
// Touch XPT2046 an VSPI (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36).
#define DISPLAY_ROTATION 1   // quer; steht das Bild kopf: 3 (Touch dreht automatisch mit)
class CydDisplay : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
    lgfx::Touch_XPT2046 touch_;

public:
    CydDisplay() {
        {
            auto c = bus_.config();
            c.spi_host = SPI2_HOST;  // HSPI
            c.spi_mode = 0;
            c.freq_write = 40000000;
            c.freq_read = 16000000;
            c.spi_3wire = false;
            c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = 14;
            c.pin_mosi = 13;
            c.pin_miso = 12;
            c.pin_dc = 2;
            bus_.config(c);
            panel_.setBus(&bus_);
        }
        {
            auto c = panel_.config();
            c.pin_cs = 15;
            c.pin_rst = -1;
            c.pin_busy = -1;
            c.panel_width = 240;
            c.panel_height = 320;
            c.offset_rotation = 0;
            c.readable = true;
            c.invert = false;
            c.rgb_order = false;
            c.dlen_16bit = false;
            c.bus_shared = false;
            panel_.config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = 21;
            c.invert = false;
            c.freq = 12000;
            c.pwm_channel = 7;
            light_.config(c);
            panel_.setLight(&light_);
        }
        {
            auto c = touch_.config();
            // Rohwerte an den Raendern (am echten Board ermittelt, Y vertauscht).
            // Genauer wird es mit der Kalibrierung (beim Einschalten 2 s auf den Bildschirm druecken).
            c.x_min = 300;
            c.x_max = 3900;
            c.y_min = 3700;
            c.y_max = 200;
            c.pin_int = 36;
            c.bus_shared = false;
            c.offset_rotation = 0;
            c.spi_host = SPI3_HOST;  // VSPI
            c.freq = 1000000;
            c.pin_sclk = 25;
            c.pin_mosi = 32;
            c.pin_miso = 39;
            c.pin_cs = 33;
            touch_.config(c);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
CydDisplay lcd;
QueueHandle_t readings, soyoReadings;
bool portal, canReady;
uint32_t lastTx, lastControl, lastControlledSample;
int manual = 0, selection = 100;
struct Sample { int watts; uint32_t time; bool valid; };
Sample meter{}, soyoMeter{};
struct Rectifier {
    float dc, ac, temp;                  // Ausgangsleistung, Eingangsleistung, Temperatur
    float outV, outA, inV, inA, eff;     // weitere Messwerte fuer MQTT / Home Assistant
    uint32_t dcAt, acAt, tempAt;
    bool fallbackSent;
};
Rectifier rectifier[3]{};

bool recent(uint32_t stamp, uint32_t age) {
    return stamp && millis() - stamp <= age;
}

bool canSend(uint32_t id, const uint8_t *data) {
    twai_message_t msg{};
    msg.identifier = id;
    msg.extd = 1;
    msg.data_length_code = 8;
    memcpy(msg.data, data, 8);
    return twai_transmit(&msg, pdMS_TO_TICKS(4)) == ESP_OK;
}

bool command(uint32_t address, uint8_t code, unsigned value) {
    const uint8_t frame[] = {1, code, 0, 0, 0, 0, uint8_t(value >> 8), uint8_t(value)};
    return canSend(address, frame);
}

void sendOutputs() {
    lastTx = millis();
    if (cfg.soyoCount) {
        const unsigned watts = dispatch.soyo / cfg.soyoCount;
        uint8_t frame[] = {0x24, 0x56, 0, 0x21, uint8_t(watts >> 8), uint8_t(watts), 0x80, 0xff};
        for (int i = 1; i < 7; ++i) frame[7] -= frame[i];
        Serial.write(frame, sizeof(frame));  // UART0 TX = GPIO1 (P1-Stecker neben USB)
    }
    if (!canReady || !cfg.huaweiCount) return;
    // Strom je Geraet in A: Leistung / Spannung, begrenzt auf den eingestellten Maximalstrom.
    const float amps = cfg.volts10
        ? min(cfg.amps10 / 10.0f, dispatch.huawei * 10.0f / (cfg.huaweiCount * cfg.volts10))
        : 0.0f;
    const unsigned current = unsigned(amps * ampFactor(cfg));
    for (int i = 0; i < cfg.huaweiCount; ++i) {
        const uint32_t node = (i + 1) << 16;
        const uint32_t address = 0x108080FE | node;
        uint8_t power[] = {1, 0x32, 0, 1, 0, 0, 0, 0};
        if (current == 0) canSend(address, power);
        auto &unit = rectifier[i];
        if (!recent(unit.acAt, 3000)) unit.fallbackSent = false;
        // Offline-Werte (0x0101 Spannung, 0x0104 Strom) nur einmal je Verbindung senden,
        // weil der Huawei sie dauerhaft speichert.
        if (!unit.fallbackSent && recent(unit.acAt, 3000)) {
            const bool keepCharging = cfg.fallbackMode == 1;
            const unsigned fallbackCurrent =
                keepCharging ? unsigned(cfg.fallbackAmps10 / 10.0f * ampFactor(cfg)) : 0;
            unit.fallbackSent =
                (!keepCharging || command(address, 0x01, cfg.fallbackVolts10 * 1024 / 10)) &&
                command(address, 0x04, fallbackCurrent);
        }
        const bool voltageSet = cfg.volts10 && command(address, 0, cfg.volts10 * 1024 / 10);
        const bool currentSet = command(address, 3, current);
        if (current > 0 && unit.fallbackSent && voltageSet && currentSet) {
            power[3] = 0;
            canSend(address, power);
        }
        const uint8_t poll[8]{};
        canSend(0x108040FE | node, poll);
    }
}

// Merkt sich im Flash, ob nach einem Neustart wieder Nulleinspeisung aktiv sein soll.
// Geschrieben wird nur bei einer Aenderung, um den Flash zu schonen.
bool autoZero = false;

void stop(Mode next, bool remember = true) {
    const bool zero = next == Mode::Zero;
    if (remember && zero != autoZero && prefs.putBool("autoZero", zero)) autoZero = zero;
    mode = next;
    manual = 0;
    dispatch.stop(millis());
    dispatch.neutralSince = millis();
    sendOutputs();
}

void receiveCan() {
    if (!canReady) return;
    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
        if (status.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
        if (status.state == TWAI_STATE_STOPPED) twai_start();
    }
    twai_message_t m{};
    for (int n = 0; n < 48 && twai_receive(&m, 0) == ESP_OK; ++n) {
        const int index = ((m.identifier >> 16) & 15) - 1;
        if (!m.extd || m.rtr || index < 0 || index >= cfg.huaweiCount ||
            (m.identifier & 0xfff0ffff) != 0x1080407f ||
            m.data_length_code != 8 || m.data[0] != 1) continue;
        const float value = ((uint32_t(m.data[4]) << 24) | (uint32_t(m.data[5]) << 16) |
                             (uint32_t(m.data[6]) << 8) | m.data[7]) / 1024.0f;
        auto &r = rectifier[index];
        if (m.data[1] == 0x73) { r.dc = value; r.dcAt = millis(); }
        if (m.data[1] == 0x70) { r.ac = value; r.acAt = millis(); }
        if (m.data[1] == 0x7f) { r.temp = value; r.tempAt = millis(); }
        // Codes laut craigpeacock/Huawei_R4850G2_CAN (r4850.c), alle Werte / 1024
        if (m.data[1] == 0x75) r.outV = value;
        if (m.data[1] == 0x81) r.outA = value;
        if (m.data[1] == 0x78) r.inV = value;
        if (m.data[1] == 0x72) r.inA = value;
        if (m.data[1] == 0x74) r.eff = value;
    }
}

bool huaweiHealthy() {
    if (cfg.huaweiCount) {
        twai_status_info_t status{};
        if (!canReady || twai_get_status_info(&status) != ESP_OK ||
            status.state != TWAI_STATE_RUNNING) return false;
    }
    for (int i = 0; i < cfg.huaweiCount; ++i) {
        const auto &r = rectifier[i];
        if (!recent(r.acAt, 3000) || !recent(r.tempAt, 3000) || r.temp >= 85) return false;
    }
    return true;
}

float huaweiPower(bool dc) {
    float sum = 0;
    for (int i = 0; i < cfg.huaweiCount; ++i) {
        const auto &r = rectifier[i];
        if (!recent(dc ? r.dcAt : r.acAt, 3000)) return NAN;
        sum += dc ? r.dc : r.ac;
    }
    return sum;
}

bool number(const cJSON *obj, const char *key, double &value) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    value = item->valuedouble;
    return true;
}

bool parseMeter(const String &body, int channel, bool rpc, double &watts) {
    cJSON *root = cJSON_Parse(body.c_str());
    bool valid = false;
    if (rpc) {
        const auto *three = cJSON_GetObjectItemCaseSensitive(root, "em:0");
        char key[12];
        snprintf(key, sizeof(key), "em1:%d", max(0, channel));
        const auto *single = cJSON_GetObjectItemCaseSensitive(root, key);
        const auto *component = three ? three : single;
        const auto *errors = cJSON_GetObjectItemCaseSensitive(component, "errors");
        const char *phase[] = {"a_act_power", "b_act_power", "c_act_power"};
        valid = cJSON_GetArraySize(errors) == 0 &&
                (three ? number(three, channel < 0 ? "total_act_power" : phase[channel], watts)
                       : number(single, "act_power", watts));
    } else {
        const auto *list = cJSON_GetObjectItemCaseSensitive(root, "emeters");
        const int count = cJSON_GetArraySize(list);
        const int first = channel < 0 ? 0 : channel;
        const int end = channel < 0 && count == 3 ? 3 : first + 1;
        watts = 0;
        valid = end <= count;
        for (int i = first; valid && i < end; ++i) {
            const auto *item = cJSON_GetArrayItem(list, i);
            double power;
            valid = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item, "is_valid")) &&
                    number(item, "power", power);
            if (valid) watts += power;
        }
    }
    cJSON_Delete(root);
    return valid && fabs(watts) < 100000;
}

void pollMeter(void *argument) {
    const bool soyo = argument != nullptr;
    const char *host = soyo ? cfg.soyoMeter : cfg.meter;
    const int channel = soyo ? cfg.soyoChannel : cfg.channel;
    const int polarity = soyo ? cfg.soyoPolarity : cfg.polarity;
    const auto queue = soyo ? soyoReadings : readings;
    int protocol = 0;
    for (;;) {
        Sample sample{};
        if (WiFi.status() == WL_CONNECTED && host[0]) {
            for (int attempt = 0; attempt < 2 && !sample.valid; ++attempt) {
                const bool rpc = (protocol + attempt) % 2 == 0;
                WiFiClient client;
                HTTPClient http;
                http.setConnectTimeout(500);
                http.setTimeout(600);
                if (!http.begin(client, String("http://") + host +
                                        (rpc ? "/rpc/Shelly.GetStatus" : "/status"))) continue;
                double watts;
                if (http.GET() == 200 && parseMeter(http.getString(), channel, rpc, watts)) {
                    sample = {int(lround(watts)) * polarity, millis(), true};
                    protocol = rpc ? 0 : 1;
                }
            }
        }
        // Nur gueltige Werte weitergeben: ein einzelner Aussetzer soll die Regelung
        // nicht abschalten. Veraltete Werte erkennt meterOK() ueber das Alter.
        if (sample.valid) xQueueOverwrite(queue, &sample);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// MQTT: Netzwert aus einem Abo (JSONPath) und Werte fuer Home Assistant
// ============================================================================

// Einfacher JSONPath: "$.a.b", "a.b", "a[0].b", "$['em:0'].act_power", "a.0".
// Leerer Pfad oder "$": die ganze Nachricht ist die Zahl (z. B. "-123.4").
// Zahlen als Text ("123.4") werden ebenfalls akzeptiert.
bool textNumber(const char *text, double &out) {
    if (!text) return false;
    char *end;
    out = strtod(text, &end);
    while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') ++end;
    return end != text && *end == 0 && isfinite(out);
}

bool jsonPathValue(const char *payload, const char *path, double &out) {
    while (*path == ' ') ++path;
    if (*path == '$') ++path;
    if (!*path && textNumber(payload, out)) return true;
    cJSON *root = cJSON_Parse(payload);
    if (!root) return false;
    const cJSON *node = root;
    const char *p = path;
    char key[64];
    while (node && *p) {
        if (*p == '.') { ++p; continue; }
        size_t n = 0;
        bool quoted = false;
        if (*p == '[') {
            ++p;
            if (*p == '\'' || *p == '"') {
                const char quote = *p++;
                quoted = true;
                while (*p && *p != quote && n < sizeof(key) - 1) key[n++] = *p++;
                if (*p == quote) ++p;
            } else {
                while (*p && *p != ']' && n < sizeof(key) - 1) key[n++] = *p++;
            }
            if (*p == ']') ++p;
        } else {
            while (*p && *p != '.' && *p != '[' && n < sizeof(key) - 1) key[n++] = *p++;
        }
        key[n] = 0;
        char *end;
        const long index = strtol(key, &end, 10);
        if (cJSON_IsArray(node) && !quoted && n && !*end) {
            node = cJSON_GetArrayItem(node, index);
        } else {
            node = cJSON_GetObjectItemCaseSensitive(node, key);
        }
    }
    bool ok = false;
    if (cJSON_IsNumber(node) && isfinite(node->valuedouble)) { out = node->valuedouble; ok = true; }
    else if (cJSON_IsString(node)) ok = textNumber(node->valuestring, out);
    else if (cJSON_IsBool(node)) { out = cJSON_IsTrue(node) ? 1 : 0; ok = true; }
    cJSON_Delete(root);
    return ok;
}

WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);
String deviceId;                 // z. B. huaweisoyo_a1b2c3
volatile bool mqttOnline = false;
bool discoveryRequested = false;

String topicOf(const char *leaf) { return String(cfg.mqttBase) + "/" + leaf; }

bool hasWildcard(const char *topic) { return strchr(topic, '+') || strchr(topic, '#'); }

void mqttMessage(char *topic, byte *payload, unsigned int length) {
    if (!strcmp(topic, "homeassistant/status")) {
        // Home Assistant neu gestartet: Discovery erneut senden
        if (length == 6 && !memcmp(payload, "online", 6)) discoveryRequested = true;
        return;
    }
    if (cfg.meterSource != 1 || (!hasWildcard(cfg.meterTopic) && strcmp(topic, cfg.meterTopic))) return;
    String text;
    text.reserve(length + 1);
    for (unsigned i = 0; i < length; ++i) text += char(payload[i]);
    double watts;
    if (jsonPathValue(text.c_str(), cfg.meterPath, watts) && fabs(watts) < 100000) {
        const Sample sample{int(lround(watts)) * cfg.polarity, millis(), true};
        xQueueOverwrite(readings, &sample);
    }
}

struct Sensor { const char *key, *name, *unit, *deviceClass, *icon; };

void publishSensor(const Sensor &s) {
    String json = String("{\"name\":\"") + s.name + "\",\"has_entity_name\":true,\"uniq_id\":\"" + deviceId + "_" +
                  s.key + "\",\"obj_id\":\"" + deviceId + "_" + s.key + "\",\"stat_t\":\"" + topicOf("state") +
                  "\",\"avty_t\":\"" + topicOf("status") + "\",\"val_tpl\":\"{{ value_json." + s.key + " }}\"";
    if (s.unit) json += String(",\"unit_of_meas\":\"") + s.unit + "\",\"stat_cla\":\"measurement\"";
    if (s.deviceClass) json += String(",\"dev_cla\":\"") + s.deviceClass + "\"";
    if (s.icon) json += String(",\"ic\":\"") + s.icon + "\"";
    json += ",\"dev\":{\"ids\":[\"" + deviceId + "\"],\"name\":\"Huawei Soyo Regler\",\"mf\":\"DIY\","
            "\"mdl\":\"CYD ESP32-2432S028R\"}}";
    mqtt.publish(("homeassistant/sensor/" + deviceId + "/" + s.key + "/config").c_str(), json.c_str(), true);
}

void publishDiscovery() {
    static const Sensor common[] = {
        {"grid", "Netzleistung", "W", "power", nullptr},
        {"soyo_set", "Soyo Sollwert", "W", "power", "mdi:transmission-tower-import"},
        {"soyo_actual", "Soyo Einspeisung gemessen", "W", "power", "mdi:transmission-tower-import"},
        {"huawei_set", "Huawei Sollwert", "W", "power", "mdi:battery-charging"},
        {"huawei_dc", "Huawei Ladeleistung", "W", "power", "mdi:battery-charging"},
        {"huawei_ac", "Huawei Aufnahme", "W", "power", nullptr},
        {"reserve", "Restbezug Ziel", "W", "power", "mdi:target"},
        {"manual", "Nur Laden Leistung", "W", "power", "mdi:battery-arrow-up"},
        {"mode", "Modus", nullptr, nullptr, "mdi:state-machine"},
    };
    for (const auto &s : common) publishSensor(s);
    const char *suffix[] = {"dc", "ac", "v", "a", "vin", "temp", "eff"};
    const char *label[] = {"Ladeleistung", "Aufnahme", "Ausgangsspannung", "Ladestrom", "Netzspannung",
                           "Temperatur", "Wirkungsgrad"};
    const char *unit[] = {"W", "W", "V", "A", "V", "°C", "%"};
    const char *cls[] = {"power", "power", "voltage", "current", "voltage", "temperature", nullptr};
    for (int i = 1; i <= cfg.huaweiCount; ++i) {
        for (int k = 0; k < 7; ++k) {
            char key[12], name[40];
            snprintf(key, sizeof(key), "h%d_%s", i, suffix[k]);
            snprintf(name, sizeof(name), "Huawei %d %s", i, label[k]);
            publishSensor({key, name, unit[k], cls[k], k == 6 ? "mdi:percent" : nullptr});
        }
    }
}

// Zahl oder null (unbekannt) fuer den Status-JSON
String jsonNumber(float value, bool valid, int decimals = 0) {
    return valid && isfinite(value) ? String(value, decimals) : String("null");
}

void publishState() {
    const bool gridValid = meter.valid && recent(meter.time, cfg.meterSource == 1 ? cfg.meterTimeout * 1000UL : 6000);
    const bool soyoValid = cfg.soyoMeter[0] && soyoMeter.valid && recent(soyoMeter.time, 6000);
    const float dc = cfg.huaweiCount ? huaweiPower(true) : NAN;
    const float ac = cfg.huaweiCount ? huaweiPower(false) : NAN;
    String json = "{\"grid\":" + jsonNumber(meter.watts, gridValid) +
                  ",\"soyo_set\":" + String(dispatch.soyo) +
                  ",\"soyo_actual\":" + jsonNumber(soyoMeter.watts, soyoValid) +
                  ",\"huawei_set\":" + String(dispatch.huawei) +
                  ",\"huawei_dc\":" + jsonNumber(dc, cfg.huaweiCount > 0) +
                  ",\"huawei_ac\":" + jsonNumber(ac, cfg.huaweiCount > 0) +
                  ",\"reserve\":" + String(cfg.reserve) +
                  ",\"manual\":" + String(manual) +
                  ",\"mode\":\"" + (mode == Mode::Zero ? "nulleinspeisung" : mode == Mode::Manual ? "nur_laden" : "aus") + "\"";
    for (int i = 0; i < cfg.huaweiCount; ++i) {
        const auto &r = rectifier[i];
        const bool live = recent(r.acAt, 3000);
        const String p = String(",\"h") + (i + 1) + "_";
        json += p + "dc\":" + jsonNumber(r.dc, live) + p + "ac\":" + jsonNumber(r.ac, live) +
                p + "v\":" + jsonNumber(r.outV, live, 2) + p + "a\":" + jsonNumber(r.outA, live, 2) +
                p + "vin\":" + jsonNumber(r.inV, live, 1) + p + "temp\":" + jsonNumber(r.temp, live, 1) +
                p + "eff\":" + jsonNumber(r.eff * 100, live, 1);
    }
    json += "}";
    mqtt.publish(topicOf("state").c_str(), json.c_str());
}

// Eigener Task: ein haengender Verbindungsaufbau darf Regelung und Soyo-Befehle nicht aufhalten.
void mqttTask(void *) {
    mqtt.setBufferSize(4096);  // auch fuer grosse Shelly-/Tasmota-Nachrichten
    mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
    mqtt.setCallback(mqttMessage);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    uint32_t lastTry = 0, lastState = 0;
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!mqtt.connected()) {
                mqttOnline = false;
                if (!lastTry || millis() - lastTry > 5000) {
                    lastTry = millis();
                    const String will = topicOf("status");
                    if (mqtt.connect(deviceId.c_str(), cfg.mqttUser[0] ? cfg.mqttUser : nullptr,
                                     cfg.mqttUser[0] ? cfg.mqttPass : nullptr, will.c_str(), 0, true, "offline")) {
                        mqtt.publish(will.c_str(), "online", true);
                        if (cfg.meterSource == 1 && cfg.meterTopic[0]) mqtt.subscribe(cfg.meterTopic);
                        if (cfg.haDiscovery) {
                            mqtt.subscribe("homeassistant/status");
                            discoveryRequested = true;
                        }
                        lastState = 0;
                    }
                }
            } else {
                mqttOnline = true;
                mqtt.loop();
                if (discoveryRequested && cfg.haDiscovery) {
                    discoveryRequested = false;
                    publishDiscovery();
                }
                if (!lastState || millis() - lastState >= 5000) {
                    lastState = millis();
                    publishState();
                }
            }
        } else {
            mqttOnline = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

String escape(const char *s) {
    String result;
    for (; *s; ++s) {
        switch (*s) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += *s;
        }
    }
    return result;
}

void field(String &page, const char *label, const char *name, const String &value,
           const char *type = "text") {
    page += String("<label>") + label + "<input type=\"" + type + "\" name=\"" + name +
            "\" value=\"" + escape(value.c_str()) + "\"></label>";
}

void showWeb() {
    String page = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Huawei / Soyo · Einstellungen</title><style>
body{font:16px system-ui;background:#0d1720;color:#edf4f9;max-width:700px;margin:2rem auto;padding:0 1rem}
fieldset{border:1px solid #526473;border-radius:10px;margin:1rem 0}label{display:block;margin:.7rem}
input,select,button{box-sizing:border-box;display:block;width:100%;font:inherit;padding:.55rem;margin-top:.3rem;border-radius:6px}
button{background:#65e7c9;border:0;font-weight:bold}p{line-height:1.5}
#nets{margin:.3rem .7rem}.net{display:flex;justify-content:space-between;padding:.6rem .7rem;margin:.3rem 0;
background:#1b2838;border-radius:6px;cursor:pointer}.net:hover,.net.sel{background:#24425a}.net small{color:#8a9bb0}
#scan{background:#2b3d50;color:#edf4f9;margin:.3rem .7rem;width:calc(100% - 1.4rem)}
</style>
<script>
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂';}
function pick(el,s){document.querySelector('[name=ssid]').value=s;
 document.querySelectorAll('.net').forEach(n=>n.classList.remove('sel'));el.classList.add('sel');
 document.querySelector('[name=password]').focus();}
function scan(refresh){const b=document.getElementById('scan');b.textContent='Suche läuft …';
 fetch('/scan'+(refresh?'?refresh=1':'')).then(r=>r.json()).then(d=>{
  const box=document.getElementById('nets');box.innerHTML='';
  d.nets.forEach(n=>{const e=document.createElement('div');e.className='net';
   const name=document.createElement('span');name.textContent=(n.lock?'🔒 ':'')+n.ssid;
   const sig=document.createElement('small');sig.textContent=bars(n.rssi)+' '+n.rssi+' dBm';
   e.append(name,sig);e.onclick=()=>pick(e,n.ssid);box.append(e);});
  if(d.running){setTimeout(()=>scan(0),1000);}
  else{b.textContent=d.nets.length?'Erneut suchen':'Keine WLANs gefunden – erneut suchen';}
 }).catch(()=>{b.textContent='WLAN suchen';});}
window.addEventListener('load',()=>scan(0));
</script>
<h1>Einstellungen</h1><form method="post" action="/save"><fieldset><legend>WLAN</legend>
<div id="nets"></div><button type="button" id="scan" onclick="scan(1)">WLAN suchen</button>)HTML";
    field(page, "Name des Heim-WLANs (oben antippen oder eintippen)", "ssid", cfg.ssid);
    field(page, "Passwort (leer lässt das gespeicherte Passwort bestehen)", "password", "", "password");
    page += "</fieldset><fieldset><legend>Netzmessung</legend><label>Quelle<select name=\"meterSource\">";
    page += String("<option value=\"0\"") + (cfg.meterSource == 0 ? " selected" : "") + ">Shelly (HTTP)</option>";
    page += String("<option value=\"1\"") + (cfg.meterSource == 1 ? " selected" : "") +
            ">MQTT-Abo mit JSONPath</option></select></label>";
    page += "<p><b>Shelly</b></p>";
    field(page, "Shelly: IPv4-Adresse oder Hostname", "meter", cfg.meter);
    field(page, "Shelly: Kanal (0–2, −1 für Summe bei 3EM)", "channel", String(cfg.channel), "number");
    page += "<p><b>MQTT</b> (Broker unten eintragen)</p>";
    field(page, "MQTT: Topic, z. B. tele/zaehler/SENSOR oder shellies/em/emeter/0/power", "meterTopic", cfg.meterTopic);
    field(page, "MQTT: JSONPath, z. B. $.ENERGY.Power oder $.em:0.total_act_power (leer = Nachricht ist nur eine Zahl)",
          "meterPath", cfg.meterPath);
    field(page, "MQTT: Wert gilt als ausgefallen nach (s)", "meterTimeout", String(cfg.meterTimeout), "number");
    page += "<p><b>Gemeinsam</b></p>";
    field(page, "Vorzeichen (1 oder −1, Netzbezug muss positiv sein)", "polarity", String(cfg.polarity), "number");
    field(page, "Gewünschter Restbezug in W", "reserve", String(cfg.reserve), "number");
    page += "</fieldset><fieldset><legend>SoyoSource</legend>";
    field(page, "Anzahl gleicher Geräte (0–32)", "soyoCount", String(cfg.soyoCount), "number");
    field(page, "Maximalleistung je Gerät in W", "soyoUnit", String(cfg.soyoUnit), "number");
    field(page, "Shelly für Soyo-Istwert (optional)", "soyoMeter", cfg.soyoMeter);
    field(page, "Kanal für Soyo-Messung", "soyoChannel", String(cfg.soyoChannel), "number");
    field(page, "Vorzeichen Soyo (1 oder −1, Einspeisung muss positiv sein)", "soyoPolarity", String(cfg.soyoPolarity), "number");
    page += "</fieldset><fieldset><legend>Huawei</legend>";
    page += "<label>Gleichrichter-Modell (bestimmt den CAN-Stromfaktor)<select name=\"model\">";
    for (int i = 0; i < modelCount; ++i) {
        page += String("<option value=\"") + i + "\"" + (cfg.model == i ? " selected" : "") + ">" +
                rectifierModels[i].name + "</option>";
    }
    page += "</select></label>";
    field(page, "Anzahl gleicher Geräte mit CAN-Adressen 1 bis 3", "huaweiCount", String(cfg.huaweiCount), "number");
    field(page, "Ladespannung in 0,1 V (z. B. 520 = 52 V)", "volts10", String(cfg.volts10), "number");
    field(page, "Maximalstrom je Gerät in 0,1 A (höchstens Nennstrom des Modells)", "amps10", String(cfg.amps10), "number");
    field(page, "Nur bei „Eigener Faktor“: CAN-Stromfaktor × 10 (z. B. 137 = 13,7)", "ampScale10", String(cfg.ampScale10), "number");
    page += String("<p>Aktiver Stromfaktor: ") + String(ampFactor(cfg), 2) + "</p>";
    page += "</fieldset><fieldset><legend>Huawei ohne CAN-Signal</legend>"
            "<p>Gilt, wenn das CYD ausfällt oder das CAN-Kabel getrennt ist. "
            "Beim Weiterladen regelt niemand mehr: der Huawei zieht dann dauerhaft Strom aus dem Netz.</p>"
            "<label>Verhalten<select name=\"fallbackMode\">";
    page += String("<option value=\"0\"") + (cfg.fallbackMode == 0 ? " selected" : "") +
            ">Laden stoppen (0 A, empfohlen)</option>";
    page += String("<option value=\"1\"") + (cfg.fallbackMode == 1 ? " selected" : "") +
            ">Mit festen Werten weiterladen</option></select></label>";
    field(page, "Ladespannung ohne CAN in 0,1 V (z. B. 535 = 53,5 V)", "fallbackVolts10", String(cfg.fallbackVolts10), "number");
    field(page, "Strom ohne CAN je Gerät in 0,1 A", "fallbackAmps10", String(cfg.fallbackAmps10), "number");
    page += "</fieldset><fieldset><legend>MQTT / Home Assistant</legend>"
            "<p>Leer lassen, wenn kein MQTT genutzt wird. Die Werte erscheinen unter <code>";
    page += escape(cfg.mqttBase);
    page += "/state</code>.</p>";
    field(page, "Broker: IPv4-Adresse oder Hostname", "mqttHost", cfg.mqttHost);
    field(page, "Port", "mqttPort", String(cfg.mqttPort), "number");
    field(page, "Benutzer (optional)", "mqttUser", cfg.mqttUser);
    field(page, "Passwort (leer lässt das gespeicherte Passwort bestehen)", "mqttPass", "", "password");
    field(page, "Topic-Präfix", "mqttBase", cfg.mqttBase);
    page += "<label>Home-Assistant-Discovery<select name=\"haDiscovery\">";
    page += String("<option value=\"1\"") + (cfg.haDiscovery ? " selected" : "") + ">An – Sensoren erscheinen automatisch</option>";
    page += String("<option value=\"0\"") + (!cfg.haDiscovery ? " selected" : "") + ">Aus</option></select></label>";
    page += String("<p>Status: ") + (!cfg.mqttHost[0] ? "nicht eingerichtet" : mqttOnline ? "verbunden" : "nicht verbunden") + "</p>";
    page += "</fieldset><button>Speichern und neu starten</button></form>"
            "<form method=\"post\" action=\"/calibrate\"><button style=\"background:#526473;color:#fff\">"
            "Touch am Display kalibrieren</button></form></html>";
    web.send(200, "text/html; charset=utf-8", page);
}

bool readInt(const char *name, int &value, int low, int high) {
    const String raw = web.arg(name);
    char *end;
    const long parsed = strtol(raw.c_str(), &end, 10);
    if (!raw.length() || *end || parsed < low || parsed > high) return false;
    value = parsed;
    return true;
}

bool validHost(const String &host) {
    for (unsigned i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    return host.length() <= 63;
}

uint32_t rebootAt;
void saveWeb() {
    Config next = cfg;
    String ssid = web.arg("ssid"), password = web.arg("password"), host = web.arg("meter");
    const String soyoHost = web.arg("soyoMeter");
    const String mqttHost = web.arg("mqttHost"), mqttUser = web.arg("mqttUser"), mqttPass = web.arg("mqttPass");
    String mqttBase = web.arg("mqttBase"), meterTopic = web.arg("meterTopic"), meterPath = web.arg("meterPath");
    mqttBase.trim();
    meterTopic.trim();
    meterPath.trim();
    if (!mqttBase.length()) mqttBase = "huaweisoyo";
    bool baseOK = mqttBase.length() <= 32;
    for (unsigned i = 0; i < mqttBase.length(); ++i) {
        if (mqttBase[i] == '+' || mqttBase[i] == '#' || mqttBase[i] == ' ') baseOK = false;
    }
    if (!readInt("meterSource", next.meterSource, 0, 1) || !readInt("meterTimeout", next.meterTimeout, 3, 300) ||
        !readInt("mqttPort", next.mqttPort, 1, 65535) || !readInt("haDiscovery", next.haDiscovery, 0, 1) ||
        !validHost(mqttHost) || mqttUser.length() > 32 || mqttPass.length() > 64 || !baseOK ||
        meterTopic.length() > 127 || meterPath.length() > 95 ||
        (next.meterSource == 1 && (!mqttHost.length() || !meterTopic.length()))) {
        web.send(400, "text/plain; charset=utf-8",
                 "Bitte MQTT prüfen: Bei Quelle MQTT sind Broker und Topic nötig, Topic-Präfix ohne + # und Leerzeichen.");
        return;
    }
    mqttHost.toCharArray(next.mqttHost, sizeof(next.mqttHost));
    mqttUser.toCharArray(next.mqttUser, sizeof(next.mqttUser));
    if (mqttPass.length() || mqttHost != cfg.mqttHost) mqttPass.toCharArray(next.mqttPass, sizeof(next.mqttPass));
    mqttBase.toCharArray(next.mqttBase, sizeof(next.mqttBase));
    meterTopic.toCharArray(next.meterTopic, sizeof(next.meterTopic));
    meterPath.toCharArray(next.meterPath, sizeof(next.meterPath));
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64 ||
        !validHost(host) || !validHost(soyoHost) ||
        !readInt("soyoChannel", next.soyoChannel, -1, 2) ||
        !readInt("soyoPolarity", next.soyoPolarity, -1, 1) || !next.soyoPolarity ||
        !readInt("channel", next.channel, -1, 2) || !readInt("polarity", next.polarity, -1, 1) ||
        !next.polarity || !readInt("reserve", next.reserve, 0, 200) ||
        !readInt("soyoCount", next.soyoCount, 0, 32) || !readInt("soyoUnit", next.soyoUnit, 0, 1200) ||
        !readInt("huaweiCount", next.huaweiCount, 0, 3) ||
        !readInt("volts10", next.volts10, 0, 580) || !readInt("amps10", next.amps10, 0, 750) ||
        !readInt("ampScale10", next.ampScale10, 0, 500) ||
        !readInt("model", next.model, 0, modelCount - 1) ||
        !readInt("fallbackMode", next.fallbackMode, 0, 1) ||
        !readInt("fallbackVolts10", next.fallbackVolts10, 0, 580) ||
        !readInt("fallbackAmps10", next.fallbackAmps10, 0, 750) ||
        (next.huaweiCount && next.fallbackMode == 1 &&
         (next.fallbackVolts10 < 450 || !next.fallbackAmps10 ||
          next.fallbackAmps10 > rectifierModels[next.model].ratedAmps * 10)) ||
        (next.soyoCount && !next.soyoUnit) ||
        (next.huaweiCount && (next.volts10 < 450 || !next.amps10 ||
                              next.amps10 > rectifierModels[next.model].ratedAmps * 10 ||
                              (next.model == 0 && next.ampScale10 < 100)))) {
        web.send(400, "text/plain; charset=utf-8", "Bitte WLAN, Gerätezahl und Leistungsgrenzen prüfen.");
        return;
    }
    ssid.toCharArray(next.ssid, sizeof(next.ssid));
    if (password.length() || ssid != cfg.ssid) password.toCharArray(next.password, sizeof(next.password));
    host.toCharArray(next.meter, sizeof(next.meter));
    soyoHost.toCharArray(next.soyoMeter, sizeof(next.soyoMeter));
    stop(Mode::Idle, false);  // bis zum Neustart aus, gespeicherter Modus bleibt erhalten
    if (prefs.putBytes("config1", &next, sizeof(next)) != sizeof(next)) {
        web.send(500, "text/plain; charset=utf-8", "Speichern fehlgeschlagen.");
        return;
    }
    web.send(200, "text/plain; charset=utf-8", "Gespeichert. Das CYD startet neu. Die neue IP steht unter Einstellungen.");
    rebootAt = millis() + 1200;
}

// ============================================================================
// Display-Oberflaeche mit LVGL 8.3
// ============================================================================

// Maximales Alter eines Shelly-Messwerts. Ein Abfragezyklus mit zwei
// fehlgeschlagenen Versuchen dauert bis ca. 3,2 s, daher etwas Reserve.
constexpr uint32_t meterMaxAge = 6000;

// MQTT-Quellen senden oft seltener (z. B. Tasmota alle 10 s), daher eigenes Zeitlimit.
uint32_t gridMaxAge() { return cfg.meterSource == 1 ? cfg.meterTimeout * 1000UL : meterMaxAge; }

bool meterOK() {
    return WiFi.status() == WL_CONNECTED && meter.valid && recent(meter.time, gridMaxAge());
}

bool soyoIsOff() {
    return !cfg.soyoMeter[0] ||
           (soyoMeter.valid && recent(soyoMeter.time, meterMaxAge) && abs(soyoMeter.watts) <= 20);
}

int maxCharge() {
    return min(2000, cfg.huaweiCount * cfg.volts10 * cfg.amps10 / 100);
}

// Farben
#define COL_BG      lv_color_hex(0x0d1520)
#define COL_HEADER  lv_color_hex(0x080d14)
#define COL_CARD    lv_color_hex(0x182433)
#define COL_CARD_HI lv_color_hex(0x22344a)
#define COL_TEXT    lv_color_hex(0xeef4f8)
#define COL_MUTED   lv_color_hex(0x8a9bb0)
#define COL_MINT    lv_color_hex(0x2dd4bf)
#define COL_SOYO    lv_color_hex(0xf59e0b)
#define COL_CHARGE  lv_color_hex(0x22c55e)
#define COL_IMPORT  lv_color_hex(0xfb923c)
#define COL_EXPORT  lv_color_hex(0x38bdf8)
#define COL_ERROR   lv_color_hex(0xef4444)
#define COL_STOP    lv_color_hex(0xb91c1c)
#define COL_START   lv_color_hex(0x15803d)

enum Action {
    ActZero, ActManual, ActIdle, ActStart, ActStop, ActCancel,
    ActMinus, ActPlus, ActReserveMinus, ActReservePlus, ActSlider
};

bool redraw = true;
uint32_t lastDraw;
const char *notice = "";
bool chargeOpen = false;

struct Hold { lv_obj_t *obj; lv_obj_t *bar; int action; uint32_t start; bool fired; } hold{};

struct Ui {
    lv_obj_t *tabs;
    lv_obj_t *modeBadge, *wifi, *can, *mqttLabel;
    lv_obj_t *soyoValue, *soyoTag, *soyoBar;
    lv_obj_t *huaweiValue, *huaweiTag, *huaweiBar;
    lv_obj_t *gridCaption, *gridValue, *gridStatus;
    lv_obj_t *netTitle, *netAddress, *netHint, *reserveValue;
    lv_obj_t *btnZero, *btnManual, *btnIdle, *barManual;
    lv_obj_t *charge, *chargeMax, *chargeSelect, *chargeValue, *chargeInfo, *btnMinus, *btnPlus;
    lv_obj_t *slider, *chargeNotice, *btnStart, *barStart;
} ui{};

// ---- kleine Helfer, die nur bei echter Aenderung neu zeichnen ----
void setText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void setTextColor(lv_obj_t *obj, lv_color_t color) {
    if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full != color.full) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
}

void setBg(lv_obj_t *obj, lv_color_t color) {
    if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full != color.full) {
        lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    }
}

void setVisible(lv_obj_t *obj, bool visible) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == visible) {
        if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void setBar(lv_obj_t *bar, int value, int maximum) {
    lv_bar_set_value(bar, maximum > 0 ? constrain(value * 100 / maximum, 0, 100) : 0, LV_ANIM_OFF);
}

// ---- Bausteine ----
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *makeCard(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

lv_obj_t *makeBar(lv_obj_t *parent, lv_color_t color, int w, int h) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0b121b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

void onEvent(lv_event_t *e);

lv_obj_t *makeButton(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                     lv_color_t color, int action, const lv_font_t *font = &lv_font_montserrat_20) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, onEvent, LV_EVENT_ALL, (void *)(intptr_t)action);
    lv_obj_t *label = makeLabel(btn, font, COL_TEXT, text);
    lv_obj_center(label);
    return btn;
}

// ---- Aktionen (gleiche Logik wie vorher bei der Touch-Auswertung) ----
void saveReserve(int delta) {
    Config next = cfg;
    next.reserve = constrain(cfg.reserve + delta, 0, 200);
    if (prefs.putBytes("config1", &next, sizeof(next)) == sizeof(next)) cfg.reserve = next.reserve;
}

void runAction(int action) {
    switch (action) {
        case ActZero: stop(Mode::Zero); break;
        case ActIdle: stop(Mode::Idle); break;
        case ActManual: stop(Mode::Manual); notice = ""; chargeOpen = true; break;
        case ActStart:
            if (!manual && meterOK() && huaweiHealthy() && soyoIsOff() && selection <= maxCharge()) {
                manual = selection;
                lastControl = millis() - 1000;
                notice = "";
            } else if (!manual) {
                if (selection > maxCharge()) notice = "Ladegrenze in den Einstellungen pruefen";
                else if (!soyoIsOff()) notice = "Auf Soyo-Stopp oder Messwert warten";
                else notice = "Shelly- oder CAN-Verbindung pruefen";
            }
            break;
        case ActStop: stop(Mode::Manual); notice = ""; break;
        case ActCancel:
            stop(Mode::Idle);
            chargeOpen = false;
            lv_tabview_set_act(ui.tabs, 2, LV_ANIM_OFF);
            break;
        case ActMinus: if (!manual) selection = max(100, selection - 100); break;
        case ActPlus: if (!manual) selection = min(2000, selection + 100); break;
        case ActReserveMinus: saveReserve(-5); break;
        case ActReservePlus: saveReserve(5); break;
    }
    redraw = true;
}

void onEvent(lv_event_t *e) {
    const int action = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if (action == ActManual || action == ActStart) {
        // Lang druecken: Fortschritt wird in uiTick() angezeigt und ausgeloest.
        if (code == LV_EVENT_PRESSED) {
            hold = {target, action == ActManual ? ui.barManual : ui.barStart, action, millis(), false};
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            if (hold.bar) lv_bar_set_value(hold.bar, 0, LV_ANIM_OFF);
            hold = {};
        }
        return;
    }
    if (action == ActStop || action == ActCancel) {
        // Stop und Abbrechen wirken sofort bei der ersten Beruehrung.
        if (code == LV_EVENT_PRESSED) runAction(action);
        return;
    }
    if (action == ActSlider) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            const int value = (lv_slider_get_value(target) + 50) / 100 * 100;
            lv_slider_set_value(target, value, LV_ANIM_OFF);
            if (!manual) selection = constrain(value, 100, 2000);
            redraw = true;
        }
        return;
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) runAction(action);
}

// Fortschritt der Halte-Tasten (Nur Laden 1,2 s, Start 3 s)
void uiTick() {
    if (!hold.obj || hold.fired) return;
    const uint32_t need = hold.action == ActManual ? 1200 : 3000;
    const uint32_t elapsed = millis() - hold.start;
    lv_bar_set_value(hold.bar, min<uint32_t>(100, elapsed * 100 / need), LV_ANIM_OFF);
    if (elapsed >= need) {
        hold.fired = true;
        lv_bar_set_value(hold.bar, 0, LV_ANIM_OFF);
        runAction(hold.action);
    }
}

// ---- Display- und Touch-Treiber ----
static lv_disp_draw_buf_t drawBuffer;
static lv_color_t pixels[320 * 24];

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    const uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(colors), w * h);
    lcd.endWrite();
    lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    lgfx::touch_point_t point;
    if (!rebootAt && lcd.getTouch(&point) > 0) {
        data->point.x = constrain(point.x, 0, 319);
        data->point.y = constrain(point.y, 0, 239);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ---- Touch kalibrieren (wie Simply Open Camper) ----
// LovyanGFX zeigt nacheinander Pfeile in den vier Ecken, die man antippt. Die 8 Werte
// landen im Flash ("tcal") und werden beim Start gesetzt.
// Ausloesen: beim Einschalten 2 s auf den Bildschirm druecken oder im Webinterface.
volatile bool calibrateRequested = false;

void touchCalibrationLoad() {
    uint16_t cal[8];
    if (prefs.getBytesLength("tcal") == sizeof(cal) && prefs.getBytes("tcal", cal, sizeof(cal)) == sizeof(cal)) {
        lcd.setTouchCalibrate(cal);
    }
}

void touchCalibrationRun() {
    uint16_t cal[8];
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextSize(2);
    lcd.drawString("Touch kalibrieren", 160, 106);
    lcd.setTextSize(1);
    lcd.drawString("Pfeilspitzen nacheinander antippen", 160, 132);
    lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 16);
    prefs.putBytes("tcal", cal, sizeof(cal));
    lcd.setTouchCalibrate(cal);
    lcd.fillScreen(TFT_BLACK);
    if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
}

// Beim Einschalten gedrueckt halten: nach 2 s loslassen = Touch kalibrieren
void touchCalibrationAtBoot() {
    lgfx::touch_point_t point;
    const uint32_t start = millis();
    bool shown = false;
    while (lcd.getTouch(&point) > 0 && millis() - start < 10000) {
        if (!shown && millis() - start >= 2000) {
            shown = true;
            lcd.fillScreen(TFT_BLACK);
            lcd.setTextColor(TFT_WHITE, TFT_BLACK);
            lcd.setTextDatum(textdatum_t::middle_center);
            lcd.setTextSize(2);
            lcd.drawString("Jetzt loslassen", 160, 120);
        }
        delay(20);
    }
    if (millis() - start >= 2000) touchCalibrationRun();
}

// ---- Seiten aufbauen ----
lv_obj_t *makeTab(const char *name) {
    lv_obj_t *tab = lv_tabview_add_tab(ui.tabs, name);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    return tab;
}

void buildHeader(lv_obj_t *screen) {
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 320, 26);
    lv_obj_set_style_bg_color(header, COL_HEADER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 10, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    ui.modeBadge = makeLabel(header, &lv_font_montserrat_14, COL_MUTED, "START");
    lv_obj_align(ui.modeBadge, LV_ALIGN_LEFT_MID, 0, 0);
    ui.wifi = makeLabel(header, &lv_font_montserrat_14, COL_MUTED, LV_SYMBOL_WIFI);
    lv_obj_align(ui.wifi, LV_ALIGN_RIGHT_MID, 0, 0);
    ui.can = makeLabel(header, &lv_font_montserrat_14, COL_MUTED, "CAN");
    lv_obj_align(ui.can, LV_ALIGN_RIGHT_MID, -34, 0);
    ui.mqttLabel = makeLabel(header, &lv_font_montserrat_14, COL_MUTED, "MQTT");
    lv_obj_align(ui.mqttLabel, LV_ALIGN_RIGHT_MID, -76, 0);
}

void buildValueCard(lv_obj_t *tab, int x, const char *caption, lv_color_t color,
                    lv_obj_t *&value, lv_obj_t *&tag, lv_obj_t *&bar) {
    lv_obj_t *card = makeCard(tab, x, 6, 148, 104);
    lv_obj_t *title = makeLabel(card, &lv_font_montserrat_14, color, caption);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    tag = makeLabel(card, &lv_font_montserrat_14, COL_MUTED, "");
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, 0, 0);
    value = makeLabel(card, &lv_font_montserrat_32, COL_TEXT, "-- W");
    lv_obj_align(value, LV_ALIGN_LEFT_MID, 0, 4);
    bar = makeBar(card, color, 132, 8);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void buildHome(lv_obj_t *tab) {
    buildValueCard(tab, 8, LV_SYMBOL_UPLOAD " Einspeisung", COL_SOYO, ui.soyoValue, ui.soyoTag, ui.soyoBar);
    buildValueCard(tab, 164, LV_SYMBOL_CHARGE " Ladung", COL_CHARGE, ui.huaweiValue, ui.huaweiTag, ui.huaweiBar);
    lv_obj_t *grid = makeCard(tab, 8, 116, 304, 52);
    ui.gridCaption = makeLabel(grid, &lv_font_montserrat_14, COL_MUTED, "Netz");
    lv_obj_align(ui.gridCaption, LV_ALIGN_TOP_LEFT, 0, -2);
    ui.gridValue = makeLabel(grid, &lv_font_montserrat_20, COL_TEXT, "--");
    lv_obj_align(ui.gridValue, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    ui.gridStatus = makeLabel(grid, &lv_font_montserrat_14, COL_MUTED, "");
    lv_obj_set_width(ui.gridStatus, 170);
    lv_obj_set_style_text_align(ui.gridStatus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(ui.gridStatus, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.gridStatus, LV_ALIGN_RIGHT_MID, 0, 0);
}

void buildSettings(lv_obj_t *tab) {
    lv_obj_t *net = makeCard(tab, 8, 6, 304, 76);
    ui.netTitle = makeLabel(net, &lv_font_montserrat_14, COL_MINT, "");
    lv_obj_align(ui.netTitle, LV_ALIGN_TOP_LEFT, 0, 0);
    ui.netAddress = makeLabel(net, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_align(ui.netAddress, LV_ALIGN_LEFT_MID, 0, 2);
    ui.netHint = makeLabel(net, &lv_font_montserrat_14, COL_MUTED, "");
    lv_obj_align(ui.netHint, LV_ALIGN_BOTTOM_LEFT, 0, 2);

    lv_obj_t *reserve = makeCard(tab, 8, 88, 304, 80);
    lv_obj_t *caption = makeLabel(reserve, &lv_font_montserrat_14, COL_MUTED, "Restbezug (Ziel am Netz)");
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, -2);
    lv_obj_t *minus = makeButton(reserve, 0, 20, 64, 44, LV_SYMBOL_MINUS, COL_CARD_HI, ActReserveMinus);
    lv_obj_t *plus = makeButton(reserve, 224, 20, 64, 44, LV_SYMBOL_PLUS, COL_CARD_HI, ActReservePlus);
    (void)minus; (void)plus;
    ui.reserveValue = makeLabel(reserve, &lv_font_montserrat_20, COL_TEXT, "");
    lv_obj_align(ui.reserveValue, LV_ALIGN_CENTER, 0, 10);
}

void buildModes(lv_obj_t *tab) {
    ui.btnZero = makeButton(tab, 8, 6, 304, 50, LV_SYMBOL_REFRESH "  Nulleinspeisung", COL_CARD, ActZero);
    ui.btnManual = makeButton(tab, 8, 62, 304, 52, LV_SYMBOL_CHARGE "  Nur Laden", COL_CARD, ActManual);
    lv_obj_t *hint = makeLabel(ui.btnManual, &lv_font_montserrat_14, COL_MUTED, "lang halten");
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 0);
    ui.barManual = makeBar(ui.btnManual, COL_MINT, 280, 4);
    lv_obj_align(ui.barManual, LV_ALIGN_BOTTOM_MID, 0, 6);
    ui.btnIdle = makeButton(tab, 8, 120, 304, 48, LV_SYMBOL_POWER "  Aus", COL_CARD, ActIdle);
}

void buildCharge(lv_obj_t *screen) {
    ui.charge = lv_obj_create(screen);
    lv_obj_set_pos(ui.charge, 0, 0);
    lv_obj_set_size(ui.charge, 320, 240);
    lv_obj_set_style_bg_color(ui.charge, COL_BG, 0);
    lv_obj_set_style_radius(ui.charge, 0, 0);
    lv_obj_set_style_border_width(ui.charge, 0, 0);
    lv_obj_set_style_pad_all(ui.charge, 0, 0);
    lv_obj_clear_flag(ui.charge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui.charge, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = makeLabel(ui.charge, &lv_font_montserrat_20, COL_CHARGE, LV_SYMBOL_CHARGE " Nur Laden");
    lv_obj_set_pos(title, 10, 6);
    ui.chargeMax = makeLabel(ui.charge, &lv_font_montserrat_14, COL_MUTED, "");
    lv_obj_set_pos(ui.chargeMax, 12, 30);
    makeButton(ui.charge, 192, 4, 120, 36, LV_SYMBOL_CLOSE " Abbrechen", COL_CARD_HI, ActCancel,
               &lv_font_montserrat_14);

    lv_obj_t *card = makeCard(ui.charge, 8, 48, 304, 96);
    ui.chargeSelect = lv_obj_create(card);   // Auswahl 100-2000 W
    lv_obj_remove_style_all(ui.chargeSelect);
    lv_obj_set_size(ui.chargeSelect, 288, 80);
    lv_obj_clear_flag(ui.chargeSelect, LV_OBJ_FLAG_SCROLLABLE);
    ui.btnMinus = makeButton(ui.chargeSelect, 0, 0, 56, 46, LV_SYMBOL_MINUS, COL_CARD_HI, ActMinus);
    ui.btnPlus = makeButton(ui.chargeSelect, 232, 0, 56, 46, LV_SYMBOL_PLUS, COL_CARD_HI, ActPlus);
    ui.slider = lv_slider_create(ui.chargeSelect);
    lv_obj_set_size(ui.slider, 260, 10);
    lv_obj_align(ui.slider, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(ui.slider, 100, 2000);
    lv_obj_set_style_bg_color(ui.slider, COL_CHARGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.slider, COL_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(ui.slider, onEvent, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)ActSlider);

    ui.chargeValue = makeLabel(card, &lv_font_montserrat_32, COL_TEXT, "");
    lv_obj_align(ui.chargeValue, LV_ALIGN_TOP_MID, 0, 4);
    ui.chargeInfo = makeLabel(card, &lv_font_montserrat_14, COL_MUTED, "");
    lv_obj_align(ui.chargeInfo, LV_ALIGN_BOTTOM_MID, 0, 0);

    ui.chargeNotice = makeLabel(ui.charge, &lv_font_montserrat_14, COL_SOYO, "");
    lv_obj_set_width(ui.chargeNotice, 304);
    lv_obj_set_style_text_align(ui.chargeNotice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.chargeNotice, 8, 148);

    ui.btnStart = makeButton(ui.charge, 8, 168, 148, 66, LV_SYMBOL_PLAY " START", COL_START, ActStart);
    lv_obj_t *startLabel = lv_obj_get_child(ui.btnStart, 0);
    lv_obj_align(startLabel, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *startHint = makeLabel(ui.btnStart, &lv_font_montserrat_14, COL_TEXT, "3 s halten");
    lv_obj_align(startHint, LV_ALIGN_CENTER, 0, 14);
    ui.barStart = makeBar(ui.btnStart, COL_TEXT, 124, 4);
    lv_obj_align(ui.barStart, LV_ALIGN_BOTTOM_MID, 0, 4);
    makeButton(ui.charge, 164, 168, 148, 66, LV_SYMBOL_STOP " STOP", COL_STOP, ActStop);
}

void uiInit() {
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, sizeof(pixels) / sizeof(pixels[0]));
    static lv_disp_drv_t displayDriver;
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = 320;
    displayDriver.ver_res = 240;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.draw_buf = &drawBuffer;
    lv_disp_t *display = lv_disp_drv_register(&displayDriver);
    static lv_indev_drv_t touchDriver;
    lv_indev_drv_init(&touchDriver);
    touchDriver.type = LV_INDEV_TYPE_POINTER;
    touchDriver.read_cb = readTouch;
    lv_indev_drv_register(&touchDriver);
    lv_disp_set_theme(display, lv_theme_default_init(display, COL_MINT, COL_SOYO, true,
                                                     &lv_font_montserrat_14));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COL_BG, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    buildHeader(screen);

    ui.tabs = lv_tabview_create(screen, LV_DIR_BOTTOM, 42);
    lv_obj_set_pos(ui.tabs, 0, 26);
    lv_obj_set_size(ui.tabs, 320, 214);
    lv_obj_set_style_bg_color(ui.tabs, COL_BG, 0);
    lv_obj_clear_flag(lv_tabview_get_content(ui.tabs), LV_OBJ_FLAG_SCROLLABLE);  // kein Wischen
    lv_obj_t *tabButtons = lv_tabview_get_tab_btns(ui.tabs);
    lv_obj_set_style_bg_color(tabButtons, COL_HEADER, 0);
    lv_obj_set_style_text_color(tabButtons, COL_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tabButtons, COL_MINT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(tabButtons, &lv_font_montserrat_14, 0);

    buildHome(makeTab(LV_SYMBOL_HOME " Start"));
    buildSettings(makeTab(LV_SYMBOL_SETTINGS " Settings"));
    buildModes(makeTab(LV_SYMBOL_LIST " Modi"));
    buildCharge(screen);
}

// ---- Werte aktualisieren ----
void uiUpdate() {
    char text[64];
    const bool shellyOK = meterOK();
    const bool huaweiOK = huaweiHealthy();

    // Kopfzeile
    if (rebootAt) {
        setText(ui.modeBadge, LV_SYMBOL_REFRESH "  NEUSTART ...");
        setTextColor(ui.modeBadge, COL_SOYO);
    } else if (mode == Mode::Zero) {
        setText(ui.modeBadge, shellyOK ? LV_SYMBOL_REFRESH "  NULLEINSPEISUNG" : LV_SYMBOL_WARNING "  WARTE AUF SHELLY");
        setTextColor(ui.modeBadge, shellyOK ? COL_MINT : COL_SOYO);
    } else if (mode == Mode::Manual) {
        setText(ui.modeBadge, manual ? LV_SYMBOL_CHARGE "  NUR LADEN AKTIV" : LV_SYMBOL_CHARGE "  NUR LADEN BEREIT");
        setTextColor(ui.modeBadge, COL_CHARGE);
    } else {
        setText(ui.modeBadge, LV_SYMBOL_POWER "  AUS");
        setTextColor(ui.modeBadge, COL_MUTED);
    }
    setTextColor(ui.wifi, WiFi.status() == WL_CONNECTED ? COL_MINT : portal ? COL_SOYO : COL_ERROR);
    setVisible(ui.can, cfg.huaweiCount > 0);
    setTextColor(ui.can, huaweiOK ? COL_MINT : COL_ERROR);
    setVisible(ui.mqttLabel, cfg.mqttHost[0] != 0);
    setTextColor(ui.mqttLabel, mqttOnline ? COL_MINT : COL_ERROR);

    // Soyo
    if (!cfg.soyoCount) {
        setText(ui.soyoValue, "--");
        setText(ui.soyoTag, "aus");
    } else if (cfg.soyoMeter[0]) {
        const bool valid = soyoMeter.valid && recent(soyoMeter.time, meterMaxAge);
        if (valid) snprintf(text, sizeof(text), "%d W", max(0, soyoMeter.watts));
        setText(ui.soyoValue, valid ? text : "-- W");
        setText(ui.soyoTag, "IST");
    } else {
        snprintf(text, sizeof(text), "%d W", dispatch.soyo / max(1, cfg.soyoCount) * cfg.soyoCount);
        setText(ui.soyoValue, text);
        setText(ui.soyoTag, "SOLL");
    }
    setBar(ui.soyoBar, dispatch.soyo, cfg.soyoCount * cfg.soyoUnit);

    // Huawei
    const float dc = cfg.huaweiCount ? huaweiPower(true) : NAN;
    if (isfinite(dc)) snprintf(text, sizeof(text), "%d W", int(lroundf(dc)));
    setText(ui.huaweiValue, isfinite(dc) ? text : "-- W");
    setText(ui.huaweiTag, !cfg.huaweiCount ? "aus" : huaweiOK ? "DC" : "CAN!");
    setTextColor(ui.huaweiTag, cfg.huaweiCount && !huaweiOK ? COL_ERROR : COL_MUTED);
    setBar(ui.huaweiBar, isfinite(dc) ? int(dc) : 0, maxCharge());

    // Netz
    if (shellyOK) {
        snprintf(text, sizeof(text), "%+d W", meter.watts);
        setText(ui.gridValue, text);
        setText(ui.gridCaption, meter.watts > 0 ? LV_SYMBOL_DOWNLOAD " Netz: Bezug"
                              : meter.watts < 0 ? LV_SYMBOL_UPLOAD " Netz: Einspeisung" : "Netz");
        setTextColor(ui.gridValue, meter.watts > 0 ? COL_IMPORT : meter.watts < 0 ? COL_EXPORT : COL_TEXT);
    } else {
        setText(ui.gridValue, "Shelly offline");
        setText(ui.gridCaption, LV_SYMBOL_WARNING " Netz");
        setTextColor(ui.gridValue, COL_ERROR);
    }
    if (notice[0]) {
        setText(ui.gridStatus, notice);
    } else {
        snprintf(text, sizeof(text), "Ziel %d W", cfg.reserve);
        setText(ui.gridStatus, text);
    }

    // Settings
    setText(ui.netTitle, portal ? LV_SYMBOL_WIFI "  Einrichtungs-WLAN" : LV_SYMBOL_WIFI "  Web-Oberflaeche");
    setText(ui.netAddress, (portal ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
    setText(ui.netHint, portal ? "Huawei-Soyo-Setup  /  PW: cydsetup" : "im Browser oeffnen");
    snprintf(text, sizeof(text), "%d W", cfg.reserve);
    setText(ui.reserveValue, text);

    // Modi
    setBg(ui.btnZero, mode == Mode::Zero ? lv_color_hex(0x0f766e) : COL_CARD);
    setBg(ui.btnManual, mode == Mode::Manual ? lv_color_hex(0x166534) : COL_CARD);
    setBg(ui.btnIdle, mode == Mode::Idle ? lv_color_hex(0x334155) : COL_CARD);

    // Nur Laden
    if (chargeOpen && mode != Mode::Manual) chargeOpen = false;  // z. B. nach Speichern im Web
    setVisible(ui.charge, chargeOpen);
    if (chargeOpen) {
        snprintf(text, sizeof(text), "max. %d W", maxCharge());
        setText(ui.chargeMax, text);
        setVisible(ui.chargeSelect, !manual);
        snprintf(text, sizeof(text), "%d W", manual ? manual : selection);
        setText(ui.chargeValue, text);
        setTextColor(ui.chargeValue, manual ? COL_CHARGE : COL_TEXT);
        if (lv_slider_get_value(ui.slider) != selection) lv_slider_set_value(ui.slider, selection, LV_ANIM_OFF);
        if (manual) {
            if (dispatch.huawei && isfinite(dc)) snprintf(text, sizeof(text), "laedt  -  Huawei DC %d W", int(lroundf(dc)));
            else snprintf(text, sizeof(text), dispatch.huawei ? "laedt ..." : "wartet auf Freigabe ...");
            setText(ui.chargeInfo, text);
        }
        setVisible(ui.chargeInfo, manual != 0);
        setText(ui.chargeNotice, notice);
        setBg(ui.btnStart, manual ? COL_CARD_HI : COL_START);
    }
}

// ---- Einrichtungs-WLAN (Captive Portal) ----
const IPAddress apIp(4, 3, 2, 1);
uint32_t portalSince = 0;

void toPortal() {
    web.sendHeader("Location", String("http://") + apIp.toString() + "/", true);
    web.send(302, "text/plain", "");
}

// ---- WLAN-Suche fuer das Webinterface ----
String scanJson = "[]";
bool scanned = false;

String jsonString(const String &s) {
    String out = "\"";
    for (unsigned i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (uint8_t(c) < 0x20) { char hex[8]; snprintf(hex, sizeof(hex), "\\u%04x", c); out += hex; }
        else out += c;
    }
    return out + "\"";
}

// Ergebnis uebernehmen: versteckte Netze weglassen, gleiche Namen nur einmal (staerkstes),
// nach Signalstaerke sortiert.
void collectScan(int count) {
    if (count < 0) return;
    std::vector<int> order;
    for (int i = 0; i < count; ++i) {
        if (!WiFi.SSID(i).length()) continue;
        bool duplicate = false;
        for (int j : order) {
            if (WiFi.SSID(j) == WiFi.SSID(i)) { duplicate = true; break; }
        }
        if (!duplicate) order.push_back(i);  // Liste kommt schon nach RSSI sortiert
    }
    String json = "[";
    for (size_t k = 0; k < order.size() && k < 25; ++k) {
        const int i = order[k];
        if (k) json += ',';
        json += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",\"rssi\":" + WiFi.RSSI(i) +
                ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    scanJson = json + "]";
    scanned = true;
    WiFi.scanDelete();
}

void handleScan() {
    const int state = WiFi.scanComplete();
    if (state >= 0) collectScan(state);
    if ((web.hasArg("refresh") || !scanned) && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        WiFi.scanNetworks(true);  // im Hintergrund, die Seite fragt jede Sekunde nach
    }
    const bool running = WiFi.scanComplete() == WIFI_SCAN_RUNNING;
    web.send(200, "application/json",
             String("{\"running\":") + (running ? "true" : "false") + ",\"nets\":" + scanJson + "}");
}

void setup() {
    // SoyoSource-RS485 haengt am UART-Stecker P1 neben der USB-Buchse (TX = GPIO1).
    // Deshalb laeuft Serial mit 4800 Baud und es gibt keine Debug-Ausgaben.
    Serial.begin(4800, SERIAL_8N1);
    prefs.begin("energy", false);
    const size_t stored = prefs.getBytesLength("config1");
    if (stored == sizeof(cfg)) {
        prefs.getBytes("config1", &cfg, sizeof(cfg));
    } else if (stored == offsetof(Config, meterSource)) {
        // Einstellungen ohne MQTT: Standard (Shelly, kein Broker) bleibt.
        prefs.getBytes("config1", &cfg, stored);
    } else if (stored == offsetof(Config, fallbackMode)) {
        // Einstellungen ohne Offline-Werte: Standard "Laden stoppen" bleibt.
        prefs.getBytes("config1", &cfg, stored);
    } else if (stored == offsetof(Config, model)) {
        // Alte Einstellungen ohne Modellauswahl: bisherigen Faktor weiterverwenden.
        prefs.getBytes("config1", &cfg, stored);
        cfg.model = 0;
    }

    for (int pin : {4, 16, 17}) {  // RGB-LED aus (Low = an)
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    lcd.init();
    lcd.setRotation(DISPLAY_ROTATION);
    lcd.setBrightness(255);
    touchCalibrationLoad();
    touchCalibrationAtBoot();
    uiInit();
    uiUpdate();
    lv_timer_handler();

    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_22, GPIO_NUM_27, TWAI_MODE_NORMAL);
    config.rx_queue_len = 64;
    config.tx_queue_len = 24;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_125KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    canReady = twai_driver_install(&config, &timing, &filter) == ESP_OK && twai_start() == ESP_OK;
    // War vor dem Neustart Nulleinspeisung aktiv, geht es damit weiter. Geregelt wird
    // aber erst, wenn gueltige Shelly-Daten (und ggf. Huawei-CAN) da sind, danach
    // folgen noch 5 s Wartezeit und ein sanftes Hochfahren ab 0 W.
    // "Nur Laden" wird nie automatisch fortgesetzt.
    autoZero = prefs.getBool("autoZero", false);
    stop(autoZero ? Mode::Zero : Mode::Idle, false);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    if (cfg.ssid[0]) WiFi.begin(cfg.ssid, cfg.password);
    const uint32_t started = millis();
    while (cfg.ssid[0] && WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
        if (millis() - lastTx >= 500) sendOutputs();
        receiveCan();
        if (millis() - lastDraw >= 250) { lastDraw = millis(); uiUpdate(); }
        lv_timer_handler();
        delay(20);
    }
    portal = WiFi.status() != WL_CONNECTED;
    if (portal) {
        // Einrichtungs-WLAN auf 4.3.2.1 (wie WLED / Simply Open Camper): Android zeigt die
        // Anmeldeseite nicht, wenn seine Pruefadresse auf eine private Adresse (192.168.x.x)
        // zeigt. Der DNS-Server leitet jede Adresse hierher, die Seite oeffnet sich von selbst.
        // Heim-WLAN nicht staendig suchen lassen, sonst wechselt der Funkkanal und das
        // Setup-WLAN bricht ab. Neuer Versuch per Neustart (siehe loop).
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);
        // Einmal WLANs suchen, bevor das Setup-WLAN startet: die Liste ist dann sofort da.
        collectScan(WiFi.scanNetworks());
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
        WiFi.softAP("Huawei-Soyo-Setup", "cydsetup");
        dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", apIp);
        portalSince = millis();
        lv_tabview_set_act(ui.tabs, 1, LV_ANIM_OFF);  // Settings mit Setup-WLAN zeigen
    }
    // Erkennungsadressen der Betriebssysteme (Android, Apple, Windows, Firefox) -> Einstellungsseite
    for (const char *url : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                            "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html",
                            "/success.txt"}) {
        web.on(url, HTTP_GET, toPortal);
    }
    web.on("/", HTTP_GET, [] {
        if (portal && web.hostHeader() != apIp.toString()) return toPortal();
        showWeb();
    });
    web.on("/save", HTTP_POST, saveWeb);
    web.on("/scan", HTTP_GET, handleScan);
    web.on("/calibrate", HTTP_POST, [] {
        calibrateRequested = true;
        web.send(200, "text/plain; charset=utf-8", "Am Display die Pfeilspitzen nacheinander antippen.");
    });
    web.onNotFound([] {
        if (portal) return toPortal();
        web.sendHeader("Location", "/");
        web.send(302);
    });
    web.begin();

    readings = xQueueCreate(1, sizeof(Sample));
    if (readings && cfg.meterSource == 0) xTaskCreate(pollMeter, "shelly", 8192, nullptr, 1, nullptr);
    if (readings && cfg.mqttHost[0] && !portal) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char id[24];
        snprintf(id, sizeof(id), "huaweisoyo_%02x%02x%02x", mac[3], mac[4], mac[5]);
        deviceId = id;
        xTaskCreate(mqttTask, "mqtt", 8192, nullptr, 1, nullptr);
    }
    if (cfg.soyoMeter[0]) {
        soyoReadings = xQueueCreate(1, sizeof(Sample));
        if (soyoReadings) xTaskCreate(pollMeter, "shellySoyo", 8192, &soyoMeter, 1, nullptr);
    }
}

void loop() {
    if (readings) xQueueReceive(readings, &meter, 0);
    if (soyoReadings) xQueueReceive(soyoReadings, &soyoMeter, 0);
    receiveCan();
    if (calibrateRequested) {
        calibrateRequested = false;
        touchCalibrationRun();
    }
    lv_timer_handler();  // liest Touch, loest Tasten-Aktionen aus und zeichnet
    uiTick();
    // Soyo braucht nur den Shelly. Der Huawei-CAN wird nur fuers Laden benoetigt,
    // damit eine CAN-Stoerung die Einspeisung nicht blockiert.
    const bool ready = !rebootAt && meterOK();
    const bool huaweiOK = huaweiHealthy();
    if (!ready && (manual || dispatch.soyo || dispatch.huawei)) {
        if (manual) notice = "Laden gestoppt: Shelly-Messung fehlt";
        stop(mode);
        redraw = true;
    }
    if (!huaweiOK && (manual || dispatch.huawei)) {
        if (manual) notice = "Laden gestoppt: Huawei-CAN fehlt";
        stop(mode);
        redraw = true;
    }
    const bool soyoOff = soyoIsOff();
    if (!soyoOff && dispatch.huawei) {
        stop(mode);
        notice = "Laden gestoppt: Soyo-Einspeisung";
        redraw = true;
    }
    if (millis() - lastControl >= 1000 &&
        (mode != Mode::Zero || meter.time != lastControlledSample)) {
        lastControl = millis();
        lastControlledSample = meter.time;
        const int previousSoyo = dispatch.soyo, previousHuawei = dispatch.huawei;
        // Soyo wird nur gesperrt, wenn der Huawei nachweislich noch Leistung aufnimmt
        // (> 40 W AC). Ohne Huawei-Daten (NAN) oder ohne Huawei darf Soyo laufen.
        const float huaweiAc = huaweiPower(false);
        const int maxSoyo = huaweiAc > 40 ? 0 : cfg.soyoCount * cfg.soyoUnit;
        dispatch.update(millis(), mode, ready, meter.watts, manual, maxSoyo,
                        soyoOff && huaweiOK ? maxCharge() : 0, cfg.reserve);
        if (previousSoyo != dispatch.soyo || previousHuawei != dispatch.huawei) sendOutputs();
    }
    if (millis() - lastTx >= 500) sendOutputs();
    if (redraw || millis() - lastDraw >= 250) {
        redraw = false;
        lastDraw = millis();
        uiUpdate();  // nur geaenderte Werte werden neu gezeichnet, kein Flackern
    }
    if (portal) dns.processNextRequest();
    web.handleClient();
    // Setup-WLAN aktiv, obwohl ein Heim-WLAN eingetragen ist (z. B. Router nach Stromausfall
    // langsamer als das CYD): nach 5 min ohne verbundenes Handy neu starten und erneut versuchen.
    if (portal && cfg.ssid[0] && !rebootAt && WiFi.softAPgetStationNum() == 0 &&
        millis() - portalSince > 5UL * 60 * 1000) {
        ESP.restart();
    }
    if (portal && WiFi.softAPgetStationNum() > 0) portalSince = millis();
    if (rebootAt && int32_t(millis() - rebootAt) >= 0) ESP.restart();
    delay(12);
}
