// Huawei R48xx (CAN) + SoyoSource (RS485) Nulleinspeisung auf dem ESP32-2432S028R (Beta)
// Kern: Einstellungen, Messwerte, CAN, Soyo-Befehle, Shelly-Abfrage, Regelung
#include <Arduino.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <driver/twai.h>
#include <esp_log.h>
#include <lvgl.h>
#include "app.h"
#include "devsetup.h"
#include "sdlog.h"
#include "hwplug.h"
#include "diag.h"

const RectifierModel rectifierModels[] = {
    {"Eigener Faktor / custom factor", 75},
    {"R4830G (30 A)", 30},
    {"R4850G2 (50 A)", 50},
    {"R4875G1 (75 A)", 75},
};
const int modelCount = sizeof(rectifierModels) / sizeof(rectifierModels[0]);

float ampFactor(const Config &c) {
    if (c.model <= 0 || c.model >= modelCount) return c.ampScale10 / 10.0f;
    return 1024.0f / rectifierModels[c.model].ratedAmps;
}

int uiLang = 0;
Config cfg;
Dispatch dispatch;
Mode mode = Mode::Idle;
Preferences prefs;
WebServer web(80);
QueueHandle_t readings, soyoReadings;
bool portal, canReady;
uint32_t lastTx, lastControl, lastControlledSample, rebootAt, lastDraw, portalSince;
int manual = 0, selection = 100;
Sample meter{}, soyoMeter{};
Rectifier rectifier[3]{};
uint8_t canNode[3] = {1, 2, 3};

bool saveConfig(const Config &c) { return prefs.putBytes("config1", &c, sizeof(c)) == sizeof(c); }

String deviceSuffix() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char s[5];
    snprintf(s, sizeof(s), "%02X%02X", mac[4], mac[5]);
    return s;
}

bool recent(uint32_t stamp, uint32_t age) {
    return stamp && millis() - stamp <= age;
}

bool canSend(uint32_t id, const uint8_t *data) {
    twai_message_t msg{};
    msg.identifier = id;
    msg.extd = 1;
    msg.data_length_code = 8;
    memcpy(msg.data, data, 8);
    const bool ok = twai_transmit(&msg, pdMS_TO_TICKS(4)) == ESP_OK;
    if (!ok) canStats.txFail++;
    diagCanFrame(true, id, data, 8);
    return ok;
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
        const uint32_t node = uint32_t(canNode[i]) << 16;
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

// Fuer Stellen, die loop() laenger aufhalten (z. B. Log-Download): CAN lesen, Sollwerte wie gewohnt alle 500 ms senden
void receiveCan();
void serviceOutputs() {
    receiveCan();
    if (millis() - lastTx >= 500) sendOutputs();
}

// Merkt sich im Flash, ob nach einem Neustart wieder Nulleinspeisung aktiv sein soll.
// Geschrieben wird nur bei einer Aenderung, um den Flash zu schonen.
bool autoZero = false;

void stop(Mode next, bool remember) {
    if (next != mode) {
        const char *names[] = {T("Aus", "off"), T("Nulleinspeisung", "zero export"), T("Nur Laden", "charge only")};
        sdlogEvent((String(T("Modus: ", "Mode: ")) + names[int(next)]).c_str());
    }
    const bool zero = next == Mode::Zero;
    if (remember && zero != autoZero && prefs.putBool("autoZero", zero)) autoZero = zero;
    mode = next;
    manual = 0;
    dispatch.stop(millis());
    dispatch.neutralSince = millis();
    sendOutputs();
}

// CAN-Treiber (TWAI) starten: TX = IO22, RX = IO27 (Stecker CN1), 125 kbit/s
bool canInstall() {
    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_22, GPIO_NUM_27, TWAI_MODE_NORMAL);
    config.rx_queue_len = 64;
    config.tx_queue_len = 24;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_125KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&config, &timing, &filter) != ESP_OK) return false;
    if (twai_start() == ESP_OK) return true;
    twai_driver_uninstall();
    return false;
}

uint32_t canRetryAt = 0;

