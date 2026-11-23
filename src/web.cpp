// Weboberflaeche: Anmeldung, Einstellungen, WLAN-Suche, Live-Werte (Design wie Simple Open Camper)
#include <vector>
#include "app.h"
#include "devsetup.h"

// ---------------------------------------------------------------- Ausgabe in Stuecken
// Die Seite wird in kleinen Teilen gesendet, damit kein grosser Puffer im RAM liegt.
struct Page {
    String buf;
    Page() {
        web.sendHeader("Cache-Control", "no-store");
        web.setContentLength(CONTENT_LENGTH_UNKNOWN);
        web.send(200, "text/html; charset=utf-8", "");
        buf.reserve(2200);
    }
    Page &operator<<(const String &s) { buf += s; flush(false); return *this; }
    Page &operator<<(const char *s) { buf += s; flush(false); return *this; }
    Page &operator<<(const __FlashStringHelper *s) { buf += s; flush(false); return *this; }
    Page &operator<<(int v) { buf += String(v); return *this; }
    void flush(bool all) {
        if (buf.length() >= 2048 || (all && buf.length())) {
            web.sendContent(buf);
            buf = "";
        }
    }
    ~Page() { flush(true); web.sendContent(""); }
};

String esc(const String &s) {
    String r;
    r.reserve(s.length() + 8);
    for (unsigned i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (c == '&') r += "&amp;";
        else if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;";
        else r += c;
    }
    return r;
}

// Zahl mit einer Nachkommastelle, Dezimalpunkt fuer <input type=number>
String dec1(int value10) { return String(value10 / 10.0f, 1); }

static const char CSS[] PROGMEM = R"CSS(
:root{--bench:#E3E6EA;--surface:#F5F6F8;--raised:#FFFFFF;--ink:#161A20;--muted:#58626E;--line:#D0D5DB;
--amber:#F3A43B;--amber-ink:#3A2204;--signal:#2C68E0;--danger:#C23E2D;--ok:#2E9D5B;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bench:#0E1013;--surface:#16191E;--raised:#1D2127;--ink:#E6EAEE;
--muted:#9AA4AF;--line:#2B3138;--amber:#F3A43B;--amber-ink:#2A1703;--signal:#5C93FF;--danger:#FF7564;--ok:#4CC27E;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--bench);color:var(--ink);
font:400 15px/1.45 Inter,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
.top{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:10px;padding:10px 16px;
background:var(--surface);border-bottom:1px solid var(--line)}
.brand{font-weight:600;display:flex;align-items:center;gap:10px;flex:1;min-width:0}
.mark{width:30px;height:22px;border-radius:6px;background:#10131A;position:relative;flex:none}
.mark:before,.mark:after{content:"";position:absolute;top:6px;width:9px;height:10px;border-radius:2px}
.mark:before{left:5px;background:var(--amber)}.mark:after{right:5px;background:#3A4350}
.tag{font-size:12px;font-weight:500;color:var(--muted);border:1px solid var(--line);border-radius:999px;padding:1px 8px}
.lang a{font-size:13px;color:var(--muted);text-decoration:none;padding:4px 9px;border-radius:999px}
.lang a.on{background:var(--amber);color:var(--amber-ink)}
main{max-width:720px;margin:0 auto;padding:16px}
.card{background:var(--raised);border:1px solid var(--line);border-radius:16px;padding:16px;margin:0 0 14px}
h2{font-size:16px;margin:0 0 12px}h3{font-size:14px;margin:14px 0 8px;color:var(--muted);font-weight:600}
.hint{color:var(--muted);font-size:13px;margin:2px 0 12px}
label.f{display:block;margin:0 0 12px}label.f>span{display:block;font-size:13px;color:var(--muted);margin:0 0 4px}
input,select{width:100%;background:var(--surface);color:var(--ink);border:1px solid var(--line);border-radius:10px;
padding:10px 12px;font:inherit}
input:focus,select:focus{outline:2px solid var(--signal);outline-offset:-1px}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1;min-width:140px}
button{border-radius:999px;border:1px solid var(--line);background:var(--surface);color:var(--ink);padding:10px 18px;
font:inherit;font-weight:600;cursor:pointer}
button.p{background:var(--amber);border-color:var(--amber);color:var(--amber-ink)}button.d{color:var(--danger)}
.sw{display:flex;align-items:center;justify-content:space-between;gap:14px;margin:0 0 12px;cursor:pointer}
.sw small{display:block;color:var(--muted);font-size:13px}
.sw input{appearance:none;-webkit-appearance:none;width:46px;height:26px;border-radius:999px;background:var(--line);
position:relative;flex:none;cursor:pointer;padding:0;border:0;margin:0}
.sw input:after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;
background:#fff;transition:left .15s}
.sw input:checked{background:var(--amber)}.sw input:checked:after{left:23px}
.pills{display:flex;gap:6px;flex-wrap:wrap;margin:0 0 14px}
.pills input{display:none}.pills label{border:1px solid var(--line);border-radius:999px;padding:7px 14px;cursor:pointer;font-size:14px}
.pills input:checked+label{background:var(--amber);border-color:var(--amber);color:var(--amber-ink);font-weight:600}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 12px}
.chip{border:1px solid var(--line);border-radius:999px;padding:3px 10px;font-size:13px;color:var(--muted)}
.chip.ok{color:var(--ok)}.chip.err{color:var(--danger)}
.live{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}
.tile{background:var(--surface);border:1px solid var(--line);border-radius:14px;padding:10px 12px}
.tile b{display:block;font-size:22px;font-weight:600;font-feature-settings:"tnum"}.tile span{font-size:13px;color:var(--muted)}
.net{display:flex;justify-content:space-between;gap:10px;padding:10px 12px;margin:0 0 6px;background:var(--surface);
border:1px solid var(--line);border-radius:10px;cursor:pointer}.net.sel{border-color:var(--signal)}
.net small{color:var(--muted)}
.save{position:sticky;bottom:0;padding:14px 0 6px;background:linear-gradient(transparent,var(--bench) 35%)}
.save button{width:100%}
.msg{padding:10px 12px;border-radius:10px;margin:0 0 14px;border:1px solid var(--line);background:var(--surface)}
.msg.err{color:var(--danger)}
dialog{border:1px solid var(--line);border-radius:16px;background:var(--raised);color:var(--ink);max-width:420px;width:92%}
dialog::backdrop{background:rgba(0,0,0,.45)}
code{font-family:ui-monospace,monospace;font-size:13px}
)CSS";

