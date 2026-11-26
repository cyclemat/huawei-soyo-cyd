# Firmware und Einrichtung

## Firmware aufspielen

**Am einfachsten:** den ⚡ [Online-Flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) in Chrome oder Edge am PC öffnen, CYD per USB anschließen, Knopf drücken, Anschluss „USB-SERIAL CH340“ wählen.

| Knopf | Wirkung |
| --- | --- |
| **Nur Firmware (Update)** | Einstellungen bleiben |
| **Komplett (neu einrichten)** | Bootloader, Partitionen und Firmware. **Alle Einstellungen gehen verloren** |

- Den RS485-Wandler am Stecker P1 vorher abziehen, er hängt am selben seriellen Anschluss.
- Hängt es bei „Connecting…“: BOOT-Taste halten und kurz RESET drücken.

**Per esptool:** Die Dateien liegen unter [Releases](https://github.com/cyclemat/huawei-soyo-cyd/releases).

```
esptool.py --chip esp32 write_flash 0x0 huawei-soyo-cyd-…-komplett.bin
esptool.py --chip esp32 write_flash 0x10000 huawei-soyo-cyd-…-update.bin
```

Die erste Zeile löscht die Einstellungen, die zweite behält sie. Ein Update über die Weboberfläche gibt es nicht.

**Selbst bauen:** PlatformIO, `pio run -e cyd -t upload`.

## Erster Start

1. Mit dem WLAN **HuaweiSoyo-XXXX** verbinden, Passwort ab Werk `panelsat`. Die Einstellungsseite öffnet sich am Handy von selbst, sonst `http://4.3.2.1` aufrufen.
2. **Eigenes Zugangspasswort vergeben** (8 bis 63 Zeichen).
3. Heim-WLAN antippen und Passwort eintragen.
4. Netzmessung, Soyo und Huawei einrichten, dann **Speichern und neu starten**.
5. Die Adresse der Weboberfläche steht danach am Display unter **Einstellungen**.
6. Unter **Aktuell** prüfen, dass Netzbezug **positiv** angezeigt wird. Sonst *Vorzeichen umkehren* einschalten.

## Einstellungen in der Weboberfläche

| Abschnitt | Einstellungen |
| --- | --- |
| **Netzmessung** | Shelly (IP, Kanal) oder MQTT-Abo (Topic, JSONPath, Zeitlimit) · Vorzeichen · Restbezug · Regelzeit |
| **SoyoSource** | Anzahl, Höchstleistung je Gerät, optional ein Shelly für den Istwert |
| **Huawei** | Modell, Anzahl, Ladespannung (45 bis 60 V), Höchststrom · Verhalten ohne CAN-Signal · optional Shelly je Huawei |
| **MQTT / Home Assistant** | Broker, Zugangsdaten, Topic-Präfix, Discovery |
| **Zugang** | Passwort des Einrichtungs-WLANs, Sprache |
| **Display und Log** | Display-Schlaf, Lichtsensor, Log auf SD-Karte |
| **Gerät** | Zugangspasswort ändern, Touch kalibrieren, Diagnose, Abmelden |

**Huawei-Modell:** Es bestimmt den Stromfaktor (1024 ÷ Nennstrom). R4830G = 34,13, R4850G2 = 20,48, R4875G1 = 13,65. Der Huawei begrenzt die Ladespannung selbst auf sein Maximum (R4850G2: 58,5 V).

## Zurücksetzen

Gerät schnell hintereinander aus- und einschalten, jeweils unter 10 s:

| Anzahl | Wirkung |
| --- | --- |
| 3-mal | Einrichtungs-WLAN sofort an (bis zum nächsten Neustart) |
| 5-mal | Zugangs- und WLAN-Passwort wieder `panelsat`. Das Heim-WLAN bleibt gespeichert |
