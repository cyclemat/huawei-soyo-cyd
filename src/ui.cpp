// Display und Touch (ESP32-2432S028R) mit LovyanGFX und LVGL 8.4, Design wie Simple Open Camper
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "app.h"
#include "devsetup.h"
#include "fonts/fonts.h"
#include "fonts/icons.h"
#include "hwplug.h"
#include "generated/splash_jpg.h"

// ---- Display ESP32-2432S028R (gleiche Einstellungen wie Simply Open Camper, sat_display.cpp) ----
// Display ILI9341 an HSPI (SCK 14, MOSI 13, MISO 12, CS 15, DC 2), Licht GPIO 21.
// Touch XPT2046 an VSPI (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36).
#define DISPLAY_ROTATION 1   // quer; steht das Bild kopf: 3 (Touch dreht automatisch mit)
class CydDisplay : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
    lgfx::Touch_XPT2046 touch_;

public:
    CydDisplay() {
        {
            auto c = bus_.config();
            c.spi_host = SPI2_HOST;  // HSPI
            c.spi_mode = 0;
            c.freq_write = 40000000;
            c.freq_read = 16000000;
            c.spi_3wire = false;
            c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = 14;
            c.pin_mosi = 13;
            c.pin_miso = 12;
            c.pin_dc = 2;
            bus_.config(c);
            panel_.setBus(&bus_);
        }
        {
            auto c = panel_.config();
            c.pin_cs = 15;
            c.pin_rst = -1;
            c.pin_busy = -1;
            c.panel_width = 240;
            c.panel_height = 320;
            c.offset_rotation = 0;
            c.readable = true;
            c.invert = false;
            c.rgb_order = false;
            c.dlen_16bit = false;
            c.bus_shared = false;
            panel_.config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = 21;
            c.invert = false;
            c.freq = 12000;
            c.pwm_channel = 7;
            light_.config(c);
            panel_.setLight(&light_);
        }
        {
            auto c = touch_.config();
            // Rohwerte an den Raendern (am echten Board ermittelt, Y vertauscht).
            // Genauer wird es mit der Kalibrierung (beim Einschalten 2 s auf den Bildschirm druecken).
            c.x_min = 300;
            c.x_max = 3900;
            c.y_min = 3700;
            c.y_max = 200;
            c.pin_int = 36;
            c.bus_shared = false;
            c.offset_rotation = 0;
            // Software-SPI (Bus -1): der Hardware-Bus VSPI bleibt frei fuer die SD-Karte (18/19/23/5)
            c.spi_host = -1;
            c.freq = 1000000;
            c.pin_sclk = 25;
            c.pin_mosi = 32;
            c.pin_miso = 39;
            c.pin_cs = 33;
            touch_.config(c);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
CydDisplay lcd;

// ---- Display-Schlaf ----
// Nach cfg.screenOffMin Minuten ohne Beruehrung geht die Beleuchtung aus. Wieder an: antippen (die erste
// Beruehrung weckt nur und loest keine Taste aus) oder starker Helligkeitsanstieg am Lichtsensor (GPIO34,
// wie Simple Open Camper: grosser Messwert = dunkel).
#define LDR_PIN 34
uint32_t lastTouchMs = 0, ldrAt = 0, offSince = 0;
bool screenOff = false, swallowTouch = false;

// ---- Startbild ----
// assets/splash_cyd.jpg (tools/embed_splash.py) liegt im Flash und wird von LovyanGFX direkt gezeichnet.
// Waehrend setup() laeuft, zeigt ein Balken mit Statuszeile den Fortschritt (splashProgress). Solange das
// Bild steht, zeichnet LVGL nicht (flushDisplay) und Beruehrungen erreichen keine Taste. Nach splashEnd()
// bleibt es noch bis SPLASH_MIN_MS nach dem Start bzw. kurz fuer "Bereit" stehen, Antippen beendet es
// frueher. Danach wird der Hauptschirm komplett aufgebaut.
const uint32_t SPLASH_MIN_MS = 2500;
uint32_t splashUntil = 0;   // != 0: Startbild steht noch (fruehestes Ende)
bool splashHold = false;    // setup() laeuft noch: nicht beenden
// Balken und Textband im Bild (320x240)
const int BAR_X = 52, BAR_Y = 201, BAR_W = 216, BAR_H = 6, TXT_Y0 = 213, TXT_Y1 = 238;

// Balkenfarbe: Orange -> helle Mitte -> Blau, wie die Leiterbahnen im Bild
uint16_t splashBarColor(int i) {
    const int h = (BAR_W - 1) / 2;
    int r, g, b;
    if (i <= h) {
        r = 255 + (250 - 255) * i / h;
        g = 138 + (225 - 138) * i / h;
        b = 26 + (195 - 26) * i / h;
    } else {
        const int j = i - h, n = BAR_W - 1 - h;
        r = 250 + (47 - 250) * j / n;
        g = 225 + (157 - 225) * j / n;
        b = 195 + (255 - 195) * j / n;
    }
    return lcd.color565(r, g, b);
}

void splashProgress(int pct, const char *text) {
    if (!splashUntil) return;
    pct = constrain(pct, 0, 100);
    const int n = BAR_W * pct / 100;
    for (int i = 0; i < n; ++i) {
        const bool edge = i == 0 || i == BAR_W - 1;   // abgerundete Enden
        lcd.drawFastVLine(BAR_X + i, edge ? BAR_Y + 1 : BAR_Y, edge ? BAR_H - 2 : BAR_H, splashBarColor(i));
    }
    if (!text) return;
    // Textband aus dem Bild wiederherstellen (Ausschnitt des JPEG), dann Text mittig
    lcd.drawJpg(SPLASH_JPG, SPLASH_JPG_LEN, 0, TXT_Y0, 320, TXT_Y1 - TXT_Y0 + 1, 0, TXT_Y0);
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(lcd.color565(220, 232, 246));
    lcd.drawString(text, 160, (TXT_Y0 + TXT_Y1) / 2);
}

void splashEnd() {
    if (!splashUntil) return;
    splashHold = false;
    const uint32_t now = millis();
    if (int32_t(splashUntil - now) < 600) splashUntil = (now + 600) | 1;   // "Bereit" kurz zeigen
}
float ldrFast = -1, ldrBase = -1;

void screenWake() {
    if (!screenOff) return;
    screenOff = false;
    lcd.setBrightness(255);
    lastTouchMs = millis();
    if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
}

void screenLoop() {
    const uint32_t now = millis();
    if (!screenOff) {
        if (cfg.screenOffMin > 0 && now - lastTouchMs > cfg.screenOffMin * 60000UL) {
            screenOff = true;
            offSince = now;
            ldrFast = ldrBase = -1;
            lcd.setBrightness(0);
        }
        return;
    }
    if (!cfg.ldrWake || now - ldrAt < 250) return;
    ldrAt = now;
    const float raw = analogRead(LDR_PIN);
    ldrFast = ldrFast < 0 ? raw : ldrFast * 0.5f + raw * 0.5f;
    if (now - offSince < 5000) {   // erst einschwingen lassen, dann Grundwert merken
        ldrBase = ldrFast;
        return;
    }
    // Deutlich heller als der langsame Grundwert (Licht an, Tuer auf, Sonne): Display an.
    // cfg.ldrRise = noetiger Anstieg in % (kleiner = empfindlicher).
    const int rise = constrain(cfg.ldrRise, 10, 90);
    if (ldrFast < ldrBase * (100 - rise) / 100.0f && ldrBase - ldrFast > 40) {
        screenWake();
        return;
    }
    ldrBase = ldrBase * 0.98f + ldrFast * 0.02f;   // langsam nachfuehren (ca. 12 s)
}

int ldrNow() {
    return analogRead(LDR_PIN);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    lgfx::touch_point_t point;
    const bool down = !rebootAt && !splashUntil && lcd.getTouch(&point) > 0;
    if (down) {
        lastTouchMs = millis();
        if (screenOff) {
            screenWake();
            swallowTouch = true;   // diese Beruehrung nur zum Wecken
        }
    } else {
        swallowTouch = false;
    }
    if (down && !swallowTouch) {
        data->point.x = constrain(point.x, 0, 319);
        data->point.y = constrain(point.y, 0, 239);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ---- Touch kalibrieren (wie Simply Open Camper) ----
// LovyanGFX zeigt nacheinander Pfeile in den vier Ecken, die man antippt. Die 8 Werte
// landen im Flash ("tcal") und werden beim Start gesetzt.
// Ausloesen: beim Einschalten 2 s auf den Bildschirm druecken oder im Webinterface.
volatile bool calibrateRequested = false;

void touchCalibrationLoad() {
    uint16_t cal[8];
    if (prefs.getBytesLength("tcal") == sizeof(cal) && prefs.getBytes("tcal", cal, sizeof(cal)) == sizeof(cal)) {
        lcd.setTouchCalibrate(cal);
    }
}

void touchCalibrationRun() {
    screenWake();   // aus der Weboberflaeche ausgeloest, waehrend das Display schlaeft
    uint16_t cal[8];
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextSize(2);
    lcd.drawString(T("Touch kalibrieren", "Touch calibration"), 160, 106);
    lcd.setTextSize(1);
    lcd.drawString(T("Pfeilspitzen nacheinander antippen", "Tap the arrow tips one after another"), 160, 132);
    lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 16);
    prefs.putBytes("tcal", cal, sizeof(cal));
    lcd.setTouchCalibrate(cal);
    lcd.fillScreen(TFT_BLACK);
    if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
}

// Beim Einschalten gedrueckt halten: nach 2 s loslassen = Touch kalibrieren
void touchCalibrationAtBoot() {
    lgfx::touch_point_t point;
    const uint32_t start = millis();
    bool shown = false;
    while (lcd.getTouch(&point) > 0 && millis() - start < 10000) {
        if (!shown && millis() - start >= 2000) {
            shown = true;
            lcd.fillScreen(TFT_BLACK);
            lcd.setTextColor(TFT_WHITE, TFT_BLACK);
            lcd.setTextDatum(textdatum_t::middle_center);
            lcd.setTextSize(2);
            lcd.drawString(T("Jetzt loslassen", "Release now"), 160, 120);
        }
        delay(20);
    }
    if (millis() - start >= 2000) touchCalibrationRun();
}


// ============================================================================
// Oberflaeche (LVGL 8.4) im Stil von Simple Open Camper (src/ui/theme.h)
// ============================================================================
#define C_BG        lv_color_hex(0x0F1215)
#define C_BAR       lv_color_hex(0x0B0D10)
#define C_LINE      lv_color_hex(0x232A31)
#define C_TILE      lv_color_hex(0x181D22)
#define C_TILE_LINE lv_color_hex(0x272E36)
#define C_FIELD     lv_color_hex(0x20262D)
#define C_TEXT      lv_color_hex(0xE9EDF1)
#define C_TEXT2     lv_color_hex(0x8D97A2)
#define C_TEXT3     lv_color_hex(0x5E6873)
#define C_AMBER     lv_color_hex(0xF3A43B)
#define C_BLUE      lv_color_hex(0x3C8DFF)
#define C_RED       lv_color_hex(0xFF6A5C)
#define C_GREEN     lv_color_hex(0x5BD38A)
#define C_INK       lv_color_hex(0x1E1206)   // dunkle Schrift auf hellen Flaechen

#define F_SMALL  (&font_ui_16)
#define F_BODY   (&font_ui_20)
#define F_TITLE  (&font_ui_26)
#define F_BIG    (&font_ui_40)
#define F_ICON   (&font_icons_24)

#define NNBSP "\xE2\x80\xAF"   // schmales Leerzeichen zwischen Zahl und Einheit

enum Action {
    ActZero, ActManual, ActIdle, ActStart, ActStop, ActCancel,
    ActMinus, ActPlus, ActReserveMinus, ActReservePlus, ActSlider, ActHuaweiOpen, ActHuaweiClose,
    ActNav0, ActNav1, ActNav2
};

bool huaweiOpen = false;   // Detailseite mit den Werten jedes Huawei

bool redraw = true;
const char *notice = "";
bool chargeOpen = false;
int page = 0;

struct Hold { lv_obj_t *obj; lv_obj_t *bar; int action; uint32_t start; bool fired; } hold{};

struct Ui {
    lv_obj_t *pages[3], *navIcon[3], *navText[3];
    lv_obj_t *modeIcon, *modeText, *wifi, *can, *mqtt;
    lv_obj_t *soyoValue, *soyoTag, *soyoFill, *huaweiValue, *huaweiTag, *huaweiFill;
    lv_obj_t *gridIcon, *gridCaption, *gridValue, *gridStatus;
    lv_obj_t *netTitle, *netAddress, *netHint, *reserveValue;
    lv_obj_t *btnMode[3], *modeLabel[3], *modeGlyph[3], *barManual;
    lv_obj_t *charge, *chargeMax, *chargeSelect, *chargeValue, *chargeInfo, *slider, *chargeNotice;
    lv_obj_t *btnStart, *startLabel, *startHint, *barStart;
    lv_obj_t *huawei, *hwSummary, *hwHead[3], *hwLine1[3], *hwLine2[3], *hwTile[3], *hwPlug[3], *hwNone;
} ui{};

// ---- kleine Helfer, die nur bei echter Aenderung neu zeichnen ----
void setText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void setTextColor(lv_obj_t *obj, lv_color_t color) {
    if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full != color.full) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
}

void setBg(lv_obj_t *obj, lv_color_t color) {
    if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full != color.full) {
        lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    }
}

void setVisible(lv_obj_t *obj, bool visible) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == visible) {
        if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Fuellstand als farbiger Hintergrund von unten
void setFill(lv_obj_t *fill, int value, int maximum, int tileHeight) {
    const int h = maximum > 0 ? constrain(value, 0, maximum) * tileHeight / maximum : 0;
    if (lv_obj_get_style_height(fill, LV_PART_MAIN) != h) lv_obj_set_height(fill, h);
}

// ---- Bausteine ----
void plain(lv_obj_t *o) {
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *makeTile(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *tile = lv_obj_create(parent);
    plain(tile);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_bg_color(tile, C_TILE, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tile, C_TILE_LINE, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_clip_corner(tile, true, 0);
    return tile;
}

lv_obj_t *makeBar(lv_obj_t *parent, lv_color_t color, int w, int h) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

void onEvent(lv_event_t *e);

lv_obj_t *makeButton(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, int action) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, onEvent, LV_EVENT_ALL, (void *)(intptr_t)action);
    return btn;
}

// Knopf mit Icon und/oder Text in der Mitte
lv_obj_t *makeTextButton(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, lv_color_t fg,
                         const char *icon, const char *text, int action, const lv_font_t *font = F_BODY) {
    lv_obj_t *btn = makeButton(parent, x, y, w, h, bg, action);
    lv_obj_t *row = lv_obj_create(btn);
    plain(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    if (icon) makeLabel(row, F_ICON, fg, icon);
    if (text) makeLabel(row, font, fg, text);
    lv_obj_center(row);
    return btn;
}

// ---- Aktionen ----
void showPage(int index) {
    page = index;
    for (int i = 0; i < 3; ++i) {
        setVisible(ui.pages[i], i == index);
        setTextColor(ui.navIcon[i], i == index ? C_AMBER : C_TEXT2);
        setTextColor(ui.navText[i], i == index ? C_AMBER : C_TEXT2);
    }
}

void uiShowSettings() { showPage(1); }

void saveReserve(int delta) {
    Config next = cfg;
    next.reserve = constrain(cfg.reserve + delta, 0, 200);
    if (saveConfig(next)) cfg.reserve = next.reserve;
}

// Hoechste waehlbare Ladeleistung in 100-W-Schritten (mindestens 100 W, damit die Anzeige stimmt)
int selectionLimit() { return max(100, maxCharge() / 100 * 100); }

// Warum "Nur Laden" nicht moeglich ist, wenn die Ladegrenze unter 100 W liegt
const char *chargeLimitReason() {
    if (!cfg.huaweiCount) return T("Kein Huawei eingetragen (Weboberfläche → Huawei)", "No Huawei configured (web → Huawei)");
    if (!cfg.volts10 || !cfg.amps10) {
        return T("Ladespannung oder Höchststrom fehlt (Weboberfläche → Huawei)",
                 "Charge voltage or max. current missing (web → Huawei)");
    }
    return T("Ladegrenze unter 100 W – Höchststrom erhöhen", "Charge limit below 100 W – raise max. current");
}

void runAction(int action) {
    switch (action) {
        case ActZero: stop(Mode::Zero); break;
        case ActIdle: stop(Mode::Idle); break;
        case ActManual: stop(Mode::Manual); notice = ""; chargeOpen = true; break;
        case ActStart:
            selection = min(selection, selectionLimit());
            if (!manual && meterOK() && huaweiHealthy() && soyoIsOff() && maxCharge() >= 100) {
                manual = selection;
                lastControl = millis() - 1000;
                notice = "";
            } else if (!manual) {
                if (maxCharge() < 100) notice = chargeLimitReason();
                else if (!meterOK()) notice = T("Keine Netzmessung", "No grid reading");
                else if (!huaweiHealthy()) notice = T("Huawei-CAN prüfen", "Check Huawei CAN");
                else notice = T("Warten, bis Soyo nicht mehr einspeist", "Waiting until Soyo stops feeding in");
            }
            break;
        case ActStop: stop(Mode::Manual); notice = ""; break;
        case ActCancel:
            stop(Mode::Idle);
            chargeOpen = false;
            notice = "";   // Hinweis aus dem Ladefenster nicht auf die Startseite mitnehmen
            showPage(2);
            break;
        case ActMinus: if (!manual) selection = max(100, selection - 100); break;
        case ActPlus: if (!manual) selection = min(selectionLimit(), selection + 100); break;
        case ActReserveMinus: saveReserve(-5); break;
        case ActReservePlus: saveReserve(5); break;
        case ActNav0: showPage(0); break;
        case ActNav1: showPage(1); break;
        case ActNav2: showPage(2); break;
        case ActHuaweiOpen: huaweiOpen = true; break;
        case ActHuaweiClose: huaweiOpen = false; break;
    }
    redraw = true;
}

void onEvent(lv_event_t *e) {
    const int action = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if (action == ActManual || action == ActStart) {
        // Lang druecken: Fortschritt wird in uiTick() angezeigt und ausgeloest.
        if (code == LV_EVENT_PRESSED) {
            hold = {target, action == ActManual ? ui.barManual : ui.barStart, action, millis(), false};
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            if (hold.bar) lv_bar_set_value(hold.bar, 0, LV_ANIM_OFF);
            hold = {};
        }
        return;
    }
    if (action == ActStop || action == ActCancel) {
        // Stopp und Abbrechen wirken sofort bei der ersten Beruehrung.
        if (code == LV_EVENT_PRESSED) runAction(action);
        return;
    }
    if (action == ActSlider) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            const int value = (lv_slider_get_value(target) + 50) / 100 * 100;
            lv_slider_set_value(target, value, LV_ANIM_OFF);
            if (!manual) selection = constrain(value, 100, 2000);
            redraw = true;
        }
        return;
    }
    if (action == ActHuaweiClose) {
        if (code == LV_EVENT_PRESSED) runAction(action);
        return;
    }
    if (action >= ActNav0) {
        if (code == LV_EVENT_PRESSED) runAction(action);   // Seitenwechsel ohne Verzoegerung
        return;
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) runAction(action);
}

