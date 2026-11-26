# Wiring

## Parts

| Part | Purpose |
| --- | --- |
| ESP32-2432S028R ("Cheap Yellow Display", 2.8″) | controller with display |
| CAN transceiver **SN65HVD230** (3.3 V) | Huawei rectifiers |
| RS485 converter with automatic direction control | SoyoSource inverters |
| Shelly energy meter at the grid connection | grid reading (or another meter via MQTT) |
| one Shelly with switch and metering per Huawei | optional: switch off when CAN fails |
| microSD card (FAT32) | optional: log |

Use a **3.3 V transceiver** for CAN, **not an MCP2515** module. The CAN controller is already inside the ESP32.

## Connector CN1 → CAN module (Huawei)

| CN1 on the CYD | SN65HVD230 |
| --- | --- |
| 3.3 V | 3V3 |
| GND | GND |
| IO22 | CTX |
| IO27 | CRX |

- **CANH / CANL** run in parallel to all Huawei units. The pins on the Huawei connector are listed in the manual of your model.
- **125 kbit/s.**
- **Connect ground:** GND of the transceiver to the negative of the Huawei output.
- Twist CANH and CANL, even on short runs, and do not route them right next to the power cables.

### Termination resistor

| Reading between CANH and CANL (everything powered off) | Meaning |
| --- | --- |
| about 60 Ω | two resistors of 120 Ω each, ideal |
| about 120 Ω | one resistor, fine with a short cable (under 1 m) |
| several kΩ or open | none present: put a 120 Ω resistor between CANH and CANL |
| clearly below 50 Ω | too many resistors or a short circuit |

It does not work without any termination, not even with 20 cm of cable. Most SN65HVD230 boards already carry a 120 Ω resistor.

## Connector P1 (UART next to the USB socket) → RS485 converter (Soyo)

| P1 on the CYD | RS485 converter |
| --- | --- |
| VIN (5 V) | VCC |
| GND | GND |
| TX (GPIO1) | DI / TXD |
| RX (GPIO3) | **do not connect** |

- **A / B** run in parallel to all Soyos. If nothing arrives: swap A and B.
- 4800 baud, all Soyos in **RS485 limiter mode**.
- RX stays free: the Soyos do not answer, and RX is wired to the USB chip internally. If connected, uploading the firmware would no longer work.

**Consequence:** the Soyo uses the USB serial of the CYD. There is therefore no serial debug output; troubleshooting runs through the [diagnostics page](Diagnostics-and-Troubleshooting.md). Unplug the RS485 converter from P1 before flashing via USB.

## Several Huawei rectifiers

- **A single unit** may have any CAN address. The CYD adopts the address it answers with.
- **Several units** need the addresses 1 to count. The rectifiers assign them by themselves when first switched on together. Which addresses actually answer is shown on the diagnostics page under "foreign ID" (0x108**2**407F = address 2).
- The CYD gives all of them the same charge voltage and splits the power evenly.
- Charging only runs when all configured units answer.

Several Huawei units have not yet been tested on real hardware.