void receiveCan() {
    if (!cfg.huaweiCount) return;
    if (!canReady) {
        // Nach Bus-Off oder Startfehler alle 5 s einen sauberen Neustart des Treibers versuchen
        if (canRetryAt && int32_t(millis() - canRetryAt) >= 0) {
            canReady = canInstall();
            canRetryAt = canReady ? 0 : (millis() + 5000) | 1;
        }
        return;
    }
    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
        if (status.state == TWAI_STATE_BUS_OFF) {
            // Kein twai_initiate_recovery(): Es setzt den Sendezaehler auf 0, eine danach noch eintreffende
            // Sendemeldung macht ihn -1 und loest im ESP-IDF-Treiber ein assert aus (twai.c,
            // "tx_msg_count >= 0") -> Neustart-Schleife, z. B. ohne angeschlossenes CAN-Modul.
            // Stattdessen Treiber ganz entfernen und spaeter neu starten.
            twai_driver_uninstall();
            canReady = false;
            canStats.busOff++;
            sdlogEvent(T("CAN: Bus-Off, Treiber wird neu gestartet", "CAN: bus-off, restarting driver"));
            canRetryAt = (millis() + 5000) | 1;
            return;
        }
        if (status.state == TWAI_STATE_STOPPED) twai_start();
    }
    twai_message_t m{};
    for (int n = 0; n < 48 && twai_receive(&m, 0) == ESP_OK; ++n) {
        canStats.rx++;
        diagCanFrame(false, m.identifier, m.data, m.data_length_code);
        // Antwort eines Huawei: 0x1081407F fuer Adresse 1, 0x1082407F fuer Adresse 2 usw.
        const int node = (m.identifier >> 16) & 15;
        int index = -1;
        if (m.extd && !m.rtr && node > 0 && (m.identifier & 0xfff0ffff) == 0x1080407f) {
            for (int i = 0; i < cfg.huaweiCount; ++i)
                if (canNode[i] == node) index = i;
            // Ein einzelnes Geraet darf jede Adresse haben: die gemeldete wird uebernommen und fortan angesprochen
            if (index < 0 && cfg.huaweiCount == 1) {
                canNode[0] = node;
                index = 0;
                sdlogEvent((String(T("Huawei meldet sich mit CAN-Adresse ", "Huawei answers with CAN address ")) + node +
                            T(", wird übernommen", ", adopted")).c_str());
            }
        }
        if (index < 0) {   // fremde oder unpassende ID: nur zaehlen (Diagnoseseite zeigt die letzte)
            canStats.rxForeign++;
            canStats.lastForeignId = m.identifier;
            canStats.lastForeignAt = millis();
            continue;
        }
        canStats.rxUnit[index]++;
        if (m.data_length_code != 8 || m.data[0] != 1) continue;
        const float value = ((uint32_t(m.data[4]) << 24) | (uint32_t(m.data[5]) << 16) |
                             (uint32_t(m.data[6]) << 8) | m.data[7]) / 1024.0f;
        auto &r = rectifier[index];
        if (m.data[1] == 0x73) { r.dc = value; r.dcAt = millis(); }
        if (m.data[1] == 0x70) { r.ac = value; r.acAt = millis(); }
        if (m.data[1] == 0x7f || m.data[1] == 0x80) {
            // Temperaturen sind vorzeichenbehaftet (am R4850G2 gemessen: 0x7F = FF FF 8C 00 = -29,0 °C im Ruhezustand,
            // 0x80 = 00 00 3C 00 = 15,0 °C). 0x7F = Ausgang, 0x80 = Eingang/Zuluft. Angezeigt und fuer die
            // Uebertemperatur-Sperre benutzt wird der hoehere der beiden plausiblen Werte (-40 bis 150 °C).
            static float outTemp[3] = {NAN, NAN, NAN};
            const uint32_t raw = (uint32_t(m.data[4]) << 24) | (uint32_t(m.data[5]) << 16) | (uint32_t(m.data[6]) << 8) | m.data[7];
            float t = int32_t(raw) / 1024.0f;
            if (t < -40 || t > 150) t = NAN;
            if (m.data[1] == 0x7f) {
                r.tempRaw = raw;
                outTemp[index] = t;
            } else {
                const float o = outTemp[index];
                const float best = isnan(t) ? o : isnan(o) ? t : max(t, o);
                if (isnan(best)) { r.temp = NAN; r.tempAt = 0; }
                else { r.temp = best; r.tempAt = millis(); }
            }
        }
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
        // Verbunden = der Huawei antwortet. Die Temperatur sperrt nur, wenn wirklich eine gueltige Meldung
        // mit 85 °C oder mehr vorliegt; eine fehlende oder unlesbare Temperatur sperrt nicht.
        if (!recent(r.acAt, 3000)) return false;
        if (recent(r.tempAt, 3000) && r.temp >= 85) return false;
    }
    return true;
}

