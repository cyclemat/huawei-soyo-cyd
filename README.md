# Huawei / Soyo am Cheap Yellow Display

ESP32-Firmware für das **ESP32-2432S028R** mit ILI9341, XPT2046 und 320 × 240 Pixeln im Querformat. Bei anderen CYD-Varianten können Displaytreiber, Hintergrundbeleuchtung und Touchpins abweichen.

## Erststart

1. Mit PlatformIO die Umgebung `cyd` bauen und über USB auf das CYD laden (`pio run -e cyd -t upload`).
2. Mit **Huawei-Soyo-Setup** verbinden. Das WLAN-Passwort lautet **cydsetup**. Die Einstellungsseite öffnet sich am Handy von selbst (Captive Portal auf 4.3.2.1 wie bei WLED). Sonst **http://4.3.2.1** aufrufen. Ist ein Heim-WLAN eingetragen und 5 Minuten lang kein Handy verbunden, startet das CYD neu und versucht es erneut. Das Einrichtungs-WLAN erscheint auch, wenn das gespeicherte WLAN beim Start nicht erreichbar ist.
3. WLAN, Shelly und Gerätegrenzen im Webinterface eintragen. Nach dem Speichern startet das CYD neu. Die Einstellungen liegen im Flash. Die Seite **Settings** zeigt die neue IP für das Webinterface an. Dort lässt sich der gewünschte Restbezug in 5-W-Schritten ändern und speichern.
4. Auf der Startseite ohne aktive Regelung prüfen, dass zusätzlicher Netzbezug den Shelly-Wert erhöht. Bei umgekehrtem Vorzeichen im Webinterface −1 wählen. Mit beiden Leistungsstufen einzeln und kleiner Leistung beginnen.

## Bedienung

- Die **Startseite** zeigt die Soyo-Einspeisung (Istwert mit separatem Shelly-Messkanal, sonst gekennzeichneter Gesamtsollwert), gemessene Huawei-DC-Ladeleistung und Shelly-Netzleistung. Bei fehlender Huawei-Telemetrie erscheint „-- W“.
- Unter **Modi → Nulleinspeisung** steuert der Regler auf den einstellbaren Restbezug, standardmäßig 25 W. Bei Bezug erhöht er den Soyo-Sollwert, bei Überschuss erhöht er die Huawei-Ladeleistung. Vor dem Richtungswechsel fordert er auf beiden Seiten mindestens fünf Sekunden lang 0 W an. Soyo wird erst freigegeben, wenn die gemeldete Huawei-AC-Aufnahme höchstens 40 W beträgt.
- Für **Modi → Nur Laden** die Taste etwa 1,2 Sekunden halten. Mit +/− 100–2000 W in 100-W-Schritten auswählen. **Start** drei Sekunden halten. **Stop** sendet beim ersten erkannten Druck den Abschaltbefehl und belässt den Modus „Nur Laden“. **Abbrechen** sendet ebenfalls den Abschaltbefehl und springt zur Modusauswahl. Der Soyo-Sollwert bleibt im gesamten Lademodus 0 W. Die Huawei-Vorgabe ist eine DC-Leistungsgrenze, umgesetzt als Stromlimit auf Basis der eingestellten Ladespannung. Insgesamt fordert der Regler in beiden Modi höchstens 2000 W an. Die konfigurierten Gerätegrenzen können diesen Wert weiter senken.
- War vor einem Neustart oder Stromausfall **Nulleinspeisung** aktiv, startet der Controller wieder in diesem Modus. Auf der Startseite steht dann „Warte auf Shelly“, bis der erste gültige Messwert da ist. Danach folgen 5 s Wartezeit, dann regelt er von 0 W aus hoch. Ein Speichern im Webinterface ändert den gemerkten Modus nicht. Nach **Aus** oder **Nur Laden** startet er auf **Aus**. Manuelles Laden wird nie automatisch fortgesetzt und erfordert eine erneute Betätigung von **Start**.
- Bei fehlenden Shelly-Daten (länger als 6 s) oder Huawei-Telemetrie fordert er 0 W an.

