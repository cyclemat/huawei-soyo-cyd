# Huawei · Soyo – zero export on the Cheap Yellow Display *(Beta)*

[Deutsch](README.md) · **English**

Firmware for the low-cost **ESP32-2432S028R** ("Cheap Yellow Display", 2.8″ touch) that regulates the grid connection to **zero** – with a battery, **SoyoSource** inverters and **Huawei R48xx** rectifiers.

| At the grid connection … | … this reacts | Energy flow |
| --- | --- | --- |
| **Import** (the house needs power) | **SoyoSource** feeds in | battery → house |
| **Export** (e.g. PV surplus) | **Huawei** charges | grid → battery |

> **Beta:** implemented but not yet fully tested on real hardware. Start with low power and test each device on its own.

## Contents

- [Features](#features)
- [Parts](#parts)
- [Wiring](#wiring)
- [First start](#first-start)
- [Display](#display)
- [Web interface](#web-interface)
- [Several Huawei rectifiers](#several-huawei-rectifiers)
- [MQTT and Home Assistant](#mqtt-and-home-assistant)
- [Safety](#safety)
- [Troubleshooting](#troubleshooting)
- [Technology and licenses](#technology-and-licenses)

## Features

- **Zero export** to an adjustable grid target (factory 25 W). Soyo and Huawei **never run at the same time**: before each change of direction both stay at 0 W for 5 s, and Soyo is only released once the Huawei draws less than 40 W – so they cannot push each other up.
- **Charge only** with a fixed power (100–2000 W), start only by holding for 3 s, stop immediately.
- **Several devices:** up to 32 SoyoSource (RS485) and up to 3 Huawei (CAN).
- **Grid meter** via Shelly (EM, 3EM, Pro EM, Pro 3EM, EM Gen4) or any **MQTT** value with JSONPath.
- **Home Assistant** via MQTT discovery: all values appear automatically.
- **Automatic restart** of zero export after a power cut, once readings arrive.
- **Setup by phone:** setup Wi-Fi with captive portal, Wi-Fi search, password-protected web interface.
- **German and English**, design as in Simple Open Camper.

## Parts

| Part | Used for | Where to buy |
| --- | --- | --- |
| ESP32-2432S028R ("Cheap Yellow Display", 2.8″) | controller with display | [on Amazon](https://link.amazon/B00jjmvuF) |
| CAN transceiver SN65HVD230 (3.3 V) | Huawei rectifiers | [on Amazon](https://link.amazon/B02DRQBV8) |
| RS485 converter with automatic direction | SoyoSource inverters | [on Amazon](https://link.amazon/B092okQsP) |
| Shelly energy meter at the grid connection | grid meter | or another meter via MQTT |

Important: for CAN use a **3.3 V transceiver** such as the SN65HVD230, **not an MCP2515** module – the CAN controller is already inside the ESP32.

## Wiring

Each module gets its own connector on the CYD.

### Connector CN1 → CAN module (Huawei)

| CN1 on the CYD | SN65HVD230 |
| --- | --- |
| 3.3 V | 3V3 |
| GND | GND |
| IO22 | CTX |
| IO27 | CRX |

- **CANH / CANL** go in parallel to all Huaweis. The pins on the Huawei connector are in the manual of your model (e.g. R4850G2 manual V2.2: pins 11/12, V1.4: pins 13/14).
- **125 kbit/s.** A CAN bus needs exactly two **120 Ω terminating resistors**, one at each end. The SN65HVD230 boards usually have one already. Check: everything off, measure between CANH and CANL – about **60 Ω** is correct.

### Connector P1 (UART next to the USB socket) → RS485 converter (Soyo)

| P1 on the CYD | RS485 converter |
| --- | --- |
| VIN (5 V) | VCC |
| GND | GND |
| TX (GPIO1) | DI / TXD |
| RX (GPIO3) | **do not connect** |

- **A / B** go in parallel to all Soyos (nothing arrives: swap A and B). 4800 baud, all Soyos in **RS485 limiter mode**.
- **Why RX stays unconnected:** in limiter mode the Soyos only receive, they never answer. And RX on P1 is connected internally to the USB chip – that is how the firmware arrives from the PC when uploading. The converter's RXD pin is an output; connected, two outputs would fight each other and uploading would stop working.

### Why not connector P3 (GND, IO35, IO22, IO21)?

- **IO22** is the same pin as on CN1 (connected on the board) and already used there.
- **IO35** is input only, **IO21** is the display backlight.
- P3 has **no 3.3 V**. So P3 is not enough for either module on its own.

**Consequence of this wiring:** Soyo uses the USB serial (UART0), so there is no serial debug output. Troubleshooting is done in the web interface. Uploading via USB still works. At power-up, boot messages briefly run over the Soyo bus; the Soyos discard them (wrong baud rate and checksum).

## First start

1. Build the firmware and upload it via USB: `pio run -e cyd -t upload`.
2. Connect to the Wi-Fi **HuaweiSoyo-XXXX** – factory password **`panelsat`**. The settings page opens by itself on the phone (captive portal on **4.3.2.1**), otherwise open `http://4.3.2.1`.
3. **Set your own access password:** enter the default password `panelsat`, below it your own (8 to 63 characters).
4. Tap your home Wi-Fi in the list, enter its password, set up grid meter, Soyo and Huawei → **Save and restart**.
5. The new address of the web interface is then shown on the display under **Settings**.
6. Under **Live**, check that grid import shows as **positive** – otherwise *Invert sign*.
7. Touch inaccurate? Press the screen for 2 s while switching on, after "Release now" tap the arrow tips in the corners.

The setup Wi-Fi also appears when the home Wi-Fi is not reachable at start. If no phone connects for 5 minutes, the device restarts and tries again – important after a power cut when the router takes longer.

**Reset** – switch the device off and on quickly (under 10 s each; crashes do not count):

| Count | Effect |
| --- | --- |
| 3 times | setup Wi-Fi on immediately (until the next restart) |
| 5 times | access and Wi-Fi password back to `panelsat`. The home Wi-Fi is kept so that a flickering grid cannot disable regulation. |

## Display

| Page | Content |
| --- | --- |
| **Home** | tiles *Soyo feed-in* and *Huawei charging* with fill level, below the grid power (import amber, export blue) and notes – e.g. why there is no reading right now |
| **Settings** | address of the web interface or setup Wi-Fi, grid target with − / + |
| **Mode** | *Zero export*, *Charge only*, *Off* – the active mode is amber |

The header shows the mode and, on the right, the state of MQTT, CAN and Wi-Fi (green = fine, red = missing).

**Zero export:** regulates to the grid target. After a restart or power cut it continues automatically once valid readings arrive ("Waiting for meter"), then a 5 s pause and a gentle ramp from 0 W.

**Charge only:** hold the button for 1.2 s → charge window. Choose the power with − / + or the slider (up to the charge limit = count × charge voltage × max. current, at most 2000 W). **Hold START for 3 s** to start, **STOP** and **Cancel** act on the first touch. Soyo stays off the whole time. "Charge only" never resumes automatically after a restart.

## Web interface

Light or dark (follows the device), German or English (top right), protected by the access password (5 wrong attempts → 30 s pause).

| Section | Settings |
| --- | --- |
| **Live** | live values, connection status, Wi-Fi signal, reason for a missing reading |
| **Wi-Fi** | search with signal strength, tap a network, password |
| **Grid meter** | *Shelly* (IP, channel or sum of all phases) or *MQTT topic* (topic, JSONPath, timeout) · sign · grid target |
| **SoyoSource** | count, max. power per unit, optionally a Shelly at the Soyo output for the actual value |
| **Huawei** | model (sets the CAN current factor), count, charge voltage, max. current · behaviour **without CAN signal** |
| **MQTT / Home Assistant** | broker, credentials, topic prefix, discovery |
| **Access** | setup Wi-Fi password (or open), language |
| **Device** | change access password, calibrate touch, sign out |

**Huawei model and current factor:** the Huawei expects the current over CAN as amps × factor, factor = 1024 ÷ rated current.

| Model | Rated current | Factor |
| --- | --- | --- |
| R4830G | 30 A | 34.13 |
| R4850G2 | 50 A | 20.48 |
| R4875G1 | 75 A | 13.65 |
| Custom factor | – | enter yourself |

## Several Huawei rectifiers

- **The rectifiers assign their addresses themselves:** when first switched on together on one bus they agree on 1, 2, 3 and keep them afterwards. Nothing is set on the device. Which unit gets which address depends on the power-up order.
- Enter the **count** in the web interface. The CYD talks to addresses **1 to count**, gives all of them the same charge voltage and **splits the power evenly** (e.g. 1500 W with three units = 500 W each).
- Charging only runs when **all configured** units answer – otherwise "no CAN" and charging stops.
- **Known limit:** the CYD expects the addresses without gaps starting at 1. If a single unit remembers address 2 from earlier, it is not found. Automatic detection is planned.

## MQTT and Home Assistant

**Grid meter via MQTT** – examples:

| Device | Topic | JSONPath |
| --- | --- | --- |
| Tasmota reading head | `tele/zaehler/SENSOR` | `$.ENERGY.Power` (depending on the meter e.g. `$.SML.Power_curr`) |
| Shelly Gen2/3 (Pro 3EM) | e.g. `shellypro3em/status/em:0` | `$.total_act_power` |
| Shelly Gen1 EM | `shellies/<name>/emeter/0/power` | empty (the message is just a number) |

Supported: `$.a.b`, `a[0].b` and `$['em:0'].act_power`; numbers sent as text work too. If no value arrives for longer than the timeout (factory 15 s), Soyo and Huawei go to 0 W.

**Values for Home Assistant:** with a broker configured, the CYD publishes a JSON on `<prefix>/state` (factory `huaweisoyo/state`) every 5 s and its availability on `<prefix>/status`. With discovery, the device **"Huawei Soyo controller"** appears with grid power, Soyo setpoint and measurement, Huawei setpoint, charging and input power, grid target, mode and per Huawei output voltage, charging current, mains voltage, temperature and efficiency. When Home Assistant restarts, the CYD sends the discovery again.

## Safety

- **Grid reading missing** (Shelly longer than 10 s, MQTT longer than the timeout): Soyo and Huawei go to 0 W. Until then the regulation holds the last value so that single dropouts do not switch anything off.
- **Huawei CAN missing:** charging stops; Soyo may keep feeding in. Without a CAN module connected, everything else keeps running normally.
- **CYD fails or CAN cable is unplugged:** with *Without CAN signal → keep charging* **off** (factory), the CYD writes an offline current of **0 A** into the Huawei's memory on first contact; after losing the connection it should stop charging with that. **Not yet tested on the device** – on first setup charge with low power, unplug CAN and check with a clamp meter whether and when the Huawei stops. A Huawei that has never been connected to the CYD still has its factory defaults.
- The battery must always be protected by its **BMS** against overcharging and deep discharge.
- Soyo actual value only via Shelly. Control from Home Assistant is not built in yet.

## Troubleshooting

| Message | Cause and fix |
| --- | --- |
| "no reading" / "Shelly does not respond" | check the IP, look at the Wi-Fi signal under *Live* (worse than about −75 dBm is too weak) |
| "Shelly requires a password" | turn off authentication on the Shelly – the firmware queries without a password |
| "unknown Shelly format" | model not supported – use measuring via MQTT |
| "CAN" red | swap CTX and CRX, check CANH/CANL, measure the termination (≈ 60 Ω), check the Huawei count |
| "No Huawei configured" / "Charge voltage or max. current missing" | fill in web interface → Huawei |
| Soyo does not feed in | swap A and B, check limiter mode on the Soyos, RX on P1 not connected? |
| Grid import shows as negative | turn on *Invert sign* |

## Technology and licenses

PlatformIO, Arduino-ESP32 2.0.17, LVGL 8.4, LovyanGFX (display and touch as in Simple Open Camper), PubSubClient. Partition `huge_app.csv` (no OTA).

| File | Content |
| --- | --- |
| `src/main.cpp` | regulation, CAN (Huawei), RS485 (Soyo), Shelly polling |
| `src/dispatch.h` | controller: one signed setpoint, change of direction only via 0 W |
| `src/ui.cpp` | display and touch |
| `src/web.cpp` | web interface |
| `src/devsetup.cpp` | access password, setup Wi-Fi, multi-reset |
| `src/mqtt.cpp` | MQTT topic and Home Assistant discovery |

Protocol knowledge from [mjpalmowski/CAN-BUS-control-R4875G1](https://github.com/mjpalmowski/CAN-BUS-control-R4875G1-with-ESPHome-and-MQTT) (MIT), [craigpeacock/Huawei_R4850G2_CAN](https://github.com/craigpeacock/Huawei_R4850G2_CAN) (GPL-3.0, protocol knowledge only, own implementation) and [matlen67/soyosource-powercontroller](https://github.com/matlen67/soyosource-powercontroller) (protocol knowledge only). Third-party parts: [THIRD_PARTY.md](THIRD_PARTY.md) · Changes: [CHANGELOG.md](CHANGELOG.md)
