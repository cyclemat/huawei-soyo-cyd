# Changelog

## 0.3.2-beta (2026-11-26)

### Neu
- Lizenz: PolyForm Noncommercial 1.0.0 (`LICENSE`). Nutzen, Ändern und Weitergeben für nicht-kommerzielle Zwecke erlaubt, kommerzielle Nutzung nur mit Erlaubnis des Urhebers.
- Anleitungen im Wiki und im Ordner `wiki/`, vollständig auf Deutsch und Englisch (Aufbau, Firmware und Einrichtung, Regelung, Diagnose).
- Online-Flasher zweisprachig (DE / EN oben rechts, Vorgabe nach Browsersprache).

### Behoben
- Gerät blieb im Einrichtungs-WLAN (4.3.2.1) hängen, wenn das Heim-WLAN beim Start nicht innerhalb von 12 s erreichbar war: Es versuchte danach nicht mehr, sich zu verbinden, solange ein Handy am Einrichtungs-WLAN hing, sonst erst nach 5 min per Neustart. Jetzt versucht es alle 30 s im Hintergrund, das Heim-WLAN zu erreichen (nur wenn kein Handy am Einrichtungs-WLAN hängt), und startet bei Erfolg von selbst neu. Beim Start gibt es nach 6 s einen zweiten Verbindungsversuch. *(Beta)*

## 0.3.1-beta (2026-11-26)

### Neu
- Diagnoseseite in der Weboberfläche (Gerät → Diagnose), weil der USB-Seriell dem Soyo gehört: CAN-Treiberzustand, Zähler (Frames je Huawei, fremde Adressen mit letzter ID, abgelehnte Sendungen, Bus-Off, TEC/REC, verlorene Frames), je Huawei Adresse, letzte Antwort und Werte, Ereignisse seit dem Start im RAM (auch ohne SD-Karte), CAN-Mitschnitt auf Knopfdruck (64 Frames TX/RX, höchstens 10 s, Puffer nur dann) und **Bericht herunterladen** (`/diag.txt`, Textdatei ohne Passwörter). *(Beta)*

### Verbessert
- Der Huawei erzeugt keinen Netzbezug mehr, um einen positiven Restbezug zu erreichen: Er lädt nur, wenn tatsächlich eingespeist wird, und regelt dann auf 0 W (bei negativem Restbezug auf diesen Wert). Der Restbezug gilt als Ziel nur noch für die Soyo-Einspeisung. *(Beta)*
- Regelzeit in der Weboberfläche einstellbar (Netzmessung → Regelzeit, 0,5 bis 60 s, ab Werk 1,0 s): Abstand zwischen zwei Regelschritten. *(Beta)*
- Restbezug (Ziel am Netz) darf negativ sein: −200 bis 200 W, in der Weboberfläche und am Display. Negativ = es wird leicht eingespeist. *(Beta)*
- Ladespannung und Spannung ohne CAN bis 60 V statt 58 V (der Huawei begrenzt selbst, R4850G2: 58,5 V).
- Ein einzelner Huawei darf jede CAN-Adresse haben: Antwortet er unter einer anderen Adresse als 1, wird sie übernommen und fortan angesprochen (Ereignis im Log). *(Beta)*
- Ereignis „Huawei-CAN fehlt“ nennt jetzt den Grund: Gerät, Adresse, „keine Antwort seit … s“, „noch nie geantwortet“, Bus-Off, Treiber aus oder zu heiß.

### Behoben
- „Kein CAN“ trotz verbundenem Huawei: Meldete das Netzteil unter 0x7F einen unlesbaren Wert (z. B. 41234455 °C) oder gar keine Temperatur, galt der CAN als gestört und das Laden blieb gesperrt. Jetzt zählt nur noch, ob der Huawei antwortet; die Übertemperatur-Sperre greift nur bei einer gültigen Meldung (−40 bis 150 °C) ab 85 °C. Unlesbare Temperatur wird als „-“ angezeigt, der Rohwert steht im Diagnosebericht. *(Beta)*
- Temperatur des Huawei wird vorzeichenbehaftet gelesen (am R4850G2 per CAN-Mitschnitt geprüft: 0x7F = −29,0 °C im Ruhezustand, 0x80 = 15,0 °C). Angezeigt und für die Übertemperatur-Sperre benutzt wird der höhere Wert aus Ausgang (0x7F) und Eingang (0x80). *(Beta)*

