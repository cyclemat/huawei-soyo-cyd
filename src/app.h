// Gemeinsame Einstellungen, Messwerte und Funktionen aller Module
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "dispatch.h"
#include "i18n.h"

#define FW_VERSION "0.3.0-beta"

struct Config {
    char ssid[33]{}, password[65]{}, meter[64]{};
    char soyoMeter[64]{};
    int soyoChannel = 0;
    int soyoPolarity = -1;
    int channel = -1;
    int polarity = 1;
    int soyoCount = 0;
    int soyoUnit = 0;
    int huaweiCount = 0;
    int volts10 = 0;
    int amps10 = 0;
    int ampScale10 = 0;   // nur fuer Modell 0 ("Eigener Faktor"), Faktor x 10
    int reserve = 25;
    int model = 3;        // Index in rectifierModels, Standard R4875G1
    // Verhalten des Huawei, wenn keine CAN-Befehle mehr vom CYD kommen
    // (CYD aus, Kabel ab). Die Werte speichert der Huawei selbst (Offline-Werte).
    int fallbackMode = 0;     // 0 = Laden stoppen (0 A), 1 = mit festen Werten weiterladen
    int fallbackVolts10 = 0;  // Offline-Ladespannung in 0,1 V
    int fallbackAmps10 = 0;   // Offline-Strom je Geraet in 0,1 A
    // Netzmessung: Shelly per HTTP oder ein beliebiges MQTT-Abo mit JSONPath
    int meterSource = 0;          // 0 = Shelly, 1 = MQTT
    char meterTopic[128]{};       // z. B. tele/stromzaehler/SENSOR
    char meterPath[96]{};         // z. B. $.ENERGY.Power, leer = Nachricht ist nur eine Zahl
    int meterTimeout = 15;        // s, aeltere MQTT-Werte gelten als ausgefallen
    // MQTT-Broker (fuer Abo und Home-Assistant-Discovery)
    char mqttHost[64]{};
    int mqttPort = 1883;
    char mqttUser[33]{}, mqttPass[65]{};
    char mqttBase[33] = "huaweisoyo";
    int haDiscovery = 1;
    int lang = 0;                 // 0 = Deutsch, 1 = English
    int screenOffMin = 60;        // Display nach so vielen Minuten ohne Beruehrung aus, 0 = nie
    int ldrWake = 1;              // Display bei starkem Helligkeitsanstieg (Lichtsensor) wieder an
    int sdLog = 1;                // Log auf der SD-Karte, wenn eine steckt
    int ldrRise = 40;             // Aufwachen, wenn der Lichtsensor um so viel % heller misst (10-90)
    // Shelly-Schalter vor jedem Huawei (optional, leer = keiner): misst die Aufnahme und schaltet
    // den Huawei ab, wenn sein CAN abreisst
    char hwShelly[3][64]{};
    int hwShellyCut = 1;          // bei CAN-Ausfall abschalten
};

// Huawei-Gleichrichter: Der CAN-Stromwert ist Ampere x (1024 / Nennstrom),
// wie in craigpeacock/Huawei_R4850G2_CAN und mjpalmowski/CAN-BUS-control-R4875G1.
struct RectifierModel { const char *name; int ratedAmps; };
extern const RectifierModel rectifierModels[];
extern const int modelCount;
float ampFactor(const Config &c);

struct Sample { int watts; uint32_t time; bool valid; };
struct Rectifier {
    float dc, ac, temp;                  // Ausgangsleistung, Eingangsleistung, Temperatur
    float outV, outA, inV, inA, eff;     // weitere Messwerte fuer MQTT / Home Assistant
    uint32_t dcAt, acAt, tempAt;
    bool fallbackSent;
};

extern Config cfg;
extern Dispatch dispatch;
extern Mode mode;
extern Preferences prefs;
extern WebServer web;
extern QueueHandle_t readings, soyoReadings;
extern bool portal, canReady;
extern uint32_t lastControl, rebootAt;
extern int manual, selection;
extern Sample meter, soyoMeter;
extern Rectifier rectifier[3];
extern const char *notice;           // Hinweis fuer Display (z. B. warum Laden gestoppt hat)
extern bool redraw;                  // Display sofort aktualisieren
extern volatile bool calibrateRequested;
extern volatile bool mqttOnline;

// Maximales Alter eines Shelly-Messwerts. Einzelne Aussetzer (WLAN, langsamer Shelly) sollen die
// Regelung nicht abschalten; sie haelt so lange den letzten Sollwert.
constexpr uint32_t meterMaxAge = 10000;

// Zustand der Shelly-Abfrage: [0] = Netz, [1] = Soyo-Istwert
enum MeterError : uint8_t { MeterOk, MeterNoWifi, MeterNoHost, MeterTimeout, MeterAuth, MeterHttp, MeterFormat };
extern volatile uint8_t meterError[2];
extern volatile int meterHttpCode[2];
const char *meterErrorText(int which);

bool recent(uint32_t stamp, uint32_t age);
void stop(Mode next, bool remember = true);
bool huaweiHealthy();
float huaweiPower(bool dc);
uint32_t gridMaxAge();
bool meterOK();
bool soyoIsOff();
int maxCharge();
bool saveConfig(const Config &c);
String deviceSuffix();               // letzte 4 Stellen der MAC, z. B. "A1B2"
void serviceOutputs();               // CAN lesen und Soyo/Huawei weiter versorgen, wenn loop() laenger blockiert

// ui.cpp
void displayBegin();                 // Display, Touch, Kalibrierung beim Start
void uiInit();
void uiUpdate();
void uiTick();
void uiShowSettings();
void splashProgress(int pct, const char *text);   // Startbild: Balken 0-100 % und Statuszeile (nullptr = Text lassen)
void splashEnd();                                 // setup() fertig: Startbild darf weg
void touchCalibrationRun();
int ldrNow();                     // aktueller Messwert des Lichtsensors (0-4095, gross = dunkel)

// web.cpp
void webBegin();
void collectScan(int count);

// mqtt.cpp
void mqttBegin();
