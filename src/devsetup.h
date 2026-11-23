// Ersteinrichtung und Zugang (uebernommen aus Simple Open Camper, src/devsetup.*, angepasst an Arduino 2.x)
//
//  - Einrichtungs-WLAN "HuaweiSoyo-XXXX" auf 4.3.2.1 mit Captive Portal (wie WLED): Android zeigt die
//    Anmeldeseite nicht, wenn seine Pruefadresse auf eine private Adresse (192.168.x.x) zeigt.
//    Ein kleiner DNS-Server leitet jede Adresse hierher, solange das Einrichtungs-WLAN an ist.
//  - Zwei getrennte Passwoerter:
//    Zugangspasswort (Weboberflaeche): ab Werk "panelsat", beim ersten Oeffnen muss ein eigenes vergeben
//    werden (Cookie-Anmeldung, 5 Fehlversuche -> 30 s Pause).
//    WLAN-Passwort des Einrichtungs-WLANs: ab Werk an mit "panelsat", aenderbar oder abschaltbar.
//  - Mehrfach-Reset: schnell hintereinander aus/ein, jeweils unter 10 s gelaufen.
//    3x = Einrichtungs-WLAN sofort an (bis zum Neustart), 5x = beide Passwoerter auf Werkseinstellung.
//    Anders als bei Simple Open Camper bleibt das Heim-WLAN erhalten: ein flackerndes Netz soll die
//    Nulleinspeisung nicht dauerhaft lahmlegen.
// NVS-Namensraum "devsetup": apw, pwset, appw, apon, bootc.
#pragma once
#include <Arduino.h>
#include <WebServer.h>

#define DEVSETUP_DEFAULT_PW "panelsat"

namespace devsetup {

struct Boot {
    uint8_t count = 0;
    bool forceAp = false;    // Einrichtungs-WLAN sofort an und anlassen
    bool reset = false;      // Passwoerter wurden zurueckgesetzt
};

extern const IPAddress AP_IP;

Boot bootCheck();                          // ganz am Anfang von setup()
void begin(WebServer &srv);                // Cookie-Header anmelden
void loop();                               // DNS, Zaehler zuruecksetzen, Einrichtungs-WLAN nach Aenderung neu

void apStart(const String &ssid);          // Einrichtungs-WLAN an (Modus vorher setzen), DNS-Server an
bool apActive();
const String &apSsid();
bool apPasswordOn();                       // false = Einrichtungs-WLAN offen
bool apPasswordDefault();                  // true = noch "panelsat"
const char *setApPassword(bool on, const String &pw);   // nullptr = ok, sonst Fehlertext

bool authed();                             // angemeldeter Browser?
bool passwordSet();                        // false = Zugangspasswort ist noch das Standardpasswort
const char *login(const String &pw);       // nullptr = ok (Cookie gesetzt), sonst Fehlertext
const char *setPassword(const String &oldPw, const String &newPw);
void logout();
void passwordReset();                      // beide Passwoerter zurueck auf "panelsat"

}  // namespace devsetup