void pageHead(Page &p, const char *title) {
    p << "<!doctype html><html lang=\"" << (uiLang ? "en" : "de")
      << "\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>"
      << title << "</title><style>" << FPSTR(CSS) << "</style></head><body><div class=\"top\"><div class=\"brand\">"
      << "<span class=\"mark\"></span>Huawei · Soyo<span class=\"tag\">Beta</span></div><div class=\"lang\">"
      << "<a href=\"/lang?l=0\"" << (uiLang ? "" : " class=\"on\"") << ">DE</a>"
      << "<a href=\"/lang?l=1\"" << (uiLang ? " class=\"on\"" : "") << ">EN</a></div></div><main>";
}

void pageEnd(Page &p) { p << "</main></body></html>"; }

void field(Page &p, const char *label, const char *name, const String &value, const char *type = "text",
           const char *extra = "") {
    p << "<label class=\"f\"><span>" << label << "</span><input type=\"" << type << "\" name=\"" << name
      << "\" value=\"" << esc(value) << "\" " << extra << "></label>";
}

void toggle(Page &p, const char *label, const char *hint, const char *name, bool on, const char *extra = "") {
    p << "<label class=\"sw\"><span>" << label;
    if (hint && hint[0]) p << "<small>" << hint << "</small>";
    p << "</span><input type=\"checkbox\" name=\"" << name << "\" " << (on ? "checked " : "") << extra << "></label>";
}

void option(Page &p, int value, int current, const char *text) {
    p << "<option value=\"" << value << "\"" << (value == current ? " selected" : "") << ">" << text << "</option>";
}

void channelSelect(Page &p, const char *label, const char *name, int current) {
    p << "<label class=\"f\"><span>" << label << "</span><select name=\"" << name << "\">";
    option(p, -1, current, T("Summe aller Phasen (3EM)", "Sum of all phases (3EM)"));
    option(p, 0, current, T("Kanal 1 / Phase A", "Channel 1 / phase A"));
    option(p, 1, current, T("Kanal 2 / Phase B", "Channel 2 / phase B"));
    option(p, 2, current, T("Kanal 3 / Phase C", "Channel 3 / phase C"));
    p << "</select></label>";
}

void sendMessage(int code, const char *title, const String &text, bool back) {
    web.sendHeader("Cache-Control", "no-store");
    String html = String("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                         "content=\"width=device-width,initial-scale=1\"><style>") + FPSTR(CSS) +
                  "</style></head><body><main><div class=\"card\"><h2>" + title + "</h2><p>" + text + "</p>";
    if (back) html += String("<button class=\"p\" onclick=\"history.back()\">") + T("Zurück", "Back") + "</button>";
    html += "</div></main></body></html>";
    web.send(code, "text/html; charset=utf-8", html);
}