## Busschnittstellen

| Schnittstelle | CYD-Pins und Protokoll |
| --- | --- |
| Huawei CAN | Stecker CN1: GPIO22 TX, GPIO27 RX, externer 3,3-V-Logik-CAN-Transceiver (z. B. SN65HVD230), 125 kbit/s, erweiterte CAN-IDs |
| SoyoSource | UART-Stecker P1 neben der USB-Buchse: TX (GPIO1) an DI/TXD des RS485-Moduls, RX nicht anschließen. RS485-Modul mit automatischer Senderichtung, 4800 Baud 8N1 |
| Display | ILI9341 auf GPIO14/12/13/15/2, Hintergrundbeleuchtung GPIO21 |
| Touch | XPT2046 auf GPIO25/39/32/33/36 |

Die Pinbelegung vor dem Anschluss mit der konkreten CYD-Platine abgleichen. Display und Touch laufen über LovyanGFX mit denselben Einstellungen wie Simply Open Camper (Klasse `CydDisplay`, Drehung `DISPLAY_ROTATION`). Touch kalibrieren: beim Einschalten 2 s auf den Bildschirm drücken und nach „Jetzt loslassen“ die Pfeilspitzen in den Ecken antippen, oder im Webinterface „Touch am Display kalibrieren“. Die Werte bleiben gespeichert. Die Display-Oberfläche nutzt LVGL 8.3 (Konfiguration in `include/lv_conf.h`). Die Schriften enthalten keine Umlaute, daher steht auf dem Display z. B. „pruefen“. Für den größeren Firmware-Umfang nutzt das Projekt die Partitionstabelle `huge_app.csv` (ohne OTA). Gespeicherte Einstellungen bleiben beim Wechsel erhalten. Der Stecker P3 (GPIO35/22/21) ist nicht nutzbar: GPIO35 kann nur lesen, GPIO21 ist die Hintergrundbeleuchtung. Weil der Soyo-Bus am UART0 hängt, gibt es keine seriellen Debug-Ausgaben. Hochladen über USB funktioniert weiterhin. Beim Reset sendet der ESP32 Bootmeldungen mit 115200 Baud auf den Bus, die Soyo wegen falscher Baudrate und Prüfsumme verwirft.