// Klartext, warum huaweiHealthy() false ist – fuer Ereignis und Diagnoseseite
String huaweiProblem() {
    if (!cfg.huaweiCount) return "";
    if (!canReady) return T("CAN-Treiber nicht gestartet (Bus-Off oder kein Transceiver)", "CAN driver not running (bus-off or no transceiver)");
    twai_status_info_t status{};
    if (twai_get_status_info(&status) != ESP_OK) return T("CAN-Status nicht lesbar", "CAN status not readable");
    if (status.state == TWAI_STATE_BUS_OFF) return T("CAN Bus-Off (Verkabelung, Abschlusswiderstand, Transceiver)", "CAN bus-off (wiring, termination, transceiver)");
    if (status.state != TWAI_STATE_RUNNING) return T("CAN-Treiber angehalten", "CAN driver stopped");
    for (int i = 0; i < cfg.huaweiCount; ++i) {
        const auto &r = rectifier[i];
        const String who = String("Huawei ") + (i + 1) + T(" (Adresse ", " (address ") + canNode[i] + "): ";
        if (!r.acAt) return who + T("hat noch nie geantwortet", "has never answered");
        if (!recent(r.acAt, 3000)) return who + T("keine Antwort seit ", "no answer for ") + String((millis() - r.acAt) / 1000.0f, 1) + " s";
        if (recent(r.tempAt, 3000) && r.temp >= 85) return who + T("zu heiß, ", "too hot, ") + String(r.temp, 0) + " °C";
    }
    return "";
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

// ---- Shelly-Abfrage ----
// Art des Shelly wird einmal erkannt und gemerkt; danach nur noch der kleine Zaehler-Baustein:
//   Gen2/3 mit "em" (Pro 3EM dreiphasig):        /rpc/EM.GetStatus?id=0
//   Gen2/3 mit "em1" (Pro EM, EM Gen3, Pro 3EM einphasig): /rpc/EM1.GetStatus?id=<Kanal>
//   Gen1 (EM, 3EM):                               /status
enum MeterKind : uint8_t { KindUnknown, KindEm, KindEm1, KindGen1 };

volatile uint8_t meterError[2] = {MeterNoHost, MeterNoHost};
volatile int meterHttpCode[2] = {0, 0};

const char *meterErrorText(int which) {
    switch (meterError[which]) {
        case MeterOk: return T("Shelly antwortet", "Shelly responds");
        case MeterNoWifi: return T("kein WLAN", "no Wi-Fi");
        case MeterNoHost: return T("kein Shelly eingetragen", "no Shelly configured");
        case MeterTimeout: return T("Shelly antwortet nicht", "Shelly does not respond");
        case MeterAuth: return T("Shelly verlangt ein Passwort", "Shelly requires a password");
        case MeterHttp: return T("Shelly meldet einen Fehler", "Shelly reports an error");
        default: return T("unbekanntes Shelly-Format", "unknown Shelly format");
    }
}

bool number(const cJSON *obj, const char *key, double &value) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    value = item->valuedouble;
    return true;
}