## 0.3.0-beta (2026-11-23)

### Neu
- Huawei-Detailseite am Display: Kachel „Ladung Huawei“ antippen → je Netzteil Verbindung, Ausgangsspannung, Ladestrom, DC- und AC-Leistung, Netzspannung, Temperatur, Wirkungsgrad. *(Beta)*
- Log auf der microSD-Karte des CYD: alle 10 s eine Zustandszeile und alle Ereignisse als CSV, je Datei 1 MB, älteste Dateien werden bei weniger als 8 MB frei gelöscht. Uhrzeit per NTP. Dateien in der Weboberfläche herunterladen und löschen. Ab Werk an, wirkt nur mit eingesteckter Karte. *(Beta)*
- Display-Schlaf: nach 60 min ohne Berührung aus (einstellbar), wieder an per Antippen oder bei starkem Helligkeitsanstieg am Lichtsensor (GPIO34). Schwellwert in der Weboberfläche einstellbar (10–90 %, ab Werk 40 %), dort auch der aktuelle Sensorwert. *(Beta)*
- Online-Flasher wie bei Simple Open Camper (`flasher/`, ESP Web Tools, GitHub Pages): „Nur Firmware (Update)“ und „Komplett (neu einrichten)“ im Browser. Lokaler Flasher per `flasher.bat` zusätzlich mit der hier gebauten Firmware. Fertige Firmware in `release/` (`huawei-soyo-cyd.bin` komplett, `ota/huawei-soyo-cyd.bin` nur Firmware), erzeugt mit `tools/release_local.py`.
- README im Aufbau von Simple Open Camper: Banner (`assets/banner.png`, Bild vom Nutzer, als `banner_small.jpg` eingebunden), Titel, Labels (Hardware, Display, Framework, Verbindung, Cloud, Status), Links zu PayPal und den Amazon-Teilen, Beta-Hinweis, Abschnitt „Support & Spenden“.
- Startbild beim Einschalten (Banner-Grafik vom Nutzer, `assets/splash_cyd.jpg`) mit Fortschrittsbalken und Statuszeile während des Starts (Version, Huawei-CAN, WLAN bzw. Einrichtungs-WLAN, Bereit); mindestens 2,5 s, danach Antippen überspringt. Beim Bauen eingebettet (`tools/embed_splash.py`), eigene `splash_cyd.jpg` im Projektordner hat Vorrang. Die Sollwerte an Soyo und Huawei laufen währenddessen schon. *(Beta)*
- Shelly vor jedem Huawei (optional, z. B. Plug oder 1PM): misst die Aufnahme, schaltet den Huawei ab, wenn sein CAN 15 s fehlt oder er 30 s über 100 W zieht, obwohl er aus sein soll; neuer Versuch nach 5 min und bei jedem Neustart. Solange ein Huawei laut Shelly über 40 W zieht, bleibt der Soyo gesperrt. Zustand auf der Huawei-Seite, in der Weboberfläche und per MQTT (`h1_plug` …). Ohne eingetragene Shellys passiert nichts. *(Beta)*

### Verbessert
- Touch läuft über Software-SPI, damit die SD-Karte den Hardware-Bus VSPI allein nutzen kann.
- Keine Log-Ausgaben mehr auf dem seriellen Anschluss – der ist der Soyo-RS485-Bus. Vorher konnten Meldungen einzelne Soyo-Befehle stören.
- Log-Download aus der Weboberfläche hält die Regelung am Laufen: die Datei geht in Stücken raus, zwischendurch werden CAN gelesen und die Sollwerte an Soyo und Huawei weiter gesendet. Vorher stand die Regelung für die Dauer des Downloads (einige Sekunden je MB).
- Sprachwechsel (DE/EN) vor der Anmeldung wirkt nur für die Sitzung; gespeichert wird er erst angemeldet. Vorher konnte jeder im Netz ohne Passwort Schreibzugriffe auf den Flash auslösen.
- GitHub-Workflow „Firmware bauen“ (nur von Hand zu starten): baut die Firmware und legt `firmware.bin` sowie eine Komplettdatei für den USB-Flash als Artifact ab.

