// Shelly-Schalter vor den Huawei-Netzteilen (optional, je Netzteil einer, z. B. Shelly Plug oder 1PM).
// Nur aktiv, wenn eine IP-Adresse eingetragen ist – sonst passiert nichts.
//  - misst die Aufnahme jedes Huawei (sperrt den Soyo, solange ein Huawei Strom zieht)
//  - schaltet einen Huawei ab, wenn sein CAN abreisst oder er Strom zieht, obwohl er aus sein soll
//  - neuer Versuch nach 5 min, und bei jedem Neustart wird wieder eingeschaltet
#pragma once
#include <Arduino.h>

struct HwPlug {
    volatile bool valid;        // Shelly hat zuletzt geantwortet
    volatile bool on;           // Schalter ist an
    volatile int watts;         // gemessene Aufnahme
    volatile uint32_t time;     // Zeitpunkt der letzten Antwort
    volatile uint32_t onSince;  // seit wann der Schalter an ist (Startzeit des Huawei)
    volatile uint8_t error;     // MeterError
    volatile bool want;         // gewuenschter Zustand
    uint32_t cutAt;             // abgeschaltet um (0 = nicht abgeschaltet)
    uint8_t cutReason;          // 1 = CAN fehlt, 2 = Aufnahme obwohl aus
};

extern HwPlug hwPlug[3];

bool hwPlugUsed(int i);         // Shelly fuer Huawei i eingetragen
void hwPlugBegin();             // Abfrage-Task starten (nur wenn mindestens einer eingetragen)
void hwPlugLoop();              // Abschalt-Logik, aus loop()
bool hwPlugDrawing();           // ein Huawei nimmt laut Shelly mehr als 40 W auf
String hwPlugText(int i);       // kurzer Zustand fuer Display/Weboberflaeche
