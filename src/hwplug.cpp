// Shelly-Schalter vor den Huawei-Netzteilen – siehe hwplug.h
#include "hwplug.h"
#include <HTTPClient.h>
#include <cJSON.h>
#include "app.h"

HwPlug hwPlug[3];

namespace {

constexpr uint32_t START_GRACE = 60000;   // so lange braucht ein Huawei nach dem Einschalten bis zum CAN
constexpr uint32_t CAN_LOST = 15000;      // ohne CAN-Daten so lange -> abschalten
constexpr uint32_t OVER_TIME = 30000;     // Aufnahme ueber OVER_WATTS so lange, obwohl aus -> abschalten
constexpr int OVER_WATTS = 100;
constexpr uint32_t RETRY = 5UL * 60 * 1000;
constexpr uint32_t MAX_AGE = 10000;

// Art des Shelly: Gen2/3 (Switch-Baustein) oder Gen1 (relays/meters), wird einmal erkannt
enum PlugKind : uint8_t { PlugUnknown, PlugGen2, PlugGen1 };
PlugKind kind[3];

bool number(const cJSON *obj, const char *key, double &value) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    value = item->valuedouble;
    return true;
}

// Gen2/3: /rpc/Switch.GetStatus?id=0 -> {"output":true,"apower":12.3}
bool parseGen2(const String &body, bool &on, double &watts) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    const auto *output = cJSON_GetObjectItemCaseSensitive(root, "output");
    bool ok = cJSON_IsBool(output);
    if (ok) on = cJSON_IsTrue(output);
    if (ok && !number(root, "apower", watts)) watts = 0;   // Schalter ohne Messung
    cJSON_Delete(root);
    return ok;
}

// Gen1: /status -> "relays":[{"ison":true}], "meters":[{"power":12.3}]
bool parseGen1(const String &body, bool &on, double &watts) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    const auto *relay = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "relays"), 0);
    const auto *ison = cJSON_GetObjectItemCaseSensitive(relay, "ison");
    bool ok = cJSON_IsBool(ison);
    if (ok) on = cJSON_IsTrue(ison);
    const auto *meter = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "meters"), 0);
    if (ok && !number(meter, "power", watts)) watts = 0;
    cJSON_Delete(root);
    return ok;
}

int request(HTTPClient &http, WiFiClient &client, int i, const String &path, String *body) {
    if (!http.begin(client, String("http://") + cfg.hwShelly[i] + path)) return -1;
    const int code = http.GET();
    if (code == 200 && body) *body = http.getString();
    http.end();
    return code;
}

void poll(HTTPClient &http, WiFiClient &client, int i) {
    HwPlug &p = hwPlug[i];
    if (WiFi.status() != WL_CONNECTED) {
        p.error = MeterNoWifi;
        return;
    }
    uint8_t error = MeterTimeout;
    for (int attempt = 0; attempt < (kind[i] == PlugUnknown ? 2 : 1); ++attempt) {
        const bool gen1 = kind[i] == PlugGen1 || attempt == 1;
        String body;
        const int code = request(http, client, i, gen1 ? "/status" : "/rpc/Switch.GetStatus?id=0", &body);
        if (code != 200) {
            error = code == 401 ? MeterAuth : code < 0 ? MeterTimeout : MeterHttp;
            continue;
        }
        bool on = false;
        double watts = 0;
        if (!(gen1 ? parseGen1(body, on, watts) : parseGen2(body, on, watts))) {
            error = MeterFormat;
            continue;
        }
        kind[i] = gen1 ? PlugGen1 : PlugGen2;
        const uint32_t now = millis();
        if (on && (!p.valid || !p.on)) p.onSince = now;   // gerade eingeschaltet: Huawei startet
        p.on = on;
        p.watts = int(lround(watts));
        p.time = now;
        p.valid = true;
        p.error = MeterOk;
        return;
    }
    p.error = error;
}

