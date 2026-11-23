# Changelog

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
