// Log auf der microSD-Karte – siehe sdlog.h
#include "sdlog.h"

#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <algorithm>
#include <memory>
#include "app.h"
#include "diag.h"

namespace {
const size_t FILE_MAX = 1024 * 1024;             // je Datei
const uint64_t KEEP_FREE = 8ULL * 1024 * 1024;   // so viel bleibt frei, sonst aelteste loeschen
const uint32_t STATUS_MS = 10000, FLUSH_MS = 10000, MOUNT_RETRY_MS = 30000, SPACE_MS = 600000;
const size_t PENDING_MAX = 8192;                 // Puffer fuer Ereignisse, falls die Karte fehlt

SPIClass sdSpi(VSPI);
SemaphoreHandle_t lock = nullptr;   // Karte und Puffer
String pending;
SdLogInfo info;
uint32_t fileNumber = 0;

String timestamp() {
    char buf[40];
    const time_t now = time(nullptr);
    if (now > 1700000000) {   // Uhrzeit per NTP bekannt
        struct tm t;
        localtime_r(&now, &t);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    } else {
        snprintf(buf, sizeof(buf), "-");
    }
    return String(buf) + ";" + String(millis() / 1000);
}

void addLine(const String &line) {
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (pending.length() + line.length() < PENDING_MAX) pending += line + "\n";
    xSemaphoreGive(lock);
}

String num(float v, bool valid) { return valid && isfinite(v) ? String(lroundf(v)) : String(""); }

String statusLine() {
    const bool grid = meterOK();
    const bool soyo = cfg.soyoMeter[0] && soyoMeter.valid && recent(soyoMeter.time, meterMaxAge);
    const float dc = cfg.huaweiCount ? huaweiPower(true) : NAN;
    const float ac = cfg.huaweiCount ? huaweiPower(false) : NAN;
    const char *m = mode == Mode::Zero ? T("Nulleinspeisung", "zero export")
                    : mode == Mode::Manual ? T("Nur Laden", "charge only") : T("Aus", "off");
    return timestamp() + ";" + T("STATUS", "STATUS") + ";" + m + ";" + num(meter.watts, grid) + ";" +
           String(dispatch.soyo) + ";" + num(soyoMeter.watts, soyo) + ";" + String(dispatch.huawei) + ";" +
           num(dc, true) + ";" + num(ac, true) + ";" + String(cfg.reserve) + ";";
}

String header() {
    return T("Zeit;Laufzeit_s;Art;Modus;Netz_W;Soyo_Soll_W;Soyo_Ist_W;Huawei_Soll_W;Huawei_DC_W;Huawei_AC_W;Restbezug_W;Text",
             "Time;Uptime_s;Type;Mode;Grid_W;Soyo_set_W;Soyo_actual_W;Huawei_set_W;Huawei_DC_W;Huawei_AC_W;Target_W;Text");
}

std::vector<SdLogFile> listLocked() {
    std::vector<SdLogFile> files;
    File dir = SD.open("/logs");
    if (!dir) return files;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (!f.isDirectory() && sdlogValidName(name)) files.push_back({name, f.size()});
        f.close();
    }
    dir.close();
    std::sort(files.begin(), files.end(), [](const SdLogFile &a, const SdLogFile &b) { return a.name > b.name; });
    return files;
}

void startFile() {
    const auto files = listLocked();
    fileNumber = files.empty() ? 1 : files.front().name.substring(4, 9).toInt() + 1;
    if (fileNumber > 99999) fileNumber = 1;
    char name[32];
    snprintf(name, sizeof(name), "/logs/log-%05u.csv", unsigned(fileNumber));
    info.current = name + 6;
    File f = SD.open(name, FILE_WRITE);
    if (f) {
        f.println(header());
        f.close();
    }
}

// Platz schaffen: aelteste Dateien loeschen, bis genug frei ist (die aktuelle bleibt)
void makeRoom() {
    info.totalBytes = SD.totalBytes();
    info.freeBytes = info.totalBytes - SD.usedBytes();
    while (info.freeBytes < KEEP_FREE) {
        auto files = listLocked();
        if (files.size() <= 1) break;
        const String oldest = "/logs/" + files.back().name;
        if (files.back().name == info.current || !SD.remove(oldest)) break;
        info.freeBytes = info.totalBytes - SD.usedBytes();
    }
}

