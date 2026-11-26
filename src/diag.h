// Diagnose ueber das Netz (Weboberflaeche -> Geraet -> Diagnose), weil der USB-Seriell dem Soyo-RS485 gehoert.
//  - Ereignisse (dieselben wie im SD-Log) zusaetzlich in einem kleinen RAM-Ring, auch ohne SD-Karte
//  - CAN-Zaehler: empfangene Frames je Huawei, fremde Adressen, Sendefehler, Bus-Off
//  - CAN-Mitschnitt nur auf Anfrage: die naechsten 64 Frames (TX und RX), hoechstens 10 s, Puffer nur dann
//  - Textbericht (/diag.txt) mit allem zusammen, zum Herunterladen und Weitergeben
#pragma once
#include <Arduino.h>

struct CanStats {
    uint32_t rx;             // empfangene Frames gesamt
    uint32_t rxUnit[3];      // davon je eingetragenem Huawei
    uint32_t rxForeign;      // Frames, die zu keinem eingetragenen Huawei passen
    uint32_t lastForeignId;  // letzte fremde CAN-ID
    uint32_t lastForeignAt;  // millis() dazu (0 = nie)
    uint32_t txFail;         // Sendeversuche, die der Treiber abgelehnt hat
    uint32_t busOff;         // Bus-Off-Ereignisse (Treiber-Neustarts)
};
extern CanStats canStats;

void diagBegin();                                  // vor dem ersten Ereignis aufrufen (setup)
void diagEvent(const char *text);                  // Ereigniszeile mit Zeitstempel (aus jedem Task erlaubt)
String diagEvents();                               // alle Zeilen, aelteste zuerst
void diagCanFrame(bool tx, uint32_t id, const uint8_t *data, uint8_t dlc);  // in den Mitschnitt, wenn er laeuft
bool diagCaptureStart();                           // Mitschnitt starten (false = kein Speicher)
bool diagCaptureActive();
String diagCaptureJson();                          // [[ms,"TX"|"RX","1081407F","01 70 ..."], ...]
String diagCaptureText();                          // dasselbe als Textzeilen
String diagReport();                               // vollstaendiger Textbericht