// ---------------------------------------------------------------- Anmeldung
void pageAuth(const char *error) {
    const bool setup = !devsetup::passwordSet();
    Page p;
    pageHead(p, T("Anmelden", "Sign in"));
    p << "<div class=\"card\"><h2>" << (setup ? T("Eigenes Passwort vergeben", "Set your own password") : T("Anmelden", "Sign in"))
      << "</h2>";
    if (error) p << "<div class=\"msg err\">" << error << "</div>";
    if (setup) {
        p << "<p class=\"hint\">" << T("Beim ersten Öffnen muss das Standardpasswort durch ein eigenes ersetzt werden "
                                       "(8 bis 63 Zeichen). Ab Werk: ",
                                       "On first use, replace the default password with your own (8 to 63 characters). "
                                       "Factory default: ")
          << "<code>" DEVSETUP_DEFAULT_PW "</code></p><form method=\"post\" action=\"/setpw\">";
        field(p, T("Standardpasswort", "Default password"), "old", "", "password", "autocomplete=\"current-password\"");
        field(p, T("Neues Passwort", "New password"), "new", "", "password", "autocomplete=\"new-password\" minlength=\"8\"");
        field(p, T("Neues Passwort wiederholen", "Repeat new password"), "new2", "", "password", "autocomplete=\"new-password\"");
        p << "<button class=\"p\">" << T("Speichern", "Save") << "</button></form>";
    } else {
        p << "<form method=\"post\" action=\"/login\">";
        field(p, T("Passwort", "Password"), "pw", "", "password", "autocomplete=\"current-password\" autofocus");
        p << "<button class=\"p\">" << T("Anmelden", "Sign in") << "</button></form>"
          << "<p class=\"hint\">" << T("Passwort vergessen? Das Gerät 5-mal schnell hintereinander aus- und einschalten "
                                       "(jeweils unter 10 s): beide Passwörter sind dann wieder ",
                                       "Forgot the password? Switch the device off and on 5 times quickly (under 10 s each): "
                                       "both passwords are reset to ")
          << "<code>" DEVSETUP_DEFAULT_PW "</code>.</p>";
    }
    p << "</div>";
    pageEnd(p);
}

void redirectHome() {
    web.sendHeader("Location", "/", true);
    web.send(303, "text/plain", "");
}

void handleLogin() {
    const char *error = devsetup::login(web.arg("pw"));
    if (error) return pageAuth(error);
    redirectHome();
}

void handleSetPassword() {
    if (web.arg("new") != web.arg("new2")) {
        const char *e = T("Die beiden neuen Passwörter sind verschieden.", "The two new passwords differ.");
        return devsetup::passwordSet() ? sendMessage(400, T("Passwort", "Password"), e, true) : pageAuth(e);
    }
    // Spaeter (angemeldet) mit dem aktuellen Passwort, beim ersten Mal mit dem Standardpasswort
    if (devsetup::passwordSet() && !devsetup::authed()) return pageAuth(nullptr);
    const char *error = devsetup::setPassword(web.arg("old"), web.arg("new"));
    if (error) return devsetup::passwordSet() ? sendMessage(400, T("Passwort", "Password"), error, true) : pageAuth(error);
    redirectHome();
}

bool guard() {
    if (devsetup::authed()) return true;
    if (web.uri() == "/") pageAuth(nullptr);
    else web.send(401, "text/plain", "login");
    return false;
}

