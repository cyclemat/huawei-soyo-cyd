# Fertige Firmware – Version 0.3.1-beta

Gebaut aus Commit c4caa35 (2026-11-26 17:21 UTC).

**Am einfachsten:** [Online-Flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) im Browser
(Chrome oder Edge am PC), Geraet per USB anschliessen, „Nur Firmware (Update)“ oder „Komplett (neu einrichten)“.

**Per USB (komplett, Adresse 0x0):** `esptool.py --chip esp32 write_flash 0x0 huawei-soyo-cyd.bin` – fuer neue
Geraete. **Achtung: loescht die Einstellungen** (WLAN, Passwoerter, Geraete), weil die Datei auch den
Einstellungsbereich enthaelt.

**Per USB, Einstellungen behalten:** `esptool.py --chip esp32 write_flash 0x10000 ota/huawei-soyo-cyd.bin`.

| Geraet | Version | Komplett (USB, 0x0) | Nur Firmware (USB, 0x10000) |
|---|---|---|---|
| ESP32-2432S028R (CYD) | 0.3.1-beta (c4caa35) | [huawei-soyo-cyd.bin](huawei-soyo-cyd.bin) (1503 KB) | [ota/huawei-soyo-cyd.bin](ota/huawei-soyo-cyd.bin) (1439 KB) |

Die Firmware hat kein Update ueber die Weboberflaeche (Partition `huge_app`, kein OTA).
