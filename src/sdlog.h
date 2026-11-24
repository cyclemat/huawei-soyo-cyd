// Log auf der microSD-Karte des CYD (Kartenslot am VSPI: SCK 18, MISO 19, MOSI 23, CS 5)
//
// Laeuft in einem eigenen Task (ein langsamer Kartenzugriff bremst nie die Regelung). Steckt eine Karte und ist
// der Log eingeschaltet (Weboberflaeche, ab Werk an), schreibt er:
//   - alle 10 s eine Zustandszeile (Modus, Netz, Soyo, Huawei, Restbezug),
//   - Ereignisse (Moduswechsel, Messausfall, CAN weg, WLAN weg, Neustart …) sofort in den Puffer.
// Dateien /logs/log-NNNNN.csv (Semikolon-getrennt, je hoechstens 1 MB). Ist weniger als 8 MB frei, werden die
// aeltesten Dateien geloescht. Eine spaeter eingesteckte Karte wird innerhalb von 30 s erkannt.
#pragma once
#include <Arduino.h>
#include <vector>

struct SdLogFile {
    String name;
    size_t size;
};

struct SdLogInfo {
    bool mounted = false;
    uint64_t totalBytes = 0, freeBytes = 0;
    String current;
};

void sdlogBegin();                         // Task starten (nur wenn cfg.sdLog)
void sdlogEvent(const char *text);         // Ereigniszeile (aus jedem Task erlaubt)
SdLogInfo sdlogInfo();
std::vector<SdLogFile> sdlogFiles();       // neueste zuerst
bool sdlogValidName(const String &name);   // nur log-NNNNN.csv (Schutz vor Pfadangaben)
bool sdlogSend(const String &name);        // Datei an die Weboberflaeche senden
void sdlogDeleteAll();                     // alle ausser der aktuellen