## 0.2.0-beta (2026-11-21)

### Neu
- Zugangspasswort für die Weboberfläche: ab Werk `panelsat`, beim ersten Öffnen muss ein eigenes vergeben werden. Anmeldung per Cookie, 5 Fehlversuche → 30 s Pause. Passwortwechsel im Dialog, Abmelden. *(Beta)*
- Einrichtungs-WLAN heißt jetzt `HuaweiSoyo-XXXX`. Das WLAN-Passwort ist ab Werk `panelsat`, änderbar oder abschaltbar. *(Beta)*
- Mehrfach-Reset: 3× schnell aus/ein öffnet das Einrichtungs-WLAN, 5× setzt beide Passwörter zurück. Das Heim-WLAN bleibt dabei erhalten. *(Beta)*
- Deutsch und Englisch für Display, Weboberfläche, Home-Assistant-Namen und README (`README_EN.md`).
- Weboberfläche zeigt Live-Werte und Verbindungsstatus.
- Weitere Huawei-Messwerte für MQTT: Ausgangsspannung, Ladestrom, Netzspannung, Wirkungsgrad. *(Beta)*

### Verbessert
- Display und Weboberfläche im Design von Simple Open Camper: Bernstein-Akzent, Inter-Schrift mit Umlauten, Kacheln mit Füllstand, Phosphor-Icons, Web hell und dunkel, Schalter statt Häkchen.
- Einheiten mit schmalem Leerzeichen („25 W“). Eingaben in V und A mit Komma oder Punkt statt Zehntel-Werten.
- Code in Module aufgeteilt (`main`, `ui`, `web`, `devsetup`, `mqtt`).
- LVGL 8.4.0 wie in Simple Open Camper.

### Behoben
- Neustart-Schleife ohne angeschlossenes CAN-Modul: Bei „Bus-Off“ löste die Wiederherstellung des ESP-IDF-Treibers ein assert aus (`twai.c`, `tx_msg_count >= 0`). Jetzt wird der CAN-Treiber entfernt und nach 5 s neu gestartet; der Rest läuft weiter.
- Abstürze zählten beim Mehrfach-Reset mit und konnten die Passwörter zurücksetzen. Jetzt zählen nur Aus-/Einschalten und die Reset-Taste.
- Ladegrenze: Schieberegler und + gehen nur noch bis zur echten Grenze; ist sie 0, nennt das Ladefenster den Grund (kein Huawei eingetragen, Spannung oder Strom fehlt).
- Shelly-Messung brach öfter ab: WLAN-Energiesparen aus, längere Wartezeit (2,5 s), Verbindung bleibt offen, Gen2/3 fragt nur den Zähler ab, Grund bei fehlendem Wert in Display und Weboberfläche, Regelung hält den letzten Sollwert bis 10 s.
- Navigationsleiste: „Einstellungen“ überlappte die Nachbartasten.
- Commit-Adresse auf die anonyme GitHub-Adresse umgestellt.

## 0.1.0-beta (2026-11-21)

### Neu
- Nulleinspeisung: Soyo speist bei Netzbezug ein, Huawei lädt bei Einspeisung. Beide laufen nie gleichzeitig (5 s Pause, Soyo erst bei Huawei-Aufnahme unter 40 W). *(Beta)*
- Modus „Nur Laden“ (100–2000 W, Start 3 s halten, Stopp/Abbrechen sofort). *(Beta)*
- Nach dem Neustart geht die Nulleinspeisung automatisch weiter, sobald Messwerte da sind.
- Huawei-Modellauswahl (CAN-Stromfaktor) und Verhalten ohne CAN-Signal. *(Beta)*
- Netzmessung per Shelly (HTTP) oder MQTT-Abo mit JSONPath, MQTT-Status und Home-Assistant-Discovery. *(Beta)*
- LVGL-Display, Touch-Kalibrierung, Einrichtungs-WLAN auf 4.3.2.1 mit Captive Portal und WLAN-Suche.
