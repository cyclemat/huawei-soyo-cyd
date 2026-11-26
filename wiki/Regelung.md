# Regelung

## Modi

| Modus | Verhalten |
| --- | --- |
| **Nulleinspeisung** | regelt automatisch: Soyo bei Bezug, Huawei bei Einspeisung |
| **Nur Laden** | lädt mit einer festen Leistung, der Soyo bleibt aus |
| **Aus** | beide stehen auf 0 W |

Nach einem Neustart setzt das Gerät nur die Nulleinspeisung fort. „Nur Laden“ wird nie automatisch fortgesetzt.

## Nulleinspeisung

| Lage am Netz | Was passiert |
| --- | --- |
| Bezug über dem Restbezug | Der Soyo speist ein und regelt auf den Restbezug |
| Bezug zwischen 0 W und Restbezug | Nichts. Der Huawei greift nicht ein |
| Einspeisung ins Netz | Der Huawei lädt und regelt auf 0 W |

- Der **Restbezug** ist das Ziel nur für den Soyo. Der Huawei erzeugt nie Netzbezug, um einen positiven Restbezug zu erreichen.
- **Totzone:** Abweichungen bis 20 W lösen keinen Regelschritt aus.
- **Schrittweite:** Pro Regelschritt wird ein Drittel der Abweichung nachgeführt, höchstens 150 W.
- **Richtungswechsel:** nur über 0 W mit 5 s Pause. Der Soyo wird erst freigegeben, wenn der Huawei unter 40 W aufnimmt.

## Restbezug

Einstellbar von **−200 bis 200 W**, in der Weboberfläche unter *Netzmessung* oder am Display unter *Einstellungen* mit − / +.

| Wert | Wirkung |
| --- | --- |
| positiv, z. B. 25 W | ein kleiner Bezug bleibt stehen, es wird sicher nichts eingespeist |
| 0 W | Ziel ist genau null |
| negativ, z. B. −50 W | es wird leicht eingespeist. Der Huawei lädt erst, wenn mehr als 50 W ins Netz gehen |

## Regelzeit

Abstand zwischen zwei Regelschritten, einstellbar von **0,5 bis 60 s**, ab Werk 1,0 s.

- Größer = ruhiger und träger.
- Kleiner = schneller.
- Ein Shelly liefert etwa einmal pro Sekunde einen neuen Wert. Kürzer als 1 s bringt nur mit einer Netzmessung per MQTT etwas.

## Nur Laden

Am Display die Taste 1,2 s halten, Leistung wählen, **START 3 s halten**. **STOPP** und **Abbrechen** wirken sofort.

Die Ladegrenze ist Anzahl × Ladespannung × Höchststrom, höchstens 2000 W. Die tatsächliche Leistung hängt von der Akkuspannung ab: 34 A bei 51 V ergeben etwa 1570 W.

## Wenn etwas ausfällt

| Ausfall | Reaktion |
| --- | --- |
| Netzmessung länger als 10 s weg (MQTT: Zeitlimit) | Soyo und Huawei gehen auf 0 W |
| Huawei-CAN fehlt | Laden stoppt, der Soyo darf weiter einspeisen |
| Huawei meldet 85 °C oder mehr | Laden stoppt |
| CYD fällt aus oder CAN-Kabel ab | Der Huawei fällt auf seinen Offline-Strom zurück, ab Werk 0 A. Noch nicht am Gerät getestet |
| Shelly vor dem Huawei eingetragen und CAN 15 s weg | Das CYD schaltet diesen Huawei per Shelly ab, neuer Versuch nach 5 min |