- **Huawei** verwendet die Adressen 1 bis 3 auf demselben Bus. Die eingetragene Anzahl erwartet fortlaufende Adressen ab 1. Die Geräte teilen sich die Leistung gleichmäßig und verwenden dieselbe Ladespannung, denselben Maximalstrom und Stromfaktor. Diese Werte müssen zu Netzteil und Batterie passen. Der Controller verwendet die 1024er Skalierung der Referenz für Spannung, Leistung und Temperatur und setzt das Huawei-Fallback-Stromlimit auf 0. Im Webinterface wird das Gleichrichter-Modell ausgewählt (R4830G, R4850G2 oder R4875G1). Daraus ergibt sich der CAN-Stromfaktor 1024 ÷ Nennstrom: 34,13 bzw. 20,48 bzw. 13,65. Für andere Geräte gibt es „Eigener Faktor“. Der Maximalstrom kann den Nennstrom des Modells nicht überschreiten. [Huawei-Referenz mit drei Geräten](https://github.com/mjpalmowski/CAN-BUS-control-R4875G1-with-ESPHome-and-MQTT/blob/main/3-phase-R48xxGx.yaml)
- **Huawei ohne CAN-Signal:** Im Webinterface wählbar. „Laden stoppen“ (Standard) setzt den Offline-Strom auf 0 A. „Mit festen Werten weiterladen“ setzt Offline-Spannung (0x0101) und Offline-Strom (0x0104). Diese Werte speichert der Huawei selbst und nutzt sie, wenn keine CAN-Befehle mehr kommen. Das CYD sendet sie einmal je Verbindung. Beim Weiterladen regelt niemand mehr, der Huawei bezieht dann dauerhaft Netzstrom. Ob der Huawei nach einem Abschaltbefehl des CYD (Ruhezustand) im Offline-Fall selbst wieder anläuft, ist am Gerät zu prüfen.
- **SoyoSource** benötigt bei allen Geräten am Bus den RS485-Limiter-Modus. Bis zu 32 gleiche Geräte sind konfigurierbar. Alle erhalten denselben Broadcast-Sollwert. Das CYD teilt den Gesamtsollwert durch die konfigurierte Anzahl. Die Referenz beschreibt Soyo-Varianten ohne Rückmeldung. Für die reale Einspeisung den optionalen Shelly-Kanal am Soyo-Abgang konfigurieren. Mit dieser Messung gibt der Regler Huawei erst frei, wenn der Betrag am Soyo-Abgang höchstens 20 W ist. Ohne die Zusatzmessung beruht die Freigabe auf der 0-W-Vorgabe und der Umschaltpause. Bei Busausfall greifen die Abschalt-/Fallbackfunktionen der Geräte. Diese vor dem gemeinsamen Betrieb am Gerät prüfen. [SoyoSource-Referenz](https://github.com/matlen67/soyosource-powercontroller)
- Die **Shelly**-Messung erfolgt über lokales HTTP für Shelly EM und 3EM (Gen1) sowie EM1- und EM-basierte Geräte wie Pro EM, EM Gen4 und Pro 3EM. Für die Summe eines 3EM im Dreiphasenprofil Kanal −1 auswählen. Für einzelne EM1-Messkanäle Kanal 0, 1 oder 2 angeben. Die Firmware unterstützt Shelly ohne aktivierte HTTP-Authentifizierung. [Gen1 API](https://shelly-api-docs.shelly.cloud/gen1/) · [EM1 RPC](https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/EM1/)

Die Regelung arbeitet softwareseitig anhand des Netzanschlussmesswerts. Wechselrichterreaktion, Funkverbindung, Lastsprünge und die Soyo-Nullvorgabe am realen Aufbau prüfen. Hardwaretests für diese Firmware stehen aus.





## MQTT und Home Assistant

- **Netzmessung per MQTT:** Im Webinterface unter *Netzmessung* die Quelle „MQTT-Abo mit JSONPath“ wählen. Dann Topic und JSONPath eintragen, z. B. Tasmota `tele/zaehler/SENSOR` mit `$.ENERGY.Power` oder Shelly Gen2 mit `$.em:0.total_act_power`. Bei leerem Pfad ist die ganze Nachricht die Zahl, z. B. bei `shellies/em/emeter/0/power`. Unterstützt werden Punkte, `[0]` für Listen und `['schlüssel']`. Das Vorzeichen aus *Gemeinsam* gilt auch hier. Kommt länger als die eingestellte Zeit (Standard 15 s) kein Wert, regelt der Controller auf 0 W.
- **Werte für Home Assistant:** Mit eingetragenem Broker veröffentlicht das CYD alle 5 s den Topic `<Präfix>/state` als JSON. Die Verfügbarkeit steht in `<Präfix>/status` (`online`/`offline`). Mit eingeschalteter Discovery erscheint in Home Assistant das Gerät „Huawei Soyo Regler“. Es enthält Netzleistung, Soyo-Sollwert und -Messung, Huawei-Sollwert, DC- und AC-Leistung, Restbezug, Modus und je Huawei Ladeleistung, Aufnahme, Ausgangsspannung, Ladestrom, Netzspannung, Temperatur und Wirkungsgrad. Nach einem Neustart von Home Assistant (`homeassistant/status` = `online`) sendet das CYD die Discovery erneut.
- Die Soyo-Istmessung läuft weiterhin nur über einen Shelly.