// ---------------------------------------------------------------- Einstellungsseite
void pageSettings() {
    Page p;
    pageHead(p, T("Einstellungen", "Settings"));

    // Live-Werte
    p << "<div class=\"card\"><h2>" << T("Aktuell", "Live") << "</h2><div class=\"chips\" id=\"chips\"></div>"
      << "<div class=\"live\"><div class=\"tile\"><span>" << T("Netz", "Grid") << "</span><b id=\"vGrid\">–</b></div>"
      << "<div class=\"tile\"><span>" << T("Einspeisung Soyo", "Soyo feed-in") << "</span><b id=\"vSoyo\">–</b></div>"
      << "<div class=\"tile\"><span>" << T("Ladung Huawei", "Huawei charging") << "</span><b id=\"vHuawei\">–</b></div>"
      << "<div class=\"tile\"><span>" << T("Modus", "Mode") << "</span><b id=\"vMode\">–</b></div></div>"
      << "<p class=\"hint\">" << T("Beta: an echter Hardware noch nicht vollständig getestet. Mit kleiner Leistung beginnen.",
                                   "Beta: not yet fully tested on real hardware. Start with low power.")
      << "</p></div>";

    p << "<form method=\"post\" action=\"/save\" id=\"cfg\">";

    // WLAN
    p << "<div class=\"card\"><h2>" << T("WLAN", "Wi-Fi") << "</h2><div id=\"nets\"></div>"
      << "<p><button type=\"button\" id=\"scan\" onclick=\"scan(1)\">" << T("WLAN suchen", "Search Wi-Fi") << "</button></p>";
    field(p, T("Name des Heim-WLANs (oben antippen oder eintippen)", "Home Wi-Fi name (tap above or type)"), "ssid", cfg.ssid);
    field(p, T("Passwort (leer lässt das gespeicherte bestehen)", "Password (leave empty to keep the stored one)"),
          "password", "", "password", "autocomplete=\"new-password\"");
    p << "</div>";

    // Netzmessung
    p << "<div class=\"card\"><h2>" << T("Netzmessung", "Grid meter") << "</h2><div class=\"pills\">"
      << "<input type=\"radio\" name=\"meterSource\" id=\"ms0\" value=\"0\"" << (cfg.meterSource == 0 ? " checked" : "")
      << "><label for=\"ms0\">Shelly</label><input type=\"radio\" name=\"meterSource\" id=\"ms1\" value=\"1\""
      << (cfg.meterSource == 1 ? " checked" : "") << "><label for=\"ms1\">" << T("MQTT-Abo", "MQTT topic") << "</label></div>";
    p << "<div id=\"srcShelly\">";
    field(p, T("Shelly: IP-Adresse oder Hostname", "Shelly: IP address or host name"), "meter", cfg.meter);
    channelSelect(p, T("Shelly: Kanal", "Shelly: channel"), "channel", cfg.channel);
    p << "<p class=\"hint\">" << T("Shelly EM, 3EM, Pro EM, Pro 3EM, EM Gen4 – ohne HTTP-Anmeldung.",
                                   "Shelly EM, 3EM, Pro EM, Pro 3EM, EM Gen4 – without HTTP authentication.")
      << "</p></div><div id=\"srcMqtt\">";
    field(p, T("Topic", "Topic"), "meterTopic", cfg.meterTopic, "text", "placeholder=\"tele/zaehler/SENSOR\"");
    field(p, "JSONPath", "meterPath", cfg.meterPath, "text", "placeholder=\"$.ENERGY.Power\"");
    p << "<p class=\"hint\">" << T("Beispiele: <code>$.ENERGY.Power</code> (Tasmota), <code>$.total_act_power</code> "
                                   "(Shelly Pro 3EM). Leer = die Nachricht ist nur eine Zahl. Broker unten eintragen.",
                                   "Examples: <code>$.ENERGY.Power</code> (Tasmota), <code>$.total_act_power</code> "
                                   "(Shelly Pro 3EM). Empty = the message is just a number. Enter the broker below.")
      << "</p>";
    field(p, T("Wert gilt als ausgefallen nach (s)", "Value counts as lost after (s)"), "meterTimeout",
          String(cfg.meterTimeout), "number", "min=\"3\" max=\"300\"");
    p << "</div>";
    toggle(p, T("Vorzeichen umkehren", "Invert sign"),
           T("Netzbezug muss positiv angezeigt werden – oben unter „Aktuell“ prüfen.",
             "Grid import must show as positive – check under “Live” above."),
           "polarityInv", cfg.polarity < 0);
    field(p, T("Restbezug (Ziel am Netz) in W", "Grid target in W"), "reserve", String(cfg.reserve), "number",
          "min=\"0\" max=\"200\"");
    p << "</div>";

    // SoyoSource
    p << "<div class=\"card\"><h2>SoyoSource</h2><div class=\"row\">";
    field(p, T("Anzahl gleicher Geräte", "Number of identical units"), "soyoCount", String(cfg.soyoCount), "number",
          "min=\"0\" max=\"32\"");
    field(p, T("Höchstleistung je Gerät in W", "Max. power per unit in W"), "soyoUnit", String(cfg.soyoUnit), "number",
          "min=\"0\" max=\"1200\"");
    p << "</div><h3>" << T("Istwert-Messung (optional)", "Actual feed-in meter (optional)") << "</h3>";
    field(p, T("Shelly am Soyo-Abgang: IP-Adresse", "Shelly at the Soyo output: IP address"), "soyoMeter", cfg.soyoMeter);
    channelSelect(p, T("Kanal", "Channel"), "soyoChannel", cfg.soyoChannel);
    toggle(p, T("Vorzeichen umkehren", "Invert sign"),
           T("Einspeisung muss positiv angezeigt werden.", "Feed-in must show as positive."),
           "soyoPolarityInv", cfg.soyoPolarity < 0);
    p << "<p class=\"hint\">" << T("Alle Soyos am RS485-Bus im Limiter-Modus. Sie bekommen denselben Sollwert.",
                                   "All Soyos on the RS485 bus in limiter mode. They all get the same setpoint.")
      << "</p></div>";

    // Huawei
    p << "<div class=\"card\"><h2>Huawei</h2><label class=\"f\"><span>"
      << T("Modell (bestimmt den CAN-Stromfaktor)", "Model (sets the CAN current factor)")
      << "</span><select name=\"model\" id=\"model\">";
    for (int i = 0; i < modelCount; ++i) option(p, i, cfg.model, rectifierModels[i].name);
    p << "</select></label><label class=\"f\"><span>" << T("Anzahl (CAN-Adressen 1 bis 3)", "Count (CAN addresses 1 to 3)")
      << "</span><select name=\"huaweiCount\">";
    for (int i = 0; i <= 3; ++i) option(p, i, cfg.huaweiCount, i ? String(i).c_str() : T("keiner", "none"));
    p << "</select></label><div class=\"row\">";
    field(p, T("Ladespannung in V", "Charge voltage in V"), "volts", dec1(cfg.volts10), "number",
          "step=\"0.1\" min=\"0\" max=\"58\"");
    field(p, T("Höchststrom je Gerät in A", "Max. current per unit in A"), "amps", dec1(cfg.amps10), "number",
          "step=\"0.1\" min=\"0\" max=\"75\"");
    p << "</div><div id=\"custom\">";
    field(p, T("Eigener CAN-Stromfaktor", "Custom CAN current factor"), "ampScale", dec1(cfg.ampScale10), "number",
          "step=\"0.1\" min=\"0\" max=\"50\"");
    p << "</div><p class=\"hint\">" << T("Aktiver Stromfaktor: ", "Active current factor: ")
      << String(ampFactor(cfg), 2) << T(" (1024 ÷ Nennstrom)", " (1024 ÷ rated current)") << "</p>";
    p << "<h3>" << T("Ohne CAN-Signal", "Without CAN signal") << "</h3>";
    toggle(p, T("Mit festen Werten weiterladen", "Keep charging with fixed values"),
           T("Aus = Laden stoppt (empfohlen). An = niemand regelt mehr, der Huawei zieht dauerhaft Netzstrom.",
             "Off = charging stops (recommended). On = nothing regulates, the Huawei draws grid power continuously."),
           "fallbackOn", cfg.fallbackMode == 1, "id=\"fallbackOn\"");
    p << "<div class=\"row\" id=\"fallback\">";
    field(p, T("Spannung ohne CAN in V", "Voltage without CAN in V"), "fallbackVolts", dec1(cfg.fallbackVolts10), "number",
          "step=\"0.1\" min=\"0\" max=\"58\"");
    field(p, T("Strom ohne CAN je Gerät in A", "Current without CAN per unit in A"), "fallbackAmps",
          dec1(cfg.fallbackAmps10), "number", "step=\"0.1\" min=\"0\" max=\"75\"");
    p << "</div></div>";

    // MQTT / Home Assistant
    p << "<div class=\"card\"><h2>MQTT / Home Assistant</h2><p class=\"hint\">"
      << T("Leer lassen, wenn kein MQTT genutzt wird. Werte unter ", "Leave empty if MQTT is not used. Values under ")
      << "<code>" << esc(cfg.mqttBase) << "/state</code>.</p><div class=\"row\">";
    field(p, T("Broker: IP-Adresse oder Hostname", "Broker: IP address or host name"), "mqttHost", cfg.mqttHost);
    field(p, "Port", "mqttPort", String(cfg.mqttPort), "number", "min=\"1\" max=\"65535\"");
    p << "</div><div class=\"row\">";
    field(p, T("Benutzer (optional)", "User (optional)"), "mqttUser", cfg.mqttUser);
    field(p, T("Passwort (leer = unverändert)", "Password (empty = unchanged)"), "mqttPass", "", "password",
          "autocomplete=\"new-password\"");
    p << "</div>";
    field(p, T("Topic-Präfix", "Topic prefix"), "mqttBase", cfg.mqttBase);
    toggle(p, T("Home-Assistant-Discovery", "Home Assistant discovery"),
           T("Die Sensoren erscheinen in Home Assistant von selbst.", "The sensors appear in Home Assistant automatically."),
           "haDiscovery", cfg.haDiscovery != 0);
    p << "</div>";

    // Zugang
    p << "<div class=\"card\"><h2>" << T("Zugang", "Access") << "</h2>";
    toggle(p, T("Einrichtungs-WLAN mit Passwort", "Setup Wi-Fi with password"),
           T("Aus = offenes Einrichtungs-WLAN (die Weboberfläche bleibt durch das Zugangspasswort geschützt).",
             "Off = open setup Wi-Fi (the web interface stays protected by the access password)."),
           "apPwOn", devsetup::apPasswordOn());
    field(p, T("Neues WLAN-Passwort für das Einrichtungs-WLAN (leer = unverändert)",
               "New password for the setup Wi-Fi (empty = unchanged)"),
          "apPw", "", "password", "autocomplete=\"new-password\"");
    p << "<label class=\"f\"><span>" << T("Sprache (Display nach dem Neustart)", "Language (display after restart)")
      << "</span><select name=\"lang\">";
    option(p, 0, cfg.lang, "Deutsch");
    option(p, 1, cfg.lang, "English");
    p << "</select></label></div>";

    p << "<div class=\"save\"><button class=\"p\">" << T("Speichern und neu starten", "Save and restart")
      << "</button></div></form>";

    // Aktionen ausserhalb des Formulars
    p << "<div class=\"card\"><h2>" << T("Gerät", "Device") << "</h2><div class=\"row\">"
      << "<button type=\"button\" onclick=\"pw.showModal()\">" << T("Zugangspasswort ändern", "Change access password")
      << "</button><form method=\"post\" action=\"/calibrate\" style=\"display:contents\"><button>"
      << T("Touch kalibrieren", "Calibrate touch") << "</button></form>"
      << "<form method=\"post\" action=\"/logout\" style=\"display:contents\"><button class=\"d\">"
      << T("Abmelden", "Sign out") << "</button></form></div><p class=\"hint\">Firmware " FW_VERSION "</p></div>";

    p << "<dialog id=\"pw\"><form method=\"post\" action=\"/setpw\"><h2>" << T("Zugangspasswort ändern", "Change access password")
      << "</h2>";
    field(p, T("Aktuelles Passwort", "Current password"), "old", "", "password", "autocomplete=\"current-password\"");
    field(p, T("Neues Passwort (8 bis 63 Zeichen)", "New password (8 to 63 characters)"), "new", "", "password",
          "autocomplete=\"new-password\" minlength=\"8\"");
    field(p, T("Neues Passwort wiederholen", "Repeat new password"), "new2", "", "password", "autocomplete=\"new-password\"");
    p << "<div class=\"row\"><button type=\"button\" onclick=\"pw.close()\">" << T("Abbrechen", "Cancel")
      << "</button><button class=\"p\">" << T("Speichern", "Save") << "</button></div></form></dialog>";

    // Skript: Sichtbarkeit, WLAN-Suche, Live-Werte
    p << "<script>const L={search:'" << T("Suche läuft …", "Searching …") << "',again:'" << T("Erneut suchen", "Search again")
      << "',none:'" << T("Keine WLANs gefunden – erneut suchen", "No networks found – search again") << "',off:'"
      << T("keine Messung", "no reading") << "',modes:{zero:'" << T("Nulleinspeisung", "Zero export") << "',charge:'"
      << T("Nur Laden", "Charge only") << "',off:'" << T("Aus", "Off") << "'}};";
    p << R"JS(
const $=s=>document.querySelector(s),nb=' ';
function vis(){const m=$('input[name=meterSource]:checked').value;$('#srcShelly').style.display=m=='0'?'':'none';
$('#srcMqtt').style.display=m=='1'?'':'none';$('#custom').style.display=$('#model').value=='0'?'':'none';
$('#fallback').style.display=$('#fallbackOn').checked?'':'none';}
document.querySelectorAll('input[name=meterSource],#model,#fallbackOn').forEach(e=>e.onchange=vis);vis();
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂';}
function scan(refresh){const b=$('#scan');b.textContent=L.search;
fetch('/scan'+(refresh?'?refresh=1':'')).then(r=>r.json()).then(d=>{const box=$('#nets');box.innerHTML='';
d.nets.forEach(n=>{const e=document.createElement('div');e.className='net';const a=document.createElement('span');
a.textContent=(n.lock?'🔒 ':'')+n.ssid;const s=document.createElement('small');s.textContent=bars(n.rssi)+' '+n.rssi+nb+'dBm';
e.append(a,s);e.onclick=()=>{$('[name=ssid]').value=n.ssid;document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));
e.classList.add('sel');$('[name=password]').focus();};box.append(e);});
if(d.running)setTimeout(()=>scan(0),1000);else b.textContent=d.nets.length?L.again:L.none;}).catch(()=>{});}
function w(v){return v==null?'–':Math.round(v)+nb+'W';}
function live(){fetch('/api/status').then(r=>r.json()).then(d=>{
$('#vGrid').textContent=d.grid==null?L.off:(d.grid>0?'+':'')+w(d.grid);$('#vSoyo').textContent=w(d.soyo);
$('#vHuawei').textContent=w(d.huawei);$('#vMode').textContent=L.modes[d.mode]||d.mode;
$('#chips').innerHTML=d.chips.map(c=>'<span class="chip '+(c[1]?'ok':'err')+'">'+c[0]+'</span>').join('');
}).catch(()=>{});}
scan(0);live();setInterval(live,2000);
</script>)JS";
    pageEnd(p);
}

