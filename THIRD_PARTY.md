# Fremde Bestandteile / Third-party components

| Bestandteil / Component | Lizenz / License | Verwendung / Use |
| --- | --- | --- |
| [LVGL](https://github.com/lvgl/lvgl) 8.4 | MIT | Display-Oberfläche / display UI (library) |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD (BSD-2-Clause) | Display- und Touch-Treiber / display and touch driver (library) |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MIT | MQTT (library) |
| [Inter](https://github.com/rsms/inter) | SIL Open Font License 1.1 | Schriften `src/fonts/font_ui_*.c` (aus Simple Open Camper) / fonts |
| [Phosphor Icons](https://github.com/phosphor-icons/core) | MIT | Icons `src/fonts/font_icons_*.c` (aus Simple Open Camper) / icons |
| Simple Open Camper (cyclemat) | eigenes Projekt / own project | Display-/Touch-Einstellungen, Zugang und Einrichtungs-WLAN (`src/devsetup.*`), Design |

Protokollwissen (keine Code-Übernahme) / protocol knowledge only (no code copied):

| Quelle / Source | Lizenz / License | Inhalt / Content |
| --- | --- | --- |
| [mjpalmowski/CAN-BUS-control-R4875G1-with-ESPHome-and-MQTT](https://github.com/mjpalmowski/CAN-BUS-control-R4875G1-with-ESPHome-and-MQTT) | MIT | Huawei-CAN-Befehle, Stromfaktor 1024 ÷ Nennstrom, mehrere Geräte |
| [craigpeacock/Huawei_R4850G2_CAN](https://github.com/craigpeacock/Huawei_R4850G2_CAN) | GPL-3.0 | Messwert-Codes (0x70–0x81), Skalierung ÷ 1024 – eigene Umsetzung |
| [matlen67/soyosource-powercontroller](https://github.com/matlen67/soyosource-powercontroller) | keine Lizenzdatei / no license file | SoyoSource-Limiter-Telegramm (RS485, 4800 Baud) |
| [Shelly API](https://shelly-api-docs.shelly.cloud/) | Dokumentation / documentation | HTTP-Abfrage der Energiezähler |
