// Diagnose ueber das Netz – siehe diag.h
#include "diag.h"
#include <driver/twai.h>
#include <memory>
#include <time.h>
#include "app.h"
#include "hwplug.h"

CanStats canStats{};

namespace {
constexpr size_t RING = 1536;        // Ereignistext im RAM (aelteste Zeilen fallen raus)
constexpr int CAP_MAX = 64;          // Frames je Mitschnitt
constexpr uint32_t CAP_MS = 10000;   // Mitschnitt hoechstens so lange

char ring[RING];
size_t ringLen = 0;
SemaphoreHandle_t lock;

struct Frame { uint32_t ms, id; uint8_t tx, dlc, data[8]; };
std::unique_ptr<Frame[]> cap;        // nur waehrend/nach einem Mitschnitt belegt
int capCount = 0;
uint32_t capStart = 0;
bool capActive = false;

String stamp() {
    const time_t now = time(nullptr);
    char s[24];
    if (now > 1600000000) {
        struct tm t;
        localtime_r(&now, &t);
        strftime(s, sizeof(s), "%d.%m. %H:%M:%S", &t);
    } else {
        snprintf(s, sizeof(s), "%lu s", static_cast<unsigned long>(millis() / 1000));
    }
    return s;
}

bool take() { return lock && xSemaphoreTake(lock, pdMS_TO_TICKS(50)) == pdTRUE; }

String frameLine(const Frame &f) {
    char s[48];
    snprintf(s, sizeof(s), "%5lu ms  %s  %08lX  ", static_cast<unsigned long>(f.ms), f.tx ? "TX" : "RX",
             static_cast<unsigned long>(f.id));
    String line = s;
    for (int k = 0; k < f.dlc; ++k) {
        snprintf(s, sizeof(s), "%s%02X", k ? " " : "", f.data[k]);
        line += s;
    }
    return line;
}

String age(uint32_t at) {
    if (!at) return T("nie", "never");
    return String((millis() - at) / 1000.0f, 1) + " s";
}
}  // namespace

void diagBegin() {
    if (!lock) lock = xSemaphoreCreateMutex();
}

void diagEvent(const char *text) {
    const String line = stamp() + "  " + text + "\n";
    if (line.length() >= RING || !take()) return;
    while (ringLen + line.length() > RING) {   // aelteste Zeile verwerfen
        const char *nl = static_cast<const char *>(memchr(ring, '\n', ringLen));
        const size_t drop = nl ? size_t(nl - ring) + 1 : ringLen;
        memmove(ring, ring + drop, ringLen - drop);
        ringLen -= drop;
    }
    memcpy(ring + ringLen, line.c_str(), line.length());
    ringLen += line.length();
    xSemaphoreGive(lock);
}

String diagEvents() {
    String out;
    if (!take()) return out;
    out.reserve(ringLen + 1);
    out.concat(ring, ringLen);
    xSemaphoreGive(lock);
    return out;
}

bool diagCaptureStart() {
    if (!cap) cap.reset(new (std::nothrow) Frame[CAP_MAX]);
    if (!cap) return false;
    capCount = 0;
    capStart = millis();
    capActive = true;
    return true;
}

bool diagCaptureActive() {
    if (capActive && (capCount >= CAP_MAX || millis() - capStart >= CAP_MS)) capActive = false;
    return capActive;
}

void diagCanFrame(bool tx, uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (!diagCaptureActive()) return;
    Frame &f = cap[capCount++];
    f.ms = millis() - capStart;
    f.id = id;
    f.tx = tx;
    f.dlc = dlc > 8 ? 8 : dlc;
    memset(f.data, 0, sizeof(f.data));
    memcpy(f.data, data, f.dlc);
}

String diagCaptureJson() {
    String j = "[";
    for (int i = 0; cap && i < capCount; ++i) {
        const Frame &f = cap[i];
        char s[40];
        snprintf(s, sizeof(s), "%s[%lu,\"%s\",\"%08lX\",\"", i ? "," : "", static_cast<unsigned long>(f.ms),
                 f.tx ? "TX" : "RX", static_cast<unsigned long>(f.id));
        j += s;
        for (int k = 0; k < f.dlc; ++k) {
            snprintf(s, sizeof(s), "%s%02X", k ? " " : "", f.data[k]);
            j += s;
        }
        j += "\"]";
    }
    return j + "]";
}

String diagCaptureText() {
    String t;
    for (int i = 0; cap && i < capCount; ++i) t += frameLine(cap[i]) + "\n";
    return t;
}