// ---------------------------------------------------------------- Speichern
bool readInt(const char *name, int &value, int low, int high) {
    const String raw = web.arg(name);
    char *end;
    const long parsed = strtol(raw.c_str(), &end, 10);
    if (!raw.length() || *end || parsed < low || parsed > high) return false;
    value = parsed;
    return true;
}

// Dezimalzahl (Komma oder Punkt) in Zehnteln, z. B. "53,5" -> 535
bool readTenths(const char *name, int &value10, int low10, int high10) {
    String raw = web.arg(name);
    raw.trim();
    raw.replace(',', '.');
    if (!raw.length()) { value10 = 0; return low10 <= 0; }
    char *end;
    const double v = strtod(raw.c_str(), &end);
    if (*end || !isfinite(v)) return false;
    const long t = lround(v * 10);
    if (t < low10 || t > high10) return false;
    value10 = t;
    return true;
}

bool validHost(const String &host) {
    for (unsigned i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    return host.length() <= 63;
}

void handleSave() {
    Config next = cfg;
    String ssid = web.arg("ssid"), password = web.arg("password"), host = web.arg("meter");
    const String soyoHost = web.arg("soyoMeter");
    const String mqttHost = web.arg("mqttHost"), mqttUser = web.arg("mqttUser"), mqttPass = web.arg("mqttPass");
    String mqttBase = web.arg("mqttBase"), meterTopic = web.arg("meterTopic"), meterPath = web.arg("meterPath");
    ssid.trim();
    host.trim();
    mqttBase.trim();
    meterTopic.trim();
    meterPath.trim();
    if (!mqttBase.length()) mqttBase = "huaweisoyo";
    bool baseOK = mqttBase.length() <= 32;
    for (unsigned i = 0; i < mqttBase.length(); ++i) {
        if (mqttBase[i] == '+' || mqttBase[i] == '#' || mqttBase[i] == ' ') baseOK = false;
    }
    next.polarity = web.hasArg("polarityInv") ? -1 : 1;
    next.soyoPolarity = web.hasArg("soyoPolarityInv") ? -1 : 1;
    next.fallbackMode = web.hasArg("fallbackOn") ? 1 : 0;
    next.haDiscovery = web.hasArg("haDiscovery") ? 1 : 0;

    const char *error = nullptr;
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
        error = T("Bitte den Namen des Heim-WLANs eintragen.", "Please enter the home Wi-Fi name.");
    } else if (!validHost(host) || !validHost(soyoHost) || !validHost(mqttHost)) {
        error = T("Adressen dürfen nur Buchstaben, Ziffern, Punkt und Bindestrich enthalten.",
                  "Addresses may only contain letters, digits, dots and hyphens.");
    } else if (!readInt("meterSource", next.meterSource, 0, 1) || !readInt("channel", next.channel, -1, 2) ||
               !readInt("meterTimeout", next.meterTimeout, 3, 300) || !readInt("reserve", next.reserve, 0, 200) ||
               !readInt("soyoCount", next.soyoCount, 0, 32) || !readInt("soyoUnit", next.soyoUnit, 0, 1200) ||
               !readInt("soyoChannel", next.soyoChannel, -1, 2) || !readInt("model", next.model, 0, modelCount - 1) ||
               !readInt("huaweiCount", next.huaweiCount, 0, 3) || !readInt("mqttPort", next.mqttPort, 1, 65535) ||
               !readInt("lang", next.lang, 0, 1) || !readTenths("volts", next.volts10, 0, 580) ||
               !readTenths("amps", next.amps10, 0, 750) || !readTenths("ampScale", next.ampScale10, 0, 500) ||
               !readTenths("fallbackVolts", next.fallbackVolts10, 0, 580) ||
               !readTenths("fallbackAmps", next.fallbackAmps10, 0, 750)) {
        error = T("Eine Zahl liegt außerhalb des erlaubten Bereichs.", "A number is out of range.");
    } else if (next.soyoCount && !next.soyoUnit) {
        error = T("Bitte die Höchstleistung je Soyo eintragen.", "Please enter the max. power per Soyo.");
    } else if (next.huaweiCount && (next.volts10 < 450 || !next.amps10 ||
                                    next.amps10 > rectifierModels[next.model].ratedAmps * 10 ||
                                    (next.model == 0 && next.ampScale10 < 100))) {
        error = T("Huawei: Ladespannung ab 45 V, Strom bis zum Nennstrom des Modells, eigener Faktor ab 10.",
                  "Huawei: charge voltage from 45 V, current up to the model's rating, custom factor from 10.");
    } else if (next.huaweiCount && next.fallbackMode == 1 &&
               (next.fallbackVolts10 < 450 || !next.fallbackAmps10 ||
                next.fallbackAmps10 > rectifierModels[next.model].ratedAmps * 10)) {
        error = T("Huawei ohne CAN: Spannung ab 45 V und ein Strom bis zum Nennstrom eintragen.",
                  "Huawei without CAN: enter a voltage from 45 V and a current up to the rating.");
    } else if (!baseOK || mqttUser.length() > 32 || mqttPass.length() > 64 || meterTopic.length() > 127 ||
               meterPath.length() > 95) {
        error = T("MQTT prüfen: Topic-Präfix ohne + # und Leerzeichen, Texte nicht zu lang.",
                  "Check MQTT: topic prefix without + # and spaces, texts not too long.");
    } else if (next.meterSource == 1 && (!mqttHost.length() || !meterTopic.length())) {
        error = T("Für die Messung per MQTT sind Broker und Topic nötig.", "Measuring via MQTT needs a broker and a topic.");
    }
    if (!error) error = devsetup::setApPassword(web.hasArg("apPwOn"), web.arg("apPw"));
    if (error) return sendMessage(400, T("Nicht gespeichert", "Not saved"), error, true);

    ssid.toCharArray(next.ssid, sizeof(next.ssid));
    if (password.length() || ssid != cfg.ssid) password.toCharArray(next.password, sizeof(next.password));
    host.toCharArray(next.meter, sizeof(next.meter));
    soyoHost.toCharArray(next.soyoMeter, sizeof(next.soyoMeter));
    mqttHost.toCharArray(next.mqttHost, sizeof(next.mqttHost));
    mqttUser.toCharArray(next.mqttUser, sizeof(next.mqttUser));
    if (mqttPass.length() || mqttHost != cfg.mqttHost) mqttPass.toCharArray(next.mqttPass, sizeof(next.mqttPass));
    mqttBase.toCharArray(next.mqttBase, sizeof(next.mqttBase));
    meterTopic.toCharArray(next.meterTopic, sizeof(next.meterTopic));
    meterPath.toCharArray(next.meterPath, sizeof(next.meterPath));
    stop(Mode::Idle, false);  // bis zum Neustart aus, gespeicherter Modus bleibt erhalten
    if (!saveConfig(next)) return sendMessage(500, T("Fehler", "Error"), T("Speichern fehlgeschlagen.", "Saving failed."), true);
    uiLang = next.lang;
    sendMessage(200, T("Gespeichert", "Saved"),
                T("Das Gerät startet neu. Die neue Adresse steht danach auf dem Display unter „Einstellungen“.",
                  "The device restarts. Afterwards the new address is shown on the display under “Settings”."),
                false);
    rebootAt = millis() + 1500;
}