bool emWatts(const cJSON *em, int channel, double &watts) {
    if (!em || cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(em, "errors"))) return false;
    const char *phase[] = {"a_act_power", "b_act_power", "c_act_power"};
    return number(em, channel < 0 ? "total_act_power" : phase[channel], watts);
}

bool em1Watts(const cJSON *em1, double &watts) {
    if (!em1 || cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(em1, "errors"))) return false;
    return number(em1, "act_power", watts);
}

bool gen1Watts(const cJSON *root, int channel, double &watts) {
    const auto *list = cJSON_GetObjectItemCaseSensitive(root, "emeters");
    const int count = cJSON_GetArraySize(list);
    const int first = channel < 0 ? 0 : channel;
    const int end = channel < 0 && count == 3 ? 3 : first + 1;
    watts = 0;
    bool valid = list && end <= count;
    for (int i = first; valid && i < end; ++i) {
        const auto *item = cJSON_GetArrayItem(list, i);
        double power;
        valid = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item, "is_valid")) && number(item, "power", power);
        if (valid) watts += power;
    }
    return valid;
}

// kind == KindUnknown: kompletter Status (Shelly.GetStatus oder Gen1 /status), dabei Art erkennen
bool parseMeter(const String &body, int channel, MeterKind &kind, double &watts) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    bool valid = false;
    switch (kind) {
        case KindUnknown: {
            char key[12];
            snprintf(key, sizeof(key), "em1:%d", max(0, channel));
            const cJSON *em = cJSON_GetObjectItemCaseSensitive(root, "em:0");
            const cJSON *em1 = cJSON_GetObjectItemCaseSensitive(root, key);
            if (em) {
                if ((valid = emWatts(em, channel, watts))) kind = KindEm;
            } else if (em1) {
                if ((valid = em1Watts(em1, watts))) kind = KindEm1;
            } else if ((valid = gen1Watts(root, channel, watts))) {
                kind = KindGen1;
            }
            break;
        }
        case KindEm: valid = emWatts(root, channel, watts); break;
        case KindEm1: valid = em1Watts(root, watts); break;
        case KindGen1: valid = gen1Watts(root, channel, watts); break;
    }
    cJSON_Delete(root);
    return valid && fabs(watts) < 100000;
}

void pollMeter(void *argument) {
    const bool soyo = argument != nullptr;
    const int which = soyo ? 1 : 0;
    const char *host = soyo ? cfg.soyoMeter : cfg.meter;
    const int channel = soyo ? cfg.soyoChannel : cfg.channel;
    const int polarity = soyo ? cfg.soyoPolarity : cfg.polarity;
    const auto queue = soyo ? soyoReadings : readings;
    MeterKind kind = KindUnknown;
    int failures = 0;
    // Verbindung offen lassen (Keep-Alive): spart den Verbindungsaufbau bei jeder Abfrage
    WiFiClient client;
    HTTPClient http;
    http.setReuse(true);
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    for (;;) {
        const uint32_t started = millis();
        Sample sample{};
        uint8_t error = MeterOk;
        if (!host[0]) {
            error = MeterNoHost;
        } else if (WiFi.status() != WL_CONNECTED) {
            error = MeterNoWifi;
        } else {
            // Unbekannte Art: erst Gen2/3 (Shelly.GetStatus), dann Gen1 (/status) versuchen
            for (int attempt = 0; attempt < (kind == KindUnknown ? 2 : 1) && !sample.valid; ++attempt) {
                String path;
                if (kind == KindEm) path = "/rpc/EM.GetStatus?id=0";
                else if (kind == KindEm1) path = String("/rpc/EM1.GetStatus?id=") + max(0, channel);
                else if (kind == KindGen1 || attempt == 1) path = "/status";
                else path = "/rpc/Shelly.GetStatus";
                if (!http.begin(client, String("http://") + host + path)) {
                    error = MeterTimeout;
                    continue;
                }
                const int code = http.GET();
                meterHttpCode[which] = code;
                double watts;
                if (code == 200) {
                    MeterKind found = kind == KindUnknown && attempt == 1 ? KindUnknown : kind;
                    if (parseMeter(http.getString(), channel, found, watts)) {
                        kind = found;
                        sample = {int(lround(watts)) * polarity, millis(), true};
                        error = MeterOk;
                    } else {
                        error = MeterFormat;
                    }
                } else {
                    error = code == 401 ? MeterAuth : code < 0 ? MeterTimeout : MeterHttp;
                }
                http.end();
                if (!sample.valid) client.stop();  // nach einem Fehler neu verbinden
            }
        }
        meterError[which] = error;
        if (sample.valid) {
            failures = 0;
            // Nur gueltige Werte weitergeben: ein einzelner Aussetzer soll die Regelung
            // nicht abschalten. Veraltete Werte erkennt meterOK() ueber das Alter.
            xQueueOverwrite(queue, &sample);
        } else if (++failures >= 20 && kind != KindUnknown) {
            kind = KindUnknown;  // lange nichts: Art neu erkennen (z. B. anderer Shelly unter der Adresse)
            failures = 0;
        }
        const uint32_t used = millis() - started;
        vTaskDelay(pdMS_TO_TICKS(used < 800 ? 1000 - used : 200));
    }
}


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