// Vollstaendiger Textbericht: Geraet, Einstellungen (ohne Passwoerter), CAN, Huawei-Werte, Ereignisse, Mitschnitt
String diagReport() {
    String r;
    r.reserve(3000);
    r += String("Huawei-Soyo CYD Diagnose / diagnostics  v" FW_VERSION "  ") + stamp() + "\n";
    r += String("Laufzeit/uptime: ") + millis() / 1000 + " s, Heap frei/free: " + ESP.getFreeHeap() / 1024 + " KB (min " +
         ESP.getMinFreeHeap() / 1024 + " KB), WLAN: " +
         (WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm, " + WiFi.localIP().toString()
                                        : String("getrennt/disconnected")) +
         ", Portal: " + (portal ? "ja/yes" : "nein/no") + "\n";
    const char *modes[] = {"Aus/off", "Nulleinspeisung/zero", "Nur Laden/charge"};
    r += String("Modus/mode: ") + modes[int(mode)] + ", Soll Soyo " + dispatch.soyo + " W, Soll Huawei " + dispatch.huawei +
         " W, Netz/grid " + (meterOK() ? String(meter.watts) + " W" : String("kein Wert/no value")) + ", Restbezug/target " +
         cfg.reserve + " W\n";
    r += String("Einstellungen/settings: Soyo ") + cfg.soyoCount + " x " + cfg.soyoUnit + " W, Huawei " + cfg.huaweiCount +
         " x " + rectifierModels[cfg.model].name + ", " + String(cfg.volts10 / 10.0f, 1) + " V, " +
         String(cfg.amps10 / 10.0f, 1) + " A, Faktor/factor " + String(ampFactor(cfg), 2) + ", ohne CAN/without CAN: " +
         (cfg.fallbackMode == 1 ? String(cfg.fallbackVolts10 / 10.0f, 1) + " V / " + String(cfg.fallbackAmps10 / 10.0f, 1) + " A"
                                : String("0 A (stoppen/stop)")) +
         ", Netzmessung/grid meter: " + (cfg.meterSource == 0 ? "Shelly" : "MQTT") + ", Shelly vor Huawei/in front: " +
         (hwPlugUsed(0) || hwPlugUsed(1) || hwPlugUsed(2) ? "ja/yes" : "nein/no") + "\n\n";

    r += "== CAN ==\n";
    if (!cfg.huaweiCount) {
        r += "kein Huawei eingetragen / no Huawei configured\n";
    } else {
        twai_status_info_t s{};
        const bool st = canReady && twai_get_status_info(&s) == ESP_OK;
        const char *state = !canReady ? "Treiber aus/driver down" : !st ? "?"
                          : s.state == TWAI_STATE_RUNNING ? "running" : s.state == TWAI_STATE_BUS_OFF ? "BUS-OFF"
                          : s.state == TWAI_STATE_STOPPED ? "stopped" : "recovering";
        r += String("Zustand/state: ") + state + ", gesund/healthy: " + (huaweiHealthy() ? "ja/yes" : "NEIN/NO");
        const String problem = huaweiProblem();
        if (problem.length()) r += " -> " + problem;
        r += "\n";
        r += String("Frames empfangen/received: ") + canStats.rx + ", fremde ID/foreign: " + canStats.rxForeign;
        if (canStats.lastForeignAt) {
            char s2[12];
            snprintf(s2, sizeof(s2), "%08lX", static_cast<unsigned long>(canStats.lastForeignId));
            r += String(" (zuletzt/last 0x") + s2 + ", " + age(canStats.lastForeignAt) + ")";
        }
        r += String(", Sendefehler/tx rejected: ") + canStats.txFail + ", Bus-Off: " + canStats.busOff + "\n";
        if (st) {
            r += String("Treiber/driver: TEC ") + s.tx_error_counter + ", REC " + s.rx_error_counter + ", tx failed " +
                 s.tx_failed_count + ", rx verloren/lost " + (s.rx_missed_count + s.rx_overrun_count) +
                 ", Busfehler/bus errors " + s.bus_error_count + ", Arbitrierung verloren/arb lost " + s.arb_lost_count +
                 ", Warteschlange/queue TX " + s.msgs_to_tx + " RX " + s.msgs_to_rx + "\n";
        }
        for (int i = 0; i < cfg.huaweiCount; ++i) {
            const auto &u = rectifier[i];
            const bool live = recent(u.acAt, 3000);
            r += String("Huawei ") + (i + 1) + ": Adresse/address " + canNode[i] + ", Frames " + canStats.rxUnit[i] +
                 ", letzte Antwort/last answer " + age(u.acAt) + ", Temperatur/temp " + age(u.tempAt) + ", 0x7F roh/raw 0x" + String(u.tempRaw, HEX);
            if (live) {
                r += String(" | ") + String(u.outV, 1) + " V, " + String(u.outA, 1) + " A, DC " + String(u.dc, 0) +
                     " W, AC " + String(u.ac, 0) + " W, Netz/mains " + String(u.inV, 0) + " V, " + String(u.temp, 0) +
                     " C, Wirkungsgrad/eff " + String(u.eff * 100, 0) + " %";
            }
            if (hwPlugUsed(i)) r += String(" | Shelly: ") + hwPlugText(i) + ", " + hwPlug[i].watts + " W";
            r += "\n";
        }
    }
    r += "\n== Ereignisse/events ==\n" + diagEvents();
    r += "\n== CAN-Mitschnitt/capture ==\n";
    const String frames = diagCaptureText();
    r += frames.length() ? frames : String(diagCaptureActive() ? "laeuft/running\n" : "keiner/none\n");
    return r;
}
