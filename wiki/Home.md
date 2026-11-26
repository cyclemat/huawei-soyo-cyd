# Huawei · Soyo – Nulleinspeisung am Cheap Yellow Display

Firmware für das **ESP32-2432S028R** („Cheap Yellow Display“, 2,8″ Touch). Sie regelt den Netzanschluss auf null:

| Lage am Netz | Wer arbeitet | Richtung |
| --- | --- | --- |
| **Bezug** | **SoyoSource** speist ein | Akku → Haus |
| **Einspeisung** (z. B. PV-Überschuss) | **Huawei R48xx** lädt | Netz → Akku |

Alles läuft lokal, ohne Cloud. Home Assistant per MQTT ist optional.

> **Beta.** Stand 0.3.1-beta: CAN, Messwerte und Laden sind an einem R4850G2 geprüft. Soyo-Einspeisung, mehrere Huaweis, Shelly vor dem Huawei, MQTT und Home Assistant sind eingebaut, aber noch nicht an echter Hardware bestätigt. Mit kleiner Leistung beginnen.

## Schnellstart

1. **[Aufbau und Anschluss](Aufbau-und-Anschluss.md)** – Teile, Stecker, CAN und RS485 verdrahten.
2. **[Firmware und Einrichtung](Firmware-und-Einrichtung.md)** – im Browser flashen, WLAN und Geräte eintragen.
3. **[Regelung](Regelung.md)** – Nulleinspeisung, Restbezug, Regelzeit, Nur Laden.
4. **[Diagnose und Fehlersuche](Diagnose-und-Fehlersuche.md)** – wenn CAN rot ist oder nichts geregelt wird.

## Links

- ⚡ [Online-Flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) (Chrome oder Edge am PC)
- [Fertige Firmware](https://github.com/cyclemat/huawei-soyo-cyd/releases) · [Changelog](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/CHANGELOG.md)
- [README Deutsch](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/README.md) · [README English](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/README_EN.md)
- [English summary](English.md)

## Sicherheit in Kürze

- Der Akku muss durch sein **BMS** gegen Über- und Tiefentladung geschützt sein.
- Fehlt die Netzmessung länger als 10 s, gehen Soyo und Huawei auf 0 W.
- Fehlt der Huawei-CAN, stoppt das Laden. Der Soyo darf weiter einspeisen.
- Soyo und Huawei laufen nie gleichzeitig. Vor jedem Richtungswechsel stehen beide 5 s auf 0 W.
- Arbeiten an 230 V und am Akku nur, wenn du weißt, was du tust. Nutzung auf eigene Verantwortung.
