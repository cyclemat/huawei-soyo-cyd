// MQTT: Netzwert aus einem Abo (JSONPath) und Werte fuer Home Assistant (Discovery)
#include <PubSubClient.h>
#include <cJSON.h>
#include "app.h"

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
    json += ",\"dev\":{\"ids\":[\"" + deviceId + "\"],\"name\":\"" + String(T("Huawei Soyo Regler", "Huawei Soyo controller")) + "\",\"mf\":\"DIY\","
            "\"mdl\":\"CYD ESP32-2432S028R\"}}";
    mqtt.publish(("homeassistant/sensor/" + deviceId + "/" + s.key + "/config").c_str(), json.c_str(), true);
}

void publishDiscovery() {
    const Sensor common[] = {
        {"grid", T("Netzleistung", "Grid power"), "W", "power", nullptr},
        {"soyo_set", T("Soyo Sollwert", "Soyo setpoint"), "W", "power", "mdi:transmission-tower-import"},
        {"soyo_actual", T("Soyo Einspeisung gemessen", "Soyo feed-in measured"), "W", "power", "mdi:transmission-tower-import"},
        {"huawei_set", T("Huawei Sollwert", "Huawei setpoint"), "W", "power", "mdi:battery-charging"},
        {"huawei_dc", T("Huawei Ladeleistung", "Huawei charging power"), "W", "power", "mdi:battery-charging"},
        {"huawei_ac", T("Huawei Aufnahme", "Huawei input power"), "W", "power", nullptr},
        {"reserve", T("Restbezug Ziel", "Grid target"), "W", "power", "mdi:target"},
        {"manual", T("Nur Laden Leistung", "Charge-only power"), "W", "power", "mdi:battery-arrow-up"},
        {"mode", T("Modus", "Mode"), nullptr, nullptr, "mdi:state-machine"},
    };
    for (const auto &s : common) publishSensor(s);
    const char *suffix[] = {"dc", "ac", "v", "a", "vin", "temp", "eff"};
    const char *label[] = {T("Ladeleistung", "charging power"), T("Aufnahme", "input power"),
                           T("Ausgangsspannung", "output voltage"), T("Ladestrom", "charging current"),
                           T("Netzspannung", "mains voltage"), T("Temperatur", "temperature"),
                           T("Wirkungsgrad", "efficiency")};
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
    const bool gridValid = meter.valid && recent(meter.time, gridMaxAge());
    const bool soyoValid = cfg.soyoMeter[0] && soyoMeter.valid && recent(soyoMeter.time, meterMaxAge);
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


void mqttBegin() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[24];
    snprintf(id, sizeof(id), "huaweisoyo_%02x%02x%02x", mac[3], mac[4], mac[5]);
    deviceId = id;
    xTaskCreate(mqttTask, "mqtt", 8192, nullptr, 1, nullptr);
}
