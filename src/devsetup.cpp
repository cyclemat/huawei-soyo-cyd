#include "devsetup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include "i18n.h"

namespace devsetup {

const IPAddress AP_IP(4, 3, 2, 1);

namespace {
const char *NS = "devsetup";
const uint32_t BOOT_OK_MS = 10000;   // so lange gelaufen = normaler Start, Zaehler wieder 0

WebServer *s_srv = nullptr;
DNSServer *s_dns = nullptr;
String s_pw = DEVSETUP_DEFAULT_PW, s_ssid;        // Zugangspasswort (Weboberflaeche)
String s_appw = DEVSETUP_DEFAULT_PW;               // WLAN-Passwort des Einrichtungs-WLANs
bool s_apon = true;                                // false = Einrichtungs-WLAN offen
bool s_pwSet = false, s_bootcClear = false, s_loaded = false;
uint32_t s_apRestartMs = 0, s_lockUntil = 0;
uint8_t s_fails = 0, s_tokNext = 0;
String s_tokens[4];   // angemeldete Browser, nur im RAM

void load() {
    if (s_loaded) return;
    s_loaded = true;
    Preferences p;
    if (!p.begin(NS, true)) return;   // noch nichts gespeichert
    s_pw = p.getString("apw", DEVSETUP_DEFAULT_PW);
    s_pwSet = p.getBool("pwset", false);
    s_appw = p.getString("appw", DEVSETUP_DEFAULT_PW);
    s_apon = p.getBool("apon", true);
    p.end();
    if (s_pw.length() < 8) {
        s_pw = DEVSETUP_DEFAULT_PW;
        s_pwSet = false;
    }
    if (s_appw.length() < 8) s_appw = DEVSETUP_DEFAULT_PW;
}

void save() {
    Preferences p;
    p.begin(NS, false);
    p.putString("apw", s_pw);
    p.putBool("pwset", s_pwSet);
    p.putString("appw", s_appw);
    p.putBool("apon", s_apon);
    p.end();
}

// Einrichtungs-WLAN mit den aktuellen Einstellungen (neu) starten; offen = ohne Passwort
void softap() { WiFi.softAP(s_ssid.c_str(), s_apon ? s_appw.c_str() : nullptr); }

void newSession() {
    char t[33];
    for (int i = 0; i < 4; i++) snprintf(t + i * 8, 9, "%08lx", (unsigned long)esp_random());
    s_tokens[s_tokNext] = t;
    s_tokNext = (s_tokNext + 1) % 4;
    s_srv->sendHeader("Set-Cookie", String("hsauth=") + t + "; Path=/; HttpOnly; SameSite=Strict");
}

bool locked() {
    if (s_lockUntil && (int32_t)(millis() - s_lockUntil) < 0) return true;
    s_lockUntil = 0;
    return false;
}

const char *wrongPw() {
    if (++s_fails >= 5) {
        s_fails = 0;
        s_lockUntil = (millis() + 30000) | 1;
    }
    return T("Passwort falsch.", "Wrong password.");
}
}  // namespace

// ---------------------------------------------------------------- Mehrfach-Reset
Boot bootCheck() {
    load();
    Boot b;
    Preferences p;
    p.begin(NS, false);
    // Nur echtes Aus-/Einschalten und die Reset-Taste zaehlen. Abstuerze, Watchdog und Software-Neustarts
    // nicht: eine Neustart-Schleife darf weder das Einrichtungs-WLAN erzwingen noch die Passwoerter loeschen.
    const esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_POWERON && reason != ESP_RST_EXT) {
        p.putUChar("bootc", 0);
        p.end();
        return b;
    }
    b.count = p.getUChar("bootc", 0) + 1;
    p.putUChar("bootc", b.count);
    p.end();
    if (b.count >= 5) {
        b.forceAp = b.reset = true;
        passwordReset();
    } else if (b.count >= 3) {
        b.forceAp = true;
    }
    return b;
}

void begin(WebServer &srv) {
    load();
    s_srv = &srv;
    static const char *HDR[] = {"Cookie"};
    srv.collectHeaders(HDR, 1);
}

