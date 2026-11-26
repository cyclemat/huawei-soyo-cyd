# English summary

Firmware for the **ESP32-2432S028R** ("Cheap Yellow Display") that regulates the grid connection to zero: **SoyoSource** inverters feed in when the house imports, **Huawei R48xx** rectifiers charge the battery when there is export. Everything runs locally, Home Assistant via MQTT is optional.

> **Beta.** As of 0.3.1-beta, CAN, readings and charging are verified on an R4850G2. Soyo feed-in, several Huawei units, the Shelly in front of the Huawei, MQTT and Home Assistant are implemented but not yet confirmed on real hardware. Start with low power.

The full documentation in English is the [README_EN](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/README_EN.md). The web interface and the display are available in German and English.

## Quick start

1. Wire the CAN transceiver (SN65HVD230, 3.3 V) to connector CN1 and the RS485 converter to connector P1. See the wiring tables in the README.
2. Flash with the ⚡ [online flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) (Chrome or Edge on a PC). Unplug the RS485 converter from P1 first.
3. Join the Wi-Fi **HuaweiSoyo-XXXX** (default password `panelsat`), set your own access password, enter home Wi-Fi, grid meter, Soyo and Huawei.

## Control behaviour

| Grid | Action |
| --- | --- |
| import above the grid target | Soyo feeds in and regulates to the grid target |
| import between 0 W and the grid target | nothing, the Huawei does not step in |
| export | Huawei charges and regulates to 0 W |

- Grid target: −200 to 200 W. Negative means a small export remains.
- Control interval: 0.5 to 60 s, default 1.0 s.
- Soyo and Huawei never run at the same time.

## Troubleshooting

Open **Device → Diagnostics** in the web interface. It shows the CAN driver state, counters, events since start and an on-demand CAN capture. **Download report** saves everything as a text file without passwords. Please attach it when reporting a problem.
