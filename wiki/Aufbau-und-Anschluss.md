# Aufbau und Anschluss

## Teile

| Teil | Wofür |
| --- | --- |
| ESP32-2432S028R („Cheap Yellow Display“, 2,8″) | Regler mit Display |
| CAN-Transceiver **SN65HVD230** (3,3 V) | Huawei-Netzteile |
| RS485-Wandler mit automatischer Senderichtung | SoyoSource-Wechselrichter |
| Shelly-Energiezähler am Netzanschluss | Netzmessung (oder ein anderer Zähler per MQTT) |
| je Huawei ein Shelly mit Schalter und Messung | optional: Abschalten bei CAN-Ausfall |
| microSD-Karte (FAT32) | optional: Log |

Für CAN einen **3,3-V-Transceiver** nehmen, **kein MCP2515**-Modul. Der CAN-Controller steckt schon im ESP32.

## Stecker CN1 → CAN-Modul (Huawei)

| CN1 am CYD | SN65HVD230 |
| --- | --- |
| 3,3 V | 3V3 |
| GND | GND |
| IO22 | CTX |
| IO27 | CRX |

- **CANH / CANL** gehen parallel an alle Huaweis. Die Pins am Huawei-Stecker stehen im Handbuch des eigenen Modells.
- **125 kbit/s.**
- **Masse verbinden:** GND des Transceivers mit dem Minus des Huawei-Ausgangs.
- CANH und CANL verdrillen, auch auf kurzen Strecken, und nicht direkt neben den Lastkabeln führen.

### Abschlusswiderstand

| Messwert zwischen CANH und CANL (alles stromlos) | Bedeutung |
| --- | --- |
| etwa 60 Ω | zwei Widerstände mit je 120 Ω, ideal |
| etwa 120 Ω | ein Widerstand, bei kurzem Kabel (unter 1 m) in Ordnung |
| mehrere kΩ oder offen | keiner vorhanden: einen 120-Ω-Widerstand zwischen CANH und CANL setzen |
| deutlich unter 50 Ω | zu viele Widerstände oder Kurzschluss |

Ganz ohne Abschlusswiderstand geht es nicht, auch nicht bei 20 cm Kabel. Die SN65HVD230-Platinen haben meist schon einen 120-Ω-Widerstand aufgelötet.

## Stecker P1 (UART neben der USB-Buchse) → RS485-Wandler (Soyo)

| P1 am CYD | RS485-Wandler |
| --- | --- |
| VIN (5 V) | VCC |
| GND | GND |
| TX (GPIO1) | DI / TXD |
| RX (GPIO3) | **nicht anschließen** |

- **A / B** gehen parallel an alle Soyos. Kommt nichts an: A und B tauschen.
- 4800 Baud, alle Soyos im **RS485-Limiter-Modus**.
- RX bleibt frei: Die Soyos antworten nicht, und RX hängt intern am USB-Chip. Angeschlossen würde das Hochladen der Firmware nicht mehr klappen.

**Folge:** Der Soyo nutzt den USB-Seriell des CYD. Es gibt deshalb keine seriellen Debug-Ausgaben, die Fehlersuche läuft über die [Diagnoseseite](Diagnose-und-Fehlersuche.md). Zum Flashen per USB den RS485-Wandler am P1 abziehen.

## Mehrere Huawei-Netzteile

- **Ein einzelnes Gerät** darf jede CAN-Adresse haben. Das CYD übernimmt die Adresse, unter der es antwortet.
- **Mehrere Geräte** brauchen die Adressen 1 bis Anzahl. Die Netzteile vergeben sie beim ersten gemeinsamen Einschalten selbst. Welche Adressen wirklich antworten, zeigt die Diagnoseseite unter „fremde ID“ (0x108**2**407F = Adresse 2).
- Das CYD gibt allen dieselbe Ladespannung und teilt die Leistung gleichmäßig auf.
- Geladen wird nur, wenn alle eingetragenen Geräte antworten.

Mehrere Huaweis sind noch nicht an echter Hardware getestet.