// Fortschritt der Halte-Tasten (Nur Laden 1,2 s, Start 3 s)
void uiTick() {
    if (splashUntil) {
        if (splashHold) return;   // setup() laeuft noch
        lgfx::touch_point_t point;
        if (int32_t(millis() - splashUntil) < 0 && lcd.getTouch(&point) == 0) return;
        splashUntil = 0;
        lastTouchMs = millis();
        swallowTouch = true;                 // die Beruehrung beendet nur das Startbild
        lv_obj_invalidate(lv_scr_act());     // Hauptschirm jetzt komplett zeichnen
    }
    screenLoop();
    if (!hold.obj || hold.fired) return;
    const uint32_t need = hold.action == ActManual ? 1200 : 3000;
    const uint32_t elapsed = millis() - hold.start;
    lv_bar_set_value(hold.bar, min<uint32_t>(100, elapsed * 100 / need), LV_ANIM_OFF);
    if (elapsed >= need) {
        hold.fired = true;
        lv_bar_set_value(hold.bar, 0, LV_ANIM_OFF);
        runAction(hold.action);
    }
}

// ---- LVGL-Treiber ----
static lv_disp_draw_buf_t drawBuffer;
static lv_color_t pixels[320 * 20];

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    if (splashUntil) {   // Startbild steht: nichts zeichnen, LVGL haelt den Schirm fuer fertig
        lv_disp_flush_ready(driver);
        return;
    }
    const uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(colors), w * h);
    lcd.endWrite();
    lv_disp_flush_ready(driver);
}