void loop() {
    if (s_dns) s_dns->processNextRequest();
    if (!s_bootcClear && millis() > BOOT_OK_MS) {
        s_bootcClear = true;
        Preferences p;
        p.begin(NS, false);
        p.putUChar("bootc", 0);
        p.end();
    }
    if (s_apRestartMs && (int32_t)(millis() - s_apRestartMs) >= 0) {
        s_apRestartMs = 0;
        softap();   // gleiches Netz, neue Einstellung
    }
}

// ---------------------------------------------------------------- Einrichtungs-WLAN
void apStart(const String &ssid) {
    load();
    s_ssid = ssid;
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    softap();
    if (!s_dns) {
        s_dns = new DNSServer();
        s_dns->setErrorReplyCode(DNSReplyCode::NoError);
        s_dns->start(53, "*", AP_IP);
    }
}

bool apActive() { return (WiFi.getMode() & WIFI_AP) != 0; }
const String &apSsid() { return s_ssid; }
bool apPasswordOn() { load(); return s_apon; }
bool apPasswordDefault() { load(); return s_appw == DEVSETUP_DEFAULT_PW; }

const char *setApPassword(bool on, const String &pw) {
    load();
    if (pw.length() && (pw.length() < 8 || pw.length() > 63)) {
        return T("Das WLAN-Passwort braucht 8 bis 63 Zeichen.", "The Wi-Fi password needs 8 to 63 characters.");
    }
    if (on == s_apon && !pw.length()) return nullptr;
    s_apon = on;
    if (pw.length()) s_appw = pw;
    save();
    if (apActive()) s_apRestartMs = (millis() + 2000) | 1;   // Einrichtungs-WLAN mit der neuen Einstellung
    return nullptr;
}

// ---------------------------------------------------------------- Zugang Weboberflaeche
bool authed() {
    if (!s_pwSet || !s_srv) return false;
    String c = s_srv->header("Cookie");
    int i = c.indexOf("hsauth=");
    if (i < 0) return false;
    String t = c.substring(i + 7, i + 7 + 32);
    if (t.length() != 32) return false;
    for (auto &k : s_tokens)
        if (k.length() && k == t) return true;
    return false;
}

bool passwordSet() {
    load();
    return s_pwSet;
}

const char *login(const String &pw) {
    load();
    if (locked()) return T("Zu viele Versuche – bitte 30 Sekunden warten.", "Too many attempts – please wait 30 seconds.");
    if (!s_pwSet) return T("Bitte zuerst ein eigenes Passwort vergeben.", "Please set your own password first.");
    if (s_pw != pw) return wrongPw();
    s_fails = 0;
    newSession();
    return nullptr;
}

// Erstes Mal mit dem Standardpasswort, danach mit dem aktuellen
const char *setPassword(const String &oldPw, const String &newPw) {
    load();
    if (locked()) return T("Zu viele Versuche – bitte 30 Sekunden warten.", "Too many attempts – please wait 30 seconds.");
    if (oldPw != s_pw) return wrongPw();
    if (newPw.length() < 8 || newPw.length() > 63) {
        return T("Das neue Passwort braucht 8 bis 63 Zeichen.", "The new password needs 8 to 63 characters.");
    }
    if (newPw == DEVSETUP_DEFAULT_PW) {
        return T("Bitte ein anderes als das Standardpasswort nehmen.", "Please choose a password other than the default.");
    }
    s_fails = 0;
    s_pw = newPw;
    s_pwSet = true;
    save();
    for (auto &k : s_tokens) k = "";   // alle anderen abmelden
    newSession();
    return nullptr;
}

void logout() {
    for (auto &k : s_tokens) k = "";
    if (s_srv) s_srv->sendHeader("Set-Cookie", "hsauth=; Path=/; Max-Age=0");
}

void passwordReset() {
    s_pw = DEVSETUP_DEFAULT_PW;
    s_pwSet = false;
    s_appw = DEVSETUP_DEFAULT_PW;
    s_apon = true;
    for (auto &k : s_tokens) k = "";
    save();
}

}  // namespace devsetup