// ---------------------------------------------------------------- Live-Werte, WLAN-Suche
void handleStatus() {
    const bool gridValid = meterOK();
    const bool soyoValid = cfg.soyoMeter[0] && soyoMeter.valid && recent(soyoMeter.time, meterMaxAge);
    const float dc = cfg.huaweiCount ? huaweiPower(true) : NAN;
    String j = String("{\"grid\":") + (gridValid ? String(meter.watts) : "null") +
               ",\"soyo\":" + (soyoValid ? String(soyoMeter.watts) : String(cfg.soyoCount ? dispatch.soyo : 0)) +
               ",\"huawei\":" + (isfinite(dc) ? String(int(dc)) : "null") + ",\"mode\":\"" +
               (mode == Mode::Zero ? "zero" : mode == Mode::Manual ? "charge" : "off") + "\",\"chips\":[";
    const bool online = WiFi.status() == WL_CONNECTED;
    j += String("[\"") + T("WLAN", "Wi-Fi") + (online ? String(" ") + WiFi.RSSI() + " dBm" : String("")) + "\"," +
         (online ? "true" : "false") + "]";
    String meterChip = T("Netzmessung", "Grid meter");
    if (cfg.meterSource == 0 && meterError[0] != MeterOk) meterChip += String(": ") + meterErrorText(0);
    else if (!gridValid) meterChip += String(": ") + T("kein Wert", "no value");
    j += String(",[\"") + meterChip + "\"," + (gridValid ? "true" : "false") + "]";
    if (cfg.huaweiCount) j += String(",[\"Huawei CAN\",") + (huaweiHealthy() ? "true" : "false") + "]";
    if (cfg.mqttHost[0]) j += String(",[\"MQTT\",") + (mqttOnline ? "true" : "false") + "]";
    j += "]}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", j);
}