bool mount() {
    sdSpi.begin(18, 19, 23, 5);
    if (!SD.begin(5, sdSpi, 16000000, "/sd", 2)) {
        SD.end();
        sdSpi.end();
        return false;
    }
    if (!SD.exists("/logs")) SD.mkdir("/logs");
    startFile();
    makeRoom();
    return true;
}

void unmount() {
    SD.end();
    sdSpi.end();
    info = SdLogInfo();
}

void task(void *) {
    uint32_t lastMount = 0, lastStatus = 0, lastFlush = 0, lastSpace = millis();
    for (;;) {
        const uint32_t now = millis();
        if (now - lastStatus >= STATUS_MS) {
            lastStatus = now;
            addLine(statusLine());
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        if (!info.mounted && (!lastMount || now - lastMount >= MOUNT_RETRY_MS)) {
            lastMount = now;
            info.mounted = mount();
        }
        if (info.mounted && now - lastFlush >= FLUSH_MS) {
            lastFlush = now;
            File f = SD.open(String("/logs/") + info.current, FILE_APPEND);
            if (!f) {
                unmount();   // Karte gezogen oder defekt: spaeter neu versuchen
            } else {
                if (pending.length()) f.print(pending);
                pending = "";
                const size_t size = f.size();
                f.close();
                if (size >= FILE_MAX) {
                    startFile();
                    makeRoom();
                } else if (now - lastSpace >= SPACE_MS) {
                    lastSpace = now;
                    makeRoom();
                }
            }
        }
        xSemaphoreGive(lock);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
}  // namespace

void sdlogBegin() {
    if (!cfg.sdLog || lock) return;
    lock = xSemaphoreCreateMutex();
    pending.reserve(1024);
    xTaskCreate(task, "sdlog", 8192, nullptr, 1, nullptr);   // FAT-Zugriffe und String-Zeilen brauchen Stack
}

void sdlogEvent(const char *text) {
    String t = text;
    t.replace(";", ",");
    t.replace("\n", " ");
    diagEvent(t.c_str());   // immer, auch ohne SD-Karte (Weboberflaeche -> Diagnose)
    addLine(timestamp() + ";" + T("EREIGNIS", "EVENT") + ";;;;;;;;;" + t);
}

SdLogInfo sdlogInfo() {
    if (!lock) return SdLogInfo();
    xSemaphoreTake(lock, portMAX_DELAY);
    SdLogInfo copy = info;
    xSemaphoreGive(lock);
    return copy;
}

std::vector<SdLogFile> sdlogFiles() {
    if (!lock) return {};
    xSemaphoreTake(lock, portMAX_DELAY);
    auto files = info.mounted ? listLocked() : std::vector<SdLogFile>();
    xSemaphoreGive(lock);
    return files;
}

bool sdlogValidName(const String &n) {
    if (n.length() != 13 || !n.startsWith("log-") || !n.endsWith(".csv")) return false;
    for (int i = 4; i < 9; ++i)
        if (!isDigit(n[i])) return false;
    return true;
}

bool sdlogSend(const String &name) {
    if (!lock || !sdlogValidName(name)) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = false;
    if (info.mounted) {
        File f = SD.open("/logs/" + name, FILE_READ);
        if (f) {
            // In Stuecken senden und zwischendurch die Regelung bedienen: eine 1-MB-Datei braucht einige
            // Sekunden, so lange duerfen Soyo und Huawei nicht ohne Sollwerte bleiben.
            web.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
            web.setContentLength(f.size());
            web.send(200, "text/csv", "");
            std::unique_ptr<char[]> buf(new char[2048]);
            WiFiClient client = web.client();
            while (buf && f.available() && client.connected()) {
                const int n = f.read(reinterpret_cast<uint8_t *>(buf.get()), 2048);
                if (n <= 0) break;
                web.sendContent(buf.get(), n);
                serviceOutputs();
            }
            f.close();
            ok = true;
        }
    }
    xSemaphoreGive(lock);
    return ok;
}

void sdlogDeleteAll() {
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (info.mounted) {
        for (auto &f : listLocked())
            if (f.name != info.current) SD.remove("/logs/" + f.name);
        info.freeBytes = info.totalBytes - SD.usedBytes();
    }
    xSemaphoreGive(lock);
}