void task(void *) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(2000);
    for (;;) {
        for (int i = 0; i < 3; ++i) {
            if (!hwPlugUsed(i)) continue;
            HwPlug &p = hwPlug[i];
            poll(http, client, i);
            // Schalter auf den gewuenschten Zustand bringen
            if (p.error == MeterOk && p.on != p.want && kind[i] != PlugUnknown) {
                const bool on = p.want;
                const String path = kind[i] == PlugGen1 ? String("/relay/0?turn=") + (on ? "on" : "off")
                                                        : String("/rpc/Switch.Set?id=0&on=") + (on ? "true" : "false");
                if (request(http, client, i, path, nullptr) == 200) poll(http, client, i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

const char *cutNotice(int i, int reason) {
    static const char *const de[2][3] = {
        {"Huawei 1 per Shelly aus: CAN fehlt", "Huawei 2 per Shelly aus: CAN fehlt", "Huawei 3 per Shelly aus: CAN fehlt"},
        {"Huawei 1 per Shelly aus: zieht Strom", "Huawei 2 per Shelly aus: zieht Strom", "Huawei 3 per Shelly aus: zieht Strom"}};
    static const char *const en[2][3] = {
        {"Huawei 1 off via Shelly: no CAN", "Huawei 2 off via Shelly: no CAN", "Huawei 3 off via Shelly: no CAN"},
        {"Huawei 1 off via Shelly: drawing power", "Huawei 2 off via Shelly: drawing power",
         "Huawei 3 off via Shelly: drawing power"}};
    return T(de[reason - 1][i], en[reason - 1][i]);
}

}  // namespace

bool hwPlugUsed(int i) {
    return i >= 0 && i < cfg.huaweiCount && i < 3 && cfg.hwShelly[i][0];
}

void hwPlugBegin() {
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        hwPlug[i] = HwPlug();
        hwPlug[i].want = true;    // nach jedem Neustart einschalten (der Huawei braucht Strom fuer den CAN)
        hwPlug[i].error = MeterNoHost;
        any |= hwPlugUsed(i);
    }
    if (any) xTaskCreate(task, "hwplug", 8192, nullptr, 1, nullptr);
}

void hwPlugLoop() {
    static uint32_t lastRun = 0, overSince[3] = {0, 0, 0};
    const uint32_t now = millis();
    if (now - lastRun < 500) return;
    lastRun = now;
    for (int i = 0; i < 3; ++i) {
        HwPlug &p = hwPlug[i];
        if (!hwPlugUsed(i)) continue;
        if (p.cutAt) {
            if (now - p.cutAt >= RETRY) {   // neuer Versuch: einschalten, CAN wieder abwarten
                p.cutAt = 0;
                p.cutReason = 0;
                p.want = true;
                overSince[i] = 0;
            }
            continue;
        }
        if (!cfg.hwShellyCut) {
            p.want = true;
            continue;
        }
        if (!p.valid || !p.on || !recent(p.time, MAX_AGE) || now - p.onSince < START_GRACE) {
            overSince[i] = 0;
            continue;
        }
        const bool canLost = !recent(rectifier[i].acAt, CAN_LOST);
        if (dispatch.huawei == 0 && !manual && p.watts > OVER_WATTS) {
            if (!overSince[i]) overSince[i] = now | 1;
        } else {
            overSince[i] = 0;
        }
        const bool over = overSince[i] && now - overSince[i] >= OVER_TIME;
        if (canLost || over) {
            p.want = false;
            p.cutAt = now | 1;
            p.cutReason = canLost ? 1 : 2;
            notice = cutNotice(i, p.cutReason);   // erscheint am Display und im SD-Log
            redraw = true;
        }
    }
}

bool hwPlugDrawing() {
    for (int i = 0; i < 3; ++i) {
        const HwPlug &p = hwPlug[i];
        if (hwPlugUsed(i) && p.valid && p.on && recent(p.time, MAX_AGE) && p.watts > 40) return true;
    }
    return false;
}

String hwPlugText(int i) {
    if (!hwPlugUsed(i)) return "";
    const HwPlug &p = hwPlug[i];
    if (p.cutAt) return p.cutReason == 1 ? T("Shelly aus: CAN fehlt", "Shelly off: no CAN")
                                         : T("Shelly aus: zog Strom", "Shelly off: drew power");
    if (!p.valid || !recent(p.time, MAX_AGE)) {
        switch (p.error) {
            case MeterNoWifi: return T("Shelly: kein WLAN", "Shelly: no Wi-Fi");
            case MeterAuth: return T("Shelly: Passwort", "Shelly: password");
            case MeterFormat: return T("Shelly: kein Schalter", "Shelly: no switch");
            default: return T("Shelly antwortet nicht", "Shelly not responding");
        }
    }
    if (!p.on) return T("Shelly aus", "Shelly off");
    return String(T("Shelly an · ", "Shelly on · ")) + p.watts + "\xE2\x80\xAFW";
}
