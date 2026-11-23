# Huawei · Soyo – Nulleinspeisung am Cheap Yellow Display *(Beta)*

**Deutsch** · [English](README_EN.md)

Eine Firmware für das günstige **ESP32-2432S028R** („Cheap Yellow Display“, 2,8″ Touch), die den Netzanschluss auf **null** regelt – mit Akku, **SoyoSource**-Wechselrichtern und **Huawei R48xx**-Netzteilen.

| Am Netzanschluss … | … reagiert | Weg der Energie |
| --- | --- | --- |
| **Bezug** (Haus braucht Strom) | **SoyoSource** speist ein | Akku → Haus |
| **Einspeisung** (z. B. PV-Überschuss) | **Huawei** lädt | Netz → Akku |

> **Beta:** eingebaut, aber an echter Hardware noch nicht vollständig getestet. Mit kleiner Leistung beginnen und jedes Gerät einzeln prüfen.

## Inhalt

- [Funktionen](#funktionen)
- [Teile](#teile)
- [Anschluss](#anschluss)
- [Erster Start](#erster-start)
- [Bedienung am Display](#bedienung-am-display)
- [Weboberfläche](#weboberfläche)
- [Mehrere Huawei-Netzteile](#mehrere-huawei-netzteile)
- [MQTT und Home Assistant](#mqtt-und-home-assistant)
- [Sicherheit](#sicherheit)
- [Fehlersuche](#fehlersuche)
- [Technik und Lizenzen](#technik-und-lizenzen)

## Funktionen

- **Nulleinspeisung** auf einen einstellbaren Restbezug (ab Werk 25 W). Soyo und Huawei laufen **nie gleichzeitig**: Vor jedem Richtungswechsel stehen beide 5 s auf 0 W, Soyo wird erst freigegeben, wenn der Huawei unter 40 W aufnimmt – so kann sich nichts gegenseitig hochregeln.
- **Nur Laden** mit fester Leistung (100–2000 W), Start nur mit 3 s Halten, Stopp sofort.
- **Mehrere Geräte:** bis zu 32 SoyoSource (RS485) und bis zu 3 Huawei (CAN).
- **Netzmessung** per Shelly (EM, 3EM, Pro EM, Pro 3EM, EM Gen4) oder beliebigem **MQTT**-Wert mit JSONPath.
- **Home Assistant** per MQTT-Discovery: alle Werte erscheinen von selbst.
- **Automatischer Neustart** der Nulleinspeisung nach Stromausfall, sobald Messwerte da sind.
- **Einrichtung per Handy:** Einrichtungs-WLAN mit Captive Portal, WLAN-Suche, Weboberfläche mit Passwort.
- **Deutsch und Englisch**, Design wie Simple Open Camper.

## Teile

| Teil | Wofür | Bezugsquelle |
| --- | --- | --- |
| ESP32-2432S028R („Cheap Yellow Display“, 2,8″) | Regler mit Display | [bei Amazon](https://link.amazon/B00jjmvuF) |
| CAN-Transceiver SN65HVD230 (3,3 V) | Huawei-Netzteile | [bei Amazon](https://link.amazon/B02DRQBV8) |
| RS485-Wandler mit automatischer Senderichtung | SoyoSource-Wechselrichter | [bei Amazon](https://link.amazon/B092okQsP) |
| Shelly-Energiezähler am Netzanschluss | Netzmessung | oder ein anderer Zähler per MQTT |

Wichtig: Für CAN einen **3,3-V-Transceiver** wie den SN65HVD230 nehmen, **kein MCP2515**-Modul – der CAN-Controller steckt schon im ESP32.

## Anschluss

Jedes Modul bekommt seinen eigenen Stecker am CYD.

### Stecker CN1 → CAN-Modul (Huawei)

| CN1 am CYD | SN65HVD230 |
| --- | --- |
| 3,3 V | 3V3 |
| GND | GND |
| IO22 | CTX |
| IO27 | CRX |

- **CANH / CANL** gehen parallel an alle Huaweis. Die Pins am Huawei-Stecker stehen im Handbuch des eigenen Modells (z. B. R4850G2-Handbuch V2.2: Pin 11/12, V1.4: Pin 13/14).
- **125 kbit/s.** Ein CAN-Bus braucht genau zwei **120-Ω-Abschlusswiderstände**, einen an jedem Ende. Die SN65HVD230-Platinen haben meist schon einen. Prüfen: alles aus, zwischen CANH und CANL messen – richtig sind etwa **60 Ω**.

### Stecker P1 (UART neben der USB-Buchse) → RS485-Wandler (Soyo)

| P1 am CYD | RS485-Wandler |
| --- | --- |
| VIN (5 V) | VCC |
| GND | GND |
| TX (GPIO1) | DI / TXD |
| RX (GPIO3) | **nicht anschließen** |

- **A / B** gehen parallel an alle Soyos (kommt nichts an: A und B tauschen). 4800 Baud, alle Soyos im **RS485-Limiter-Modus**.
- **Warum RX frei bleibt:** Die Soyos empfangen im Limiter-Modus nur, sie antworten nicht. Und RX am P1 hängt intern am USB-Chip – darüber kommt beim Hochladen die Firmware vom PC. Der RXD-Pin des Wandlers ist ein Ausgang; angeschlossen würden zwei Ausgänge gegeneinander arbeiten, das Hochladen klappt dann nicht mehr.

### Warum nicht der Stecker P3 (GND, IO35, IO22, IO21)?

- **IO22** ist derselbe Pin wie auf CN1 (auf der Platine verbunden) und dort schon belegt.
- **IO35** kann nur lesen, **IO21** ist die Hintergrundbeleuchtung des Displays.
- P3 hat **keine 3,3 V**. Damit reicht P3 für keines der beiden Module allein.

**Folge der Belegung:** Soyo nutzt den USB-Seriell (UART0), daher gibt es keine seriellen Debug-Ausgaben. Fehlersuche läuft über die Weboberfläche. Hochladen über USB geht weiter. Beim Einschalten laufen kurz Bootmeldungen über den Soyo-Bus; die Soyos verwerfen sie (falsche Baudrate und Prüfsumme).

## Erster Start

1. Firmware bauen und per USB hochladen: `pio run -e cyd -t upload`.
2. Mit dem WLAN **HuaweiSoyo-XXXX** verbinden – Passwort ab Werk **`panelsat`**. Die Einstellungsseite öffnet sich am Handy von selbst (Captive Portal auf **4.3.2.1**), sonst `http://4.3.2.1` aufrufen.
3. **Eigenes Zugangspasswort vergeben:** Standardpasswort `panelsat` eintragen, darunter ein eigenes (8 bis 63 Zeichen).
4. Heim-WLAN aus der Liste antippen, Passwort eintragen, Netzmessung, Soyo und Huawei einrichten → **Speichern und neu starten**.
5. Die neue Adresse der Weboberfläche steht danach auf dem Display unter **Einstellungen**.
6. Unter **Aktuell** prüfen, dass Netzbezug **positiv** angezeigt wird – sonst *Vorzeichen umkehren*.
7. Touch ungenau? Beim Einschalten 2 s auf den Bildschirm drücken, nach „Jetzt loslassen“ die Pfeilspitzen in den Ecken antippen.

Das Einrichtungs-WLAN erscheint auch, wenn das Heim-WLAN beim Start nicht erreichbar ist. Verbindet sich 5 Minuten lang kein Handy, startet das Gerät neu und versucht es erneut – wichtig nach einem Stromausfall, wenn der Router länger braucht.

**Zurücksetzen** – Gerät schnell hintereinander aus- und einschalten (jeweils unter 10 s; Abstürze zählen nicht mit):

| Anzahl | Wirkung |
| --- | --- |
| 3-mal | Einrichtungs-WLAN sofort an (bis zum nächsten Neustart) |
| 5-mal | Zugangs- und WLAN-Passwort wieder `panelsat`. Das Heim-WLAN bleibt gespeichert, damit ein flackerndes Netz die Regelung nicht lahmlegt. |

## Bedienung am Display

| Seite | Inhalt |
| --- | --- |
| **Start** | Kacheln *Einspeisung Soyo* und *Ladung Huawei* mit Füllstand, darunter die Netzleistung (Bezug bernsteinfarben, Einspeisung blau) und Hinweise – z. B. warum gerade kein Messwert kommt |
| **Einstellungen** | Adresse der Weboberfläche bzw. des Einrichtungs-WLANs, Restbezug (Ziel am Netz) mit − / + |
| **Modus** | *Nulleinspeisung*, *Nur Laden*, *Aus* – der aktive Modus ist bernsteinfarben |

Die Kopfzeile zeigt den Modus und rechts den Zustand von MQTT, CAN und WLAN (grün = in Ordnung, rot = fehlt).

**Nulleinspeisung:** regelt auf den Restbezug. Nach Neustart oder Stromausfall geht es damit automatisch weiter, sobald gültige Messwerte da sind („Warte auf Messwert“), dann 5 s Pause und sanftes Hochfahren ab 0 W.

**Nur Laden:** Taste 1,2 s halten → Ladefenster. Leistung mit − / + oder Schieberegler wählen (bis zur Ladegrenze = Anzahl × Ladespannung × Höchststrom, höchstens 2000 W). **START 3 s halten** startet, **STOPP** und **Abbrechen** wirken sofort bei der ersten Berührung. Soyo bleibt die ganze Zeit aus. „Nur Laden“ wird nach einem Neustart nie automatisch fortgesetzt.

## Weboberfläche

Hell oder dunkel (je nach Gerät), Deutsch oder Englisch (oben rechts), geschützt durch das Zugangspasswort (5 Fehlversuche → 30 s Pause).

| Bereich | Einstellungen |
| --- | --- |
| **Aktuell** | Live-Werte, Verbindungsstatus, WLAN-Signal, Grund bei fehlender Messung |
| **WLAN** | Suche mit Signalstärke, Netz antippen, Passwort |
| **Netzmessung** | *Shelly* (IP, Kanal oder Summe aller Phasen) oder *MQTT-Abo* (Topic, JSONPath, Zeitlimit) · Vorzeichen · Restbezug |
| **SoyoSource** | Anzahl, Höchstleistung je Gerät, optional ein Shelly am Soyo-Abgang für den Istwert |
| **Huawei** | Modell (bestimmt den CAN-Stromfaktor), Anzahl, Ladespannung, Höchststrom · Verhalten **ohne CAN-Signal** |
| **MQTT / Home Assistant** | Broker, Zugangsdaten, Topic-Präfix, Discovery |
| **Zugang** | Passwort des Einrichtungs-WLANs (oder offen), Sprache |
| **Gerät** | Zugangspasswort ändern, Touch kalibrieren, Abmelden |

**Huawei-Modell und Stromfaktor:** Der Huawei erwartet den Strom über CAN als Ampere × Faktor, Faktor = 1024 ÷ Nennstrom.

| Modell | Nennstrom | Faktor |
| --- | --- | --- |
| R4830G | 30 A | 34,13 |
| R4850G2 | 50 A | 20,48 |
| R4875G1 | 75 A | 13,65 |
| Eigener Faktor | – | selbst eintragen |

## Mehrere Huawei-Netzteile

- **Adressen vergeben die Netzteile selbst:** Beim ersten Einschalten am gemeinsamen Bus einigen sie sich auf 1, 2, 3 und behalten sie danach. Am Gerät wird nichts eingestellt. Welches Gerät welche Adresse bekommt, hängt von der Einschaltreihenfolge ab.
- In der Weboberfläche die **Anzahl** eintragen. Das CYD spricht die Adressen **1 bis Anzahl** an, gibt allen dieselbe Ladespannung und **teilt die Leistung gleichmäßig** auf (z. B. 1500 W bei drei Geräten = je 500 W).
- Geladen wird nur, wenn **alle eingetragenen** Geräte antworten – sonst „CAN fehlt“ und das Laden stoppt.
- **Bekannte Grenze:** Das CYD erwartet die Adressen lückenlos ab 1. Hat sich ein einzelnes Gerät früher die Adresse 2 gemerkt, wird es nicht gefunden. Eine automatische Erkennung ist geplant.

## MQTT und Home Assistant

**Netzmessung per MQTT** – Beispiele:

| Gerät | Topic | JSONPath |
| --- | --- | --- |
| Tasmota-Lesekopf | `tele/zaehler/SENSOR` | `$.ENERGY.Power` (je nach Zähler z. B. `$.SML.Power_curr`) |
| Shelly Gen2/3 (Pro 3EM) | z. B. `shellypro3em/status/em:0` | `$.total_act_power` |
| Shelly Gen1 EM | `shellies/<name>/emeter/0/power` | leer (die Nachricht ist nur eine Zahl) |

Unterstützt werden `$.a.b`, `a[0].b` und `$['em:0'].act_power`; Zahlen als Text gehen auch. Kommt länger als das Zeitlimit (ab Werk 15 s) kein Wert, gehen Soyo und Huawei auf 0 W.

**Werte für Home Assistant:** Mit eingetragenem Broker sendet das CYD alle 5 s ein JSON auf `<Präfix>/state` (ab Werk `huaweisoyo/state`), die Verfügbarkeit auf `<Präfix>/status`. Mit Discovery erscheint das Gerät **„Huawei Soyo Regler“** mit Netzleistung, Soyo-Sollwert und -Messung, Huawei-Sollwert, Lade- und Aufnahmeleistung, Restbezug, Modus und je Huawei Ausgangsspannung, Ladestrom, Netzspannung, Temperatur und Wirkungsgrad. Startet Home Assistant neu, sendet das CYD die Discovery erneut.

## Sicherheit

- **Netzmessung fehlt** (Shelly länger als 10 s, MQTT länger als das Zeitlimit): Soyo und Huawei gehen auf 0 W. Bis dahin hält die Regelung den letzten Wert, damit einzelne Aussetzer nichts abschalten.
- **Huawei-CAN fehlt:** Laden stoppt; Soyo darf weiter einspeisen. Ohne angeschlossenes CAN-Modul läuft alles andere normal weiter.
- **CYD fällt aus oder CAN-Kabel ab:** Ist *Ohne CAN-Signal → weiterladen* **aus** (ab Werk), schreibt das CYD dem Huawei beim ersten Kontakt den Offline-Strom **0 A** in seinen Speicher; nach Verbindungsverlust soll er damit aufhören zu laden. **Noch nicht am Gerät getestet** – beim ersten Aufbau mit kleiner Leistung laden, CAN abziehen und mit Stromzange prüfen, ob und wann der Huawei aufhört. Ein Huawei, der noch nie mit dem CYD verbunden war, hat noch seine Werksvorgabe.
- Der Akku muss in jedem Fall durch sein **BMS** gegen Über- und Tiefentladung geschützt sein.
- Soyo-Istmessung nur per Shelly. Steuern aus Home Assistant ist noch nicht eingebaut.

## Fehlersuche

| Anzeige | Ursache und Abhilfe |
| --- | --- |
| „keine Messung“ / „Shelly antwortet nicht“ | IP prüfen, WLAN-Signal unter *Aktuell* ansehen (schlechter als etwa −75 dBm ist zu schwach) |
| „Shelly verlangt ein Passwort“ | Anmeldung am Shelly abschalten – die Firmware fragt ohne Passwort ab |
| „unbekanntes Shelly-Format“ | Modell wird nicht unterstützt – Messung per MQTT nutzen |
| „CAN“ rot | CTX und CRX tauschen, CANH/CANL prüfen, Abschlusswiderstand messen (≈ 60 Ω), Anzahl der Huaweis prüfen |
| „Kein Huawei eingetragen“ / „Ladespannung oder Höchststrom fehlt“ | Weboberfläche → Huawei ausfüllen |
| Soyo speist nicht ein | A und B tauschen, Limiter-Modus an den Soyos prüfen, RX am P1 nicht angeschlossen? |
| Netzbezug wird negativ angezeigt | *Vorzeichen umkehren* einschalten |

## Technik und Lizenzen

PlatformIO, Arduino-ESP32 2.0.17, LVGL 8.4, LovyanGFX (Display und Touch wie Simple Open Camper), PubSubClient. Partition `huge_app.csv` (ohne OTA).

| Datei | Inhalt |
| --- | --- |
| `src/main.cpp` | Regelung, CAN (Huawei), RS485 (Soyo), Shelly-Abfrage |
| `src/dispatch.h` | Regler: ein Sollwert mit Vorzeichen, Richtungswechsel nur über 0 W |
| `src/ui.cpp` | Display und Touch |
| `src/web.cpp` | Weboberfläche |
| `src/devsetup.cpp` | Zugangspasswort, Einrichtungs-WLAN, Mehrfach-Reset |
| `src/mqtt.cpp` | MQTT-Abo und Home-Assistant-Discovery |

Protokollwissen aus [mjpalmowski/CAN-BUS-control-R4875G1](https://github.com/mjpalmowski/CAN-BUS-control-R4875G1-with-ESPHome-and-MQTT) (MIT), [craigpeacock/Huawei_R4850G2_CAN](https://github.com/craigpeacock/Huawei_R4850G2_CAN) (GPL-3.0, nur Protokollwissen, eigene Umsetzung) und [matlen67/soyosource-powercontroller](https://github.com/matlen67/soyosource-powercontroller) (nur Protokollwissen). Fremde Bestandteile: [THIRD_PARTY.md](THIRD_PARTY.md) · Änderungen: [CHANGELOG.md](CHANGELOG.md)