void loadConfig() {
    const size_t stored = prefs.getBytesLength("config1");
    if (stored == sizeof(cfg)) {
        prefs.getBytes("config1", &cfg, sizeof(cfg));
    } else if (stored == offsetof(Config, controlTime10) || stored == offsetof(Config, hwShelly) || stored == offsetof(Config, ldrRise) ||
               stored == offsetof(Config, screenOffMin) || stored == offsetof(Config, lang) ||
               stored == offsetof(Config, meterSource) ||
               stored == offsetof(Config, fallbackMode)) {
        // Aeltere Einstellungen: neue Felder behalten ihre Standardwerte.
        prefs.getBytes("config1", &cfg, stored);
    } else if (stored == offsetof(Config, model)) {
        // Alte Einstellungen ohne Modellauswahl: bisherigen Faktor weiterverwenden.
        prefs.getBytes("config1", &cfg, stored);
        cfg.model = 0;
    }
    uiLang = cfg.lang == 1 ? 1 : 0;
}

void setup() {
    // SoyoSource-RS485 haengt am UART-Stecker P1 neben der USB-Buchse (TX = GPIO1).
    // Deshalb laeuft Serial mit 4800 Baud und es gibt keine Debug-Ausgaben.
    Serial.begin(4800, SERIAL_8N1);
    esp_log_level_set("*", ESP_LOG_NONE);   // auch ESP-IDF-Meldungen nicht auf den Soyo-Bus schreiben
    const devsetup::Boot boot = devsetup::bootCheck();   // Mehrfach-Reset zaehlen
    prefs.begin("energy", false);
    loadConfig();
    diagBegin();   // Ereignis-Ring fuer die Diagnoseseite, vor dem ersten sdlogEvent

    displayBegin();
    uiInit();
    uiUpdate();
    lv_timer_handler();

    // Log auf der SD-Karte (eigener Task, nur wenn eingeschaltet und eine Karte steckt)
    sdlogBegin();
    {
        const esp_reset_reason_t r = esp_reset_reason();
        const char *why = r == ESP_RST_POWERON ? T("Einschalten", "power on")
                        : r == ESP_RST_EXT ? T("Reset-Taste", "reset button")
                        : r == ESP_RST_SW ? T("Neustart durch Firmware", "restart by firmware")
                        : r == ESP_RST_PANIC ? T("Absturz", "crash")
                        : r == ESP_RST_BROWNOUT ? T("Unterspannung", "brownout")
                        : (r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT) ? "Watchdog"
                        : T("anderer Grund", "other reason");
        sdlogEvent((String(T("Start v", "Start v")) + FW_VERSION + " – " + why).c_str());
    }

    if (cfg.huaweiCount) {
        splashProgress(20, T("Verbinde mit Huawei...", "Connecting to Huawei..."));
        canReady = canInstall();
        if (!canReady) canRetryAt = (millis() + 5000) | 1;
    }
    // War vor dem Neustart Nulleinspeisung aktiv, geht es damit weiter. Geregelt wird
    // aber erst, wenn gueltige Messwerte da sind, danach folgen noch 5 s Wartezeit und ein
    // sanftes Hochfahren ab 0 W. "Nur Laden" wird nie automatisch fortgesetzt.
    autoZero = prefs.getBool("autoZero", false);
    stop(autoZero ? Mode::Zero : Mode::Idle, false);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // kein WLAN-Energiesparen: Antworten kommen sonst verspaetet oder gar nicht
    if (cfg.ssid[0]) WiFi.begin(cfg.ssid, cfg.password);
    splashProgress(35, cfg.ssid[0] ? T("Verbinde WLAN...", "Connecting Wi-Fi...") : T("Kein WLAN eingerichtet", "No Wi-Fi set up"));
    const uint32_t started = millis();
    while (cfg.ssid[0] && WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
        if (millis() - lastTx >= 500) sendOutputs();
        receiveCan();
        if (millis() - lastDraw >= 250) {
            lastDraw = millis();
            uiUpdate();
            splashProgress(35 + int((millis() - started) * 40 / 12000), nullptr);   // Balken waechst mit der Wartezeit
        }
        lv_timer_handler();
        delay(20);
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    portal = !connected || boot.forceAp;
    if (portal) {
        splashProgress(80, (String(T("Einrichtungs-WLAN ", "Setup Wi-Fi ")) + "HuaweiSoyo-" + deviceSuffix()).c_str());
        if (!connected) {
            // Heim-WLAN nicht staendig suchen lassen, sonst wechselt der Funkkanal und das
            // Setup-WLAN bricht ab. Neuer Versuch per Neustart (siehe loop).
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false);
        }
        // Einmal WLANs suchen, bevor das Setup-WLAN startet: die Liste ist dann sofort da.
        collectScan(WiFi.scanNetworks());
        WiFi.mode(WIFI_AP_STA);
        devsetup::apStart("HuaweiSoyo-" + deviceSuffix());
        portalSince = millis();
        uiShowSettings();  // Einstellungen mit Setup-WLAN zeigen
    }
    if (connected) splashProgress(80, (String(T("WLAN verbunden, ", "Wi-Fi connected, ")) + WiFi.localIP().toString()).c_str());
    webBegin();
    // Uhrzeit fuer den Log (Mitteleuropa mit Sommerzeit)
    if (connected) configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");

    readings = xQueueCreate(1, sizeof(Sample));
    if (readings && cfg.meterSource == 0) xTaskCreate(pollMeter, "shelly", 8192, nullptr, 1, nullptr);
    if (readings && cfg.mqttHost[0] && connected) mqttBegin();
    if (cfg.soyoMeter[0]) {
        soyoReadings = xQueueCreate(1, sizeof(Sample));
        if (soyoReadings) xTaskCreate(pollMeter, "shellySoyo", 8192, &soyoMeter, 1, nullptr);
    }
    hwPlugBegin();   // Shelly vor den Huawei, nur wenn eingetragen
    splashProgress(100, T("Bereit", "Ready"));
    splashEnd();
}