// ---------------------------------------------------------------- WLAN-Suche
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


// ---------------------------------------------------------------- Captive Portal und Routen
void toPortal() {
    web.sendHeader("Location", String("http://") + devsetup::AP_IP.toString() + "/", true);
    web.send(302, "text/plain", "");
}

void webBegin() {
    devsetup::begin(web);
    // Erkennungsadressen der Betriebssysteme (Android, Apple, Windows, Firefox) -> Einstellungsseite
    for (const char *url : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                            "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html",
                            "/success.txt"}) {
        web.on(url, HTTP_GET, toPortal);
    }
    web.on("/", HTTP_GET, [] {
        if (portal && web.hostHeader() != devsetup::AP_IP.toString()) return toPortal();
        if (!devsetup::authed()) return pageAuth(nullptr);
        pageSettings();
    });
    web.on("/login", HTTP_POST, handleLogin);
    web.on("/setpw", HTTP_POST, handleSetPassword);
    web.on("/logout", HTTP_POST, [] {
        devsetup::logout();
        redirectHome();
    });
    // Sprache: Weboberflaeche sofort, feste Texte am Display nach dem naechsten Neustart
    web.on("/lang", HTTP_GET, [] {
        int lang;
        if (readInt("l", lang, 0, 1) && lang != cfg.lang) {
            Config next = cfg;
            next.lang = lang;
            if (saveConfig(next)) {
                cfg.lang = lang;
                uiLang = lang;
            }
        }
        redirectHome();
    });
    web.on("/save", HTTP_POST, [] { if (guard()) handleSave(); });
    web.on("/scan", HTTP_GET, [] { if (guard()) handleScan(); });
    web.on("/api/status", HTTP_GET, [] { if (guard()) handleStatus(); });
    web.on("/calibrate", HTTP_POST, [] {
        if (!guard()) return;
        calibrateRequested = true;
        sendMessage(200, T("Touch kalibrieren", "Calibrate touch"),
                    T("Am Display die Pfeilspitzen nacheinander antippen.", "Tap the arrow tips on the display one after another."),
                    true);
    });
    web.onNotFound([] {
        if (portal) return toPortal();
        redirectHome();
    });
    web.begin();
}
