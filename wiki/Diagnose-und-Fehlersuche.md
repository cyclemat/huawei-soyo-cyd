# Diagnose und Fehlersuche

Der USB-Seriell des CYD gehört dem Soyo-Bus, deshalb gibt es keine seriellen Ausgaben. Die Fehlersuche läuft über das Netz.

## Diagnoseseite

Weboberfläche → **Gerät → Diagnose** (Adresse `http://<IP>/diag`).

| Bereich | Inhalt |
| --- | --- |
| **Huawei-CAN** | Zustand des Treibers, je Huawei Adresse und „antwortet / keine Daten“, im Klartext der Grund, wenn etwas fehlt |
| **Zähler** | empfangene Frames, Frames mit fremder ID samt letzter ID, abgelehnte Sendungen, Bus-Off-Neustarts, Fehlerzähler TEC / REC, verlorene Frames |
| **Ereignisse** | alles seit dem Start: Modus, Messausfall, WLAN, CAN weg mit Grund. Liegt im RAM, auch ohne SD-Karte |
| **CAN-Mitschnitt** | auf Knopfdruck die nächsten 64 Frames, gesendet (TX) und empfangen (RX), höchstens 10 s |
| **Bericht herunterladen** | alles zusammen als Textdatei `huawei-soyo-diagnose.txt`, ohne Passwörter |

Bei Fragen oder Fehlermeldungen bitte den Bericht mit Mitschnitt anhängen.

### Zähler lesen

| Anzeige | Bedeutung |
| --- | --- |
| „hat noch nie geantwortet“, Frames bleiben 0 | Verkabelung: CTX und CRX tauschen, CANH/CANL prüfen, Huawei am Netz? |
| Bus-Off-Neustarts zählen hoch, TEC über 96 | niemand bestätigt die Sendungen: Abschlusswiderstand, Masse, Transceiver |
| „fremde ID“ zählt hoch, Huawei antwortet trotzdem | harmlos. Der Huawei sendet eigene Statusmeldungen |
| „fremde ID“ wie 0x1082407F und ein Huawei fehlt | dieser Huawei sitzt auf Adresse 2: Anzahl bzw. Adressen prüfen |
| „Arbitrierung verloren“ zählt hoch, Fehlerzähler bleiben 0 | normal, wenn zwei Teilnehmer senden |

## Log auf microSD-Karte

Steckt eine Karte (FAT32), schreibt das CYD alle 10 s eine Zeile und alle Ereignisse nach `/logs/log-NNNNN.csv`. Die Dateien lassen sich in der Weboberfläche unter *Log-Dateien* herunterladen. Der Log übersteht Neustarts und ist für Fehler, die nur gelegentlich auftreten, die bessere Quelle.

## Häufige Fehler

| Anzeige | Ursache und Abhilfe |
| --- | --- |
| „keine Messung“ / „Shelly antwortet nicht“ | IP prüfen, WLAN-Signal unter *Aktuell* ansehen (schlechter als etwa −75 dBm ist zu schwach) |
| „Shelly verlangt ein Passwort“ | Anmeldung am Shelly abschalten |
| „unbekanntes Shelly-Format“ | Modell wird nicht unterstützt, Messung per MQTT nutzen |
| „CAN“ rot | Diagnoseseite öffnen, siehe Tabelle oben |
| „CAN“ war grün und wird rot | Diagnose → Ereignisse: dort steht der Grund |
| Temperatur zeigt „-“ | Der Huawei meldet keinen lesbaren Wert. Laden läuft trotzdem, bitte Bericht mit Mitschnitt schicken |
| „Kein Huawei eingetragen“ / „Ladespannung oder Höchststrom fehlt“ | Weboberfläche → Huawei ausfüllen |
| Soyo speist nicht ein | A und B tauschen, Limiter-Modus prüfen, RX am P1 nicht angeschlossen? |
| Netzbezug wird negativ angezeigt | *Vorzeichen umkehren* einschalten |
| Flashen bricht ab | RS485-Wandler am P1 abziehen, BOOT halten und RESET drücken |

## Bekannte Eigenheiten

- **R4850G2:** Im Ruhezustand meldet der Ausgangssensor etwa −29 °C. Angezeigt wird deshalb der höhere Wert aus Ausgang und Eingang.
- Beim Einschalten laufen kurz Bootmeldungen über den Soyo-Bus. Die Soyos verwerfen sie.