void displayBegin() {
    for (int pin : {4, 16, 17}) {  // RGB-LED aus (Low = an)
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    lcd.init();
    lcd.setRotation(DISPLAY_ROTATION);
    lcd.setBrightness(255);
    lastTouchMs = millis();
    analogRead(LDR_PIN);                               // richtet den Pin als Analogeingang ein
    analogSetPinAttenuation(LDR_PIN, ADC_0db);         // empfindlichster Bereich (der LDR liefert wenig Spannung)
    touchCalibrationLoad();
    touchCalibrationAtBoot();
    if (SPLASH_JPG_LEN && lcd.drawJpg(SPLASH_JPG, SPLASH_JPG_LEN, 0, 0)) {
        splashUntil = (millis() + SPLASH_MIN_MS) | 1;
        splashHold = true;
        splashProgress(5, "v" FW_VERSION);
    }
}

// ---- Seiten ----
lv_obj_t *makePage(lv_obj_t *screen) {
    lv_obj_t *p = lv_obj_create(screen);
    plain(p);
    lv_obj_set_pos(p, 0, 28);
    lv_obj_set_size(p, 320, 168);
    return p;
}

void buildHeader(lv_obj_t *screen) {
    lv_obj_t *header = lv_obj_create(screen);
    plain(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 320, 28);
    lv_obj_set_style_bg_color(header, C_BAR, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_LINE, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    ui.modeIcon = makeLabel(header, F_ICON, C_TEXT2, ICON_POWER);
    lv_obj_align(ui.modeIcon, LV_ALIGN_LEFT_MID, 8, 0);
    ui.modeText = makeLabel(header, F_SMALL, C_TEXT2, "");
    lv_obj_align(ui.modeText, LV_ALIGN_LEFT_MID, 38, 0);
    ui.wifi = makeLabel(header, F_ICON, C_TEXT3, ICON_WIFI);
    lv_obj_align(ui.wifi, LV_ALIGN_RIGHT_MID, -8, 0);
    ui.can = makeLabel(header, F_SMALL, C_TEXT3, "CAN");
    lv_obj_align(ui.can, LV_ALIGN_RIGHT_MID, -40, 0);
    ui.mqtt = makeLabel(header, F_ICON, C_TEXT3, ICON_LINK);
    lv_obj_align(ui.mqtt, LV_ALIGN_RIGHT_MID, -82, 0);
}

void buildNav(lv_obj_t *screen) {
    lv_obj_t *nav = lv_obj_create(screen);
    plain(nav);
    lv_obj_set_pos(nav, 0, 196);
    lv_obj_set_size(nav, 320, 44);
    lv_obj_set_style_bg_color(nav, C_BAR, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(nav, C_LINE, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    const char *icons[] = {ICON_HOUSE, ICON_GEAR, ICON_LIST};
    const char *texts[] = {T("Start", "Home"), T("Einstellungen", "Settings"), T("Modus", "Mode")};
    // "Einstellungen" ist mit Icon breiter als ein Drittel: Mitte breiter, Start und Modus schmaler
    const int x[] = {0, 80, 220}, w[] = {80, 140, 100};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *btn = lv_obj_create(nav);
        plain(btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(btn, x[i], 0);
        lv_obj_set_size(btn, w[i], 44);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 5, 0);
        if (i == 2) lv_obj_set_style_pad_right(btn, 10, 0);   // "Modus" etwas weg vom rechten Rand
        lv_obj_add_event_cb(btn, onEvent, LV_EVENT_ALL, (void *)(intptr_t)(ActNav0 + i));
        ui.navIcon[i] = makeLabel(btn, F_ICON, C_TEXT2, icons[i]);
        ui.navText[i] = makeLabel(btn, F_SMALL, C_TEXT2, texts[i]);
    }
}

lv_obj_t *buildValueTile(lv_obj_t *parent, int x, const char *icon, lv_color_t color, const char *name,
                         lv_obj_t *&value, lv_obj_t *&tag, lv_obj_t *&fill) {
    lv_obj_t *tile = makeTile(parent, x, 6, 148, 108);
    fill = lv_obj_create(tile);
    plain(fill);
    lv_obj_set_size(fill, 148, 0);
    lv_obj_set_style_bg_color(fill, color, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_30, 0);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *glyph = makeLabel(tile, F_ICON, color, icon);
    lv_obj_set_pos(glyph, 12, 10);
    tag = makeLabel(tile, F_SMALL, C_TEXT2, "");
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_t *row = lv_obj_create(tile);
    plain(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_pos(row, 12, 38);
    value = makeLabel(row, F_BIG, C_TEXT, "--");
    makeLabel(row, F_BODY, C_TEXT2, "W");
    lv_obj_t *label = makeLabel(tile, F_SMALL, C_TEXT2, name);
    lv_obj_set_pos(label, 12, 84);
    return tile;
}

void buildHome(lv_obj_t *p) {
    buildValueTile(p, 8, ICON_LIGHTNING, C_AMBER, T("Einspeisung Soyo", "Soyo feed-in"),
                   ui.soyoValue, ui.soyoTag, ui.soyoFill);
    lv_obj_t *huaweiTile = buildValueTile(p, 164, ICON_BATTERY, C_GREEN, T("Ladung Huawei", "Huawei charging"),
                                          ui.huaweiValue, ui.huaweiTag, ui.huaweiFill);
    // Antippen: Detailseite mit den gemeldeten Werten jedes Netzteils
    lv_obj_add_flag(huaweiTile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(huaweiTile, onEvent, LV_EVENT_ALL, (void *)(intptr_t)ActHuaweiOpen);
    lv_obj_t *grid = makeTile(p, 8, 120, 304, 46);
    ui.gridIcon = makeLabel(grid, F_ICON, C_TEXT2, ICON_PLUG);
    lv_obj_align(ui.gridIcon, LV_ALIGN_LEFT_MID, 12, 0);
    ui.gridCaption = makeLabel(grid, F_SMALL, C_TEXT2, "");
    lv_obj_set_pos(ui.gridCaption, 46, 3);
    ui.gridValue = makeLabel(grid, F_BODY, C_TEXT, "--");
    lv_obj_set_pos(ui.gridValue, 46, 20);
    ui.gridStatus = makeLabel(grid, F_SMALL, C_TEXT2, "");
    lv_obj_set_width(ui.gridStatus, 140);
    lv_obj_set_style_text_align(ui.gridStatus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(ui.gridStatus, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.gridStatus, LV_ALIGN_RIGHT_MID, -12, 0);
}

void buildSettings(lv_obj_t *p) {
    lv_obj_t *net = makeTile(p, 8, 6, 304, 80);
    ui.netTitle = makeLabel(net, F_SMALL, C_AMBER, "");
    lv_obj_set_pos(ui.netTitle, 14, 8);
    ui.netAddress = makeLabel(net, F_TITLE, C_TEXT, "");
    lv_obj_set_pos(ui.netAddress, 14, 27);
    ui.netHint = makeLabel(net, F_SMALL, C_TEXT2, "");
    lv_obj_set_pos(ui.netHint, 14, 57);

    lv_obj_t *reserve = makeTile(p, 8, 92, 304, 74);
    lv_obj_t *caption = makeLabel(reserve, F_SMALL, C_TEXT2, T("Restbezug (Ziel am Netz)", "Grid target"));
    lv_obj_set_pos(caption, 14, 6);
    makeTextButton(reserve, 12, 28, 60, 38, C_FIELD, C_TEXT, nullptr, LV_SYMBOL_MINUS, ActReserveMinus);
    makeTextButton(reserve, 230, 28, 60, 38, C_FIELD, C_TEXT, nullptr, LV_SYMBOL_PLUS, ActReservePlus);
    ui.reserveValue = makeLabel(reserve, F_BODY, C_TEXT, "");
    lv_obj_align(ui.reserveValue, LV_ALIGN_TOP_MID, 0, 36);
}

void buildModeButton(lv_obj_t *p, int index, int y, int h, const char *icon, const char *text,
                     const char *hint, int action) {
    lv_obj_t *btn = makeButton(p, 8, y, 304, h, C_TILE, action);
    lv_obj_set_style_border_color(btn, C_TILE_LINE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    ui.btnMode[index] = btn;
    ui.modeGlyph[index] = makeLabel(btn, F_ICON, C_TEXT, icon);
    lv_obj_align(ui.modeGlyph[index], LV_ALIGN_LEFT_MID, 16, 0);
    ui.modeLabel[index] = makeLabel(btn, F_BODY, C_TEXT, text);
    lv_obj_align(ui.modeLabel[index], LV_ALIGN_LEFT_MID, 50, 0);
    if (hint) {
        lv_obj_t *h2 = makeLabel(btn, F_SMALL, C_TEXT2, hint);
        lv_obj_align(h2, LV_ALIGN_RIGHT_MID, -16, 0);
    }
}

void buildModes(lv_obj_t *p) {
    buildModeButton(p, 0, 6, 50, ICON_REFRESH, T("Nulleinspeisung", "Zero export"), nullptr, ActZero);
    buildModeButton(p, 1, 62, 52, ICON_BATTERY, T("Nur Laden", "Charge only"), T("lang halten", "hold"), ActManual);
    ui.barManual = makeBar(ui.btnMode[1], C_AMBER, 272, 4);
    lv_obj_align(ui.barManual, LV_ALIGN_BOTTOM_MID, 0, -3);
    buildModeButton(p, 2, 120, 46, ICON_POWER, T("Aus", "Off"), nullptr, ActIdle);
}

void buildCharge(lv_obj_t *screen) {
    ui.charge = lv_obj_create(screen);
    plain(ui.charge);
    lv_obj_add_flag(ui.charge, LV_OBJ_FLAG_CLICKABLE);   // Beruehrungen nicht an die Seite darunter
    lv_obj_set_pos(ui.charge, 0, 0);
    lv_obj_set_size(ui.charge, 320, 240);
    lv_obj_set_style_bg_color(ui.charge, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.charge, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.charge, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = makeLabel(ui.charge, F_TITLE, C_TEXT, T("Nur Laden", "Charge only"));
    lv_obj_set_pos(title, 12, 8);
    ui.chargeMax = makeLabel(ui.charge, F_SMALL, C_TEXT2, "");
    lv_obj_set_pos(ui.chargeMax, 13, 40);
    makeTextButton(ui.charge, 188, 8, 124, 40, C_FIELD, C_TEXT, ICON_CLOSE, T("Abbrechen", "Cancel"), ActCancel, F_SMALL);

    lv_obj_t *card = makeTile(ui.charge, 8, 62, 304, 92);
    ui.chargeSelect = lv_obj_create(card);   // Auswahl 100-2000 W
    plain(ui.chargeSelect);
    lv_obj_set_size(ui.chargeSelect, 304, 92);
    makeTextButton(ui.chargeSelect, 10, 8, 54, 46, C_FIELD, C_TEXT, nullptr, LV_SYMBOL_MINUS, ActMinus);
    makeTextButton(ui.chargeSelect, 240, 8, 54, 46, C_FIELD, C_TEXT, nullptr, LV_SYMBOL_PLUS, ActPlus);
    ui.slider = lv_slider_create(ui.chargeSelect);
    lv_obj_set_size(ui.slider, 256, 8);
    lv_obj_set_pos(ui.slider, 24, 72);
    lv_slider_set_range(ui.slider, 100, 2000);
    lv_obj_set_style_bg_color(ui.slider, C_FIELD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.slider, C_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.slider, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(ui.slider, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(ui.slider, 14);
    lv_obj_add_event_cb(ui.slider, onEvent, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)ActSlider);
    ui.chargeValue = makeLabel(card, F_BIG, C_TEXT, "");
    lv_obj_align(ui.chargeValue, LV_ALIGN_TOP_MID, 0, 8);
    ui.chargeInfo = makeLabel(card, F_SMALL, C_TEXT2, "");
    lv_obj_align(ui.chargeInfo, LV_ALIGN_BOTTOM_MID, 0, -10);

    ui.chargeNotice = makeLabel(ui.charge, F_SMALL, C_AMBER, "");
    lv_obj_set_width(ui.chargeNotice, 304);
    lv_obj_set_style_text_align(ui.chargeNotice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.chargeNotice, 8, 157);

    ui.btnStart = makeButton(ui.charge, 8, 178, 148, 56, C_AMBER, ActStart);
    ui.startLabel = makeLabel(ui.btnStart, F_BODY, C_INK, T("START", "START"));
    lv_obj_align(ui.startLabel, LV_ALIGN_CENTER, 0, -8);
    ui.startHint = makeLabel(ui.btnStart, F_SMALL, C_INK, T("3 s halten", "hold 3 s"));
    lv_obj_align(ui.startHint, LV_ALIGN_CENTER, 0, 13);
    ui.barStart = makeBar(ui.btnStart, C_INK, 124, 4);
    lv_obj_align(ui.barStart, LV_ALIGN_BOTTOM_MID, 0, -3);
    makeTextButton(ui.charge, 164, 178, 148, 56, C_RED, C_INK, ICON_CLOSE, T("STOPP", "STOP"), ActStop);
}

// ---- Detailseite Huawei ----
void buildHuawei(lv_obj_t *screen) {
    ui.huawei = lv_obj_create(screen);
    plain(ui.huawei);
    lv_obj_add_flag(ui.huawei, LV_OBJ_FLAG_CLICKABLE);   // Beruehrungen nicht an die Seite darunter
    lv_obj_set_pos(ui.huawei, 0, 0);
    lv_obj_set_size(ui.huawei, 320, 240);
    lv_obj_set_style_bg_color(ui.huawei, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.huawei, LV_OPA_COVER, 0);
    lv_obj_add_flag(ui.huawei, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *icon = makeLabel(ui.huawei, F_ICON, C_GREEN, ICON_BATTERY);
    lv_obj_set_pos(icon, 12, 12);
    lv_obj_t *title = makeLabel(ui.huawei, F_TITLE, C_TEXT, "Huawei");
    lv_obj_set_pos(title, 42, 8);
    ui.hwSummary = makeLabel(ui.huawei, F_SMALL, C_TEXT2, "");
    lv_obj_set_pos(ui.hwSummary, 13, 40);
    makeTextButton(ui.huawei, 196, 8, 116, 40, C_FIELD, C_TEXT, ICON_CLOSE, T("Zurück", "Back"), ActHuaweiClose, F_SMALL);

    for (int i = 0; i < 3; ++i) {
        ui.hwTile[i] = makeTile(ui.huawei, 8, 64 + i * 59, 304, 55);
        ui.hwHead[i] = makeLabel(ui.hwTile[i], F_SMALL, C_TEXT, "");
        lv_obj_set_pos(ui.hwHead[i], 12, 2);
        ui.hwLine1[i] = makeLabel(ui.hwTile[i], F_SMALL, C_TEXT, "");
        lv_obj_set_pos(ui.hwLine1[i], 12, 18);
        ui.hwLine2[i] = makeLabel(ui.hwTile[i], F_SMALL, C_TEXT2, "");
        lv_obj_set_pos(ui.hwLine2[i], 12, 34);
        ui.hwPlug[i] = makeLabel(ui.hwTile[i], F_SMALL, C_TEXT2, "");   // Shelly vor dem Huawei (optional)
        lv_obj_align(ui.hwPlug[i], LV_ALIGN_TOP_RIGHT, -10, 2);
    }
    ui.hwNone = makeLabel(ui.huawei, F_BODY, C_TEXT2, T("Kein Huawei eingetragen.\nWeboberfläche → Huawei",
                                                       "No Huawei configured.\nWeb interface → Huawei"));
    lv_obj_set_pos(ui.hwNone, 14, 80);
}

// Zahl mit Komma (Deutsch) bzw. Punkt (Englisch)
String decimal(float v, int digits) {
    String s(v, digits);
    if (!uiLang) s.replace('.', ',');
    return s;
}

void updateHuawei() {
    setVisible(ui.huawei, huaweiOpen);
    if (!huaweiOpen) return;
    char text[96];
    const int count = cfg.huaweiCount;
    setVisible(ui.hwNone, count == 0);
    if (count) {
        snprintf(text, sizeof(text), "%s %d" NNBSP "W  ·  %s %s" NNBSP "V  ·  %s %s" NNBSP "A",
                 T("Soll", "Set"), dispatch.huawei, T("Ladespannung", "voltage"), decimal(cfg.volts10 / 10.0f, 1).c_str(),
                 T("max.", "max."), decimal(cfg.amps10 / 10.0f, 1).c_str());
    } else {
        text[0] = 0;
    }
    setText(ui.hwSummary, text);
    for (int i = 0; i < 3; ++i) {
        setVisible(ui.hwTile[i], i < count);
        if (i >= count) continue;
        const Rectifier &r = rectifier[i];
        const bool live = recent(r.acAt, 3000);
        snprintf(text, sizeof(text), "Huawei %d  ·  %s", i + 1,
                 live ? T("verbunden", "connected") : T("keine Daten", "no data"));
        setText(ui.hwHead[i], text);
        setTextColor(ui.hwHead[i], live ? C_GREEN : C_RED);
        setText(ui.hwPlug[i], hwPlugText(i).c_str());
        setTextColor(ui.hwPlug[i], hwPlug[i].cutAt ? C_RED : C_TEXT2);
        if (live) {
            snprintf(text, sizeof(text), "%s" NNBSP "V  ·  %s" NNBSP "A  ·  DC %d" NNBSP "W",
                     decimal(r.outV, 2).c_str(), decimal(r.outA, 1).c_str(), int(lroundf(r.dc)));
            setText(ui.hwLine1[i], text);
            snprintf(text, sizeof(text), "AC %d" NNBSP "W  ·  %s" NNBSP "V  ·  %s" NNBSP "°C  ·  %s" NNBSP "%%",
                     int(lroundf(r.ac)), decimal(r.inV, 0).c_str(), decimal(r.temp, 0).c_str(),
                     decimal(r.eff * 100, 1).c_str());
            setText(ui.hwLine2[i], text);
        } else {
            setText(ui.hwLine1[i], T("Antwortet nicht über CAN", "Not answering over CAN"));
            setText(ui.hwLine2[i], "");
        }
    }
}

void uiInit() {
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, sizeof(pixels) / sizeof(pixels[0]));
    static lv_disp_drv_t displayDriver;
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = 320;
    displayDriver.ver_res = 240;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.draw_buf = &drawBuffer;
    lv_disp_t *display = lv_disp_drv_register(&displayDriver);
    static lv_indev_drv_t touchDriver;
    lv_indev_drv_init(&touchDriver);
    touchDriver.type = LV_INDEV_TYPE_POINTER;
    touchDriver.read_cb = readTouch;
    lv_indev_drv_register(&touchDriver);
    lv_disp_set_theme(display, lv_theme_default_init(display, C_AMBER, C_BLUE, true, F_SMALL));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, C_BG, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    buildHeader(screen);
    for (int i = 0; i < 3; ++i) ui.pages[i] = makePage(screen);
    buildHome(ui.pages[0]);
    buildSettings(ui.pages[1]);
    buildModes(ui.pages[2]);
    buildNav(screen);
    buildCharge(screen);
    buildHuawei(screen);
    showPage(0);
}

// ---- Werte aktualisieren ----
void uiUpdate() {
    char text[80];
    const bool meterLive = meterOK();
    const bool huaweiOK = huaweiHealthy();

    // Kopfzeile
    const char *icon = ICON_POWER;
    lv_color_t color = C_TEXT2;
    if (rebootAt) {
        icon = ICON_REFRESH;
        setText(ui.modeText, T("NEUSTART …", "RESTARTING …"));
        color = C_AMBER;
    } else if (mode == Mode::Zero) {
        icon = meterLive ? ICON_REFRESH : ICON_WARNING;
        setText(ui.modeText, meterLive ? T("NULLEINSPEISUNG", "ZERO EXPORT") : T("WARTE AUF MESSWERT", "WAITING FOR METER"));
        color = meterLive ? C_AMBER : C_RED;
    } else if (mode == Mode::Manual) {
        icon = ICON_BATTERY;
        setText(ui.modeText, manual ? T("NUR LADEN AKTIV", "CHARGING") : T("NUR LADEN BEREIT", "CHARGE READY"));
        color = C_GREEN;
    } else {
        setText(ui.modeText, T("AUS", "OFF"));
    }
    setText(ui.modeIcon, icon);
    setTextColor(ui.modeIcon, color);
    setTextColor(ui.modeText, color);
    const bool online = WiFi.status() == WL_CONNECTED;
    setText(ui.wifi, online || portal ? ICON_WIFI : ICON_WIFI_OFF);
    setTextColor(ui.wifi, online ? C_GREEN : portal ? C_AMBER : C_RED);
    setVisible(ui.can, cfg.huaweiCount > 0);
    setTextColor(ui.can, huaweiOK ? C_GREEN : C_RED);
    setVisible(ui.mqtt, cfg.mqttHost[0] != 0);
    setTextColor(ui.mqtt, mqttOnline ? C_GREEN : C_RED);

    // Soyo
    if (!cfg.soyoCount) {
        setText(ui.soyoValue, "--");
        setText(ui.soyoTag, T("aus", "off"));
    } else if (cfg.soyoMeter[0]) {
        const bool valid = soyoMeter.valid && recent(soyoMeter.time, meterMaxAge);
        if (valid) snprintf(text, sizeof(text), "%d", max(0, soyoMeter.watts));
        setText(ui.soyoValue, valid ? text : "--");
        setText(ui.soyoTag, T("IST", "ACTUAL"));
    } else {
        snprintf(text, sizeof(text), "%d", dispatch.soyo / max(1, cfg.soyoCount) * cfg.soyoCount);
        setText(ui.soyoValue, text);
        setText(ui.soyoTag, T("SOLL", "SET"));
    }
    setFill(ui.soyoFill, dispatch.soyo, cfg.soyoCount * cfg.soyoUnit, 108);

    // Huawei
    const float dc = cfg.huaweiCount ? huaweiPower(true) : NAN;
    if (isfinite(dc)) snprintf(text, sizeof(text), "%d", int(lroundf(dc)));
    setText(ui.huaweiValue, isfinite(dc) ? text : "--");
    setText(ui.huaweiTag, !cfg.huaweiCount ? T("aus", "off") : huaweiOK ? "DC" : T("CAN fehlt", "no CAN"));
    setTextColor(ui.huaweiTag, cfg.huaweiCount && !huaweiOK ? C_RED : C_TEXT2);
    setFill(ui.huaweiFill, isfinite(dc) ? int(dc) : 0, maxCharge(), 108);

    // Netz
    if (meterLive) {
        snprintf(text, sizeof(text), "%+d" NNBSP "W", meter.watts);
        setText(ui.gridValue, text);
        setText(ui.gridCaption, meter.watts > 0 ? T("Netz · Bezug", "Grid · import")
                              : meter.watts < 0 ? T("Netz · Einspeisung", "Grid · export") : T("Netz", "Grid"));
        const lv_color_t c = meter.watts > 0 ? C_AMBER : meter.watts < 0 ? C_BLUE : C_TEXT;
        setTextColor(ui.gridValue, c);
        setTextColor(ui.gridIcon, c);
    } else {
        setText(ui.gridValue, T("keine Messung", "no reading"));
        setText(ui.gridCaption, T("Netz", "Grid"));
        setTextColor(ui.gridValue, C_RED);
        setTextColor(ui.gridIcon, C_RED);
    }
    if (notice[0]) {
        setText(ui.gridStatus, notice);
    } else if (!meterLive && cfg.meterSource == 0) {
        setText(ui.gridStatus, meterErrorText(0));   // warum gerade kein Wert kommt
    } else {
        snprintf(text, sizeof(text), "%s %d" NNBSP "W", T("Ziel", "Target"), cfg.reserve);
        setText(ui.gridStatus, text);
    }

    // Einstellungen
    if (portal) {
        setText(ui.netTitle, T("Einrichtungs-WLAN", "Setup Wi-Fi"));
        setText(ui.netAddress, devsetup::AP_IP.toString().c_str());
        snprintf(text, sizeof(text), "%s  ·  %s", devsetup::apSsid().c_str(),
                 !devsetup::apPasswordOn() ? T("offen", "open")
                 : devsetup::apPasswordDefault() ? "PW: " DEVSETUP_DEFAULT_PW : T("eigenes Passwort", "own password"));
        setText(ui.netHint, text);
    } else {
        setText(ui.netTitle, T("Weboberfläche", "Web interface"));
        setText(ui.netAddress, WiFi.localIP().toString().c_str());
        snprintf(text, sizeof(text), "%s  ·  v" FW_VERSION, T("im Browser öffnen", "open in a browser"));
        setText(ui.netHint, text);
    }
    snprintf(text, sizeof(text), "%d" NNBSP "W", cfg.reserve);
    setText(ui.reserveValue, text);

    // Modus: aktive Taste in Bernstein mit dunkler Schrift
    const Mode modes[] = {Mode::Zero, Mode::Manual, Mode::Idle};
    for (int i = 0; i < 3; ++i) {
        const bool active = mode == modes[i];
        setBg(ui.btnMode[i], active ? C_AMBER : C_TILE);
        setTextColor(ui.modeLabel[i], active ? C_INK : C_TEXT);
        setTextColor(ui.modeGlyph[i], active ? C_INK : C_TEXT);
    }

    updateHuawei();

    // Nur Laden
    if (chargeOpen && mode != Mode::Manual) chargeOpen = false;  // z. B. nach Speichern im Web
    setVisible(ui.charge, chargeOpen);
    if (chargeOpen) {
        snprintf(text, sizeof(text), "%s %d" NNBSP "W", T("höchstens", "max."), maxCharge());
        setText(ui.chargeMax, text);
        setVisible(ui.chargeSelect, !manual);
        snprintf(text, sizeof(text), "%d" NNBSP "W", manual ? manual : selection);
        setText(ui.chargeValue, text);
        setTextColor(ui.chargeValue, manual ? C_GREEN : C_TEXT);
        // Schieberegler nur bis zur echten Ladegrenze (Huawei-Anzahl x Spannung x Strom, hoechstens 2000 W)
        const int limit = selectionLimit();
        if (!manual && selection > limit) selection = limit;
        if (lv_slider_get_max_value(ui.slider) != max(limit, 101)) lv_slider_set_range(ui.slider, 100, max(limit, 101));
        if (lv_slider_get_value(ui.slider) != selection) lv_slider_set_value(ui.slider, selection, LV_ANIM_OFF);
        if (!manual && maxCharge() < 100 && !notice[0]) notice = chargeLimitReason();
        if (manual) {
            if (dispatch.huawei && isfinite(dc)) {
                snprintf(text, sizeof(text), "%s %d" NNBSP "W", T("lädt – Huawei DC", "charging – Huawei DC"), int(lroundf(dc)));
            } else {
                snprintf(text, sizeof(text), "%s", dispatch.huawei ? T("lädt …", "charging …") : T("wartet auf Freigabe …", "waiting for release …"));
            }
            setText(ui.chargeInfo, text);
        }
        setVisible(ui.chargeInfo, manual != 0);
        setText(ui.chargeNotice, notice);
        setBg(ui.btnStart, manual ? C_FIELD : C_AMBER);
        setTextColor(ui.startLabel, manual ? C_TEXT3 : C_INK);
        setTextColor(ui.startHint, manual ? C_TEXT3 : C_INK);
    }
}