// Zustandswechsel als Ereignis in den SD-Log schreiben
void logTransitions(bool meterLive, bool huaweiOK) {
    static int lastMeter = -1, lastCan = -1, lastWifi = -1;
    static const char *lastNotice = "";
    if (notice != lastNotice) {   // Hinweise wie "Laden gestoppt: ..." ebenfalls protokollieren
        if (notice[0]) sdlogEvent(notice);
        lastNotice = notice;
    }
    const int wifi = WiFi.status() == WL_CONNECTED;
    if (int(meterLive) != lastMeter) {
        if (lastMeter >= 0 || meterLive) {
            sdlogEvent(meterLive ? T("Netzmessung da", "grid reading available")
                       : (String(T("Netzmessung fehlt: ", "grid reading missing: ")) +
                          (cfg.meterSource == 0 ? meterErrorText(0) : "MQTT")).c_str());
        }
        lastMeter = meterLive;
    }
    if (cfg.huaweiCount && int(huaweiOK) != lastCan) {
        if (lastCan >= 0 || huaweiOK) {
            sdlogEvent(huaweiOK ? T("Huawei-CAN da", "Huawei CAN available")
                                : (String(T("Huawei-CAN fehlt: ", "Huawei CAN missing: ")) + huaweiProblem()).c_str());
        }
        lastCan = huaweiOK;
    }
    if (wifi != lastWifi) {
        if (lastWifi >= 0 || wifi) {
            sdlogEvent(wifi ? (String(T("WLAN verbunden, ", "Wi-Fi connected, ")) + WiFi.localIP().toString() + ", " +
                               WiFi.RSSI() + " dBm").c_str()
                            : T("WLAN getrennt", "Wi-Fi disconnected"));
        }
        lastWifi = wifi;
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
    // Soyo braucht nur die Netzmessung. Der Huawei-CAN wird nur fuers Laden benoetigt,
    // damit eine CAN-Stoerung die Einspeisung nicht blockiert.
    const bool ready = !rebootAt && meterOK();
    const bool huaweiOK = huaweiHealthy();
    logTransitions(ready, huaweiOK);
    if (!ready && (manual || dispatch.soyo || dispatch.huawei)) {
        if (manual) notice = T("Laden gestoppt: Netzmessung fehlt", "Charging stopped: no grid reading");
        stop(mode);
        redraw = true;
    }
    if (!huaweiOK && (manual || dispatch.huawei)) {
        if (manual) notice = T("Laden gestoppt: Huawei-CAN fehlt", "Charging stopped: no Huawei CAN");
        stop(mode);
        redraw = true;
    }
    const bool soyoOff = soyoIsOff();
    if (!soyoOff && dispatch.huawei) {
        stop(mode);
        notice = T("Laden gestoppt: Soyo speist ein", "Charging stopped: Soyo is feeding in");
        redraw = true;
    }
    if (millis() - lastControl >= uint32_t(constrain(cfg.controlTime10, 5, 600)) * 100 &&
        (mode != Mode::Zero || meter.time != lastControlledSample)) {
        lastControl = millis();
        lastControlledSample = meter.time;
        const int previousSoyo = dispatch.soyo, previousHuawei = dispatch.huawei;
        // Soyo wird nur gesperrt, wenn der Huawei nachweislich noch Leistung aufnimmt
        // (> 40 W AC laut CAN oder laut Shelly vor dem Huawei). Ohne Huawei-Daten (NAN) oder
        // ohne Huawei darf Soyo laufen.
        const float huaweiAc = huaweiPower(false);
        const int maxSoyo = huaweiAc > 40 || hwPlugDrawing() ? 0 : cfg.soyoCount * cfg.soyoUnit;
        dispatch.update(millis(), mode, ready, meter.watts, manual, maxSoyo,
                        soyoOff && huaweiOK ? maxCharge() : 0, cfg.reserve);
        if (previousSoyo != dispatch.soyo || previousHuawei != dispatch.huawei) sendOutputs();
    }
    if (millis() - lastTx >= 500) sendOutputs();
    hwPlugLoop();
    if (redraw || millis() - lastDraw >= 250) {
        redraw = false;
        lastDraw = millis();
        uiUpdate();  // nur geaenderte Werte werden neu gezeichnet, kein Flackern
    }
    devsetup::loop();
    web.handleClient();
    // Setup-WLAN aktiv, weil das Heim-WLAN fehlte (z. B. Router nach Stromausfall langsamer
    // als das CYD): nach 5 min ohne verbundenes Handy neu starten und erneut versuchen.
    if (portal && WiFi.softAPgetStationNum() > 0) portalSince = millis();
    if (portal && cfg.ssid[0] && !rebootAt && WiFi.status() != WL_CONNECTED &&
        millis() - portalSince > 5UL * 60 * 1000) {
        ESP.restart();
    }
    if (rebootAt && int32_t(millis() - rebootAt) >= 0) ESP.restart();
    delay(12);
}
