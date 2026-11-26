# Huawei · Soyo – zero export on the Cheap Yellow Display

Firmware for the **ESP32-2432S028R** ("Cheap Yellow Display", 2.8″ touch). It regulates the grid connection to zero:

| Grid | Who works | Direction |
| --- | --- | --- |
| **Import** | **SoyoSource** feeds in | battery → house |
| **Export** (e.g. PV surplus) | **Huawei R48xx** charges | grid → battery |

Everything runs locally, without a cloud. Home Assistant via MQTT is optional. The web interface and the display are available in German and English: switch with DE/EN at the top right of the web interface, the display language is under *Access*.

> **Beta.** As of 0.3.2-beta, CAN, readings and charging are verified on an R4850G2. Soyo feed-in, several Huawei units, the Shelly in front of the Huawei, MQTT and Home Assistant are implemented but not yet confirmed on real hardware. Start with low power.

## Quick start

1. **[Wiring](Wiring.md)** – parts, connectors, CAN and RS485.
2. **[Firmware and setup](Firmware-and-Setup.md)** – flash in the browser, enter Wi-Fi and devices.
3. **[Control](Control.md)** – zero export, grid target, control interval, charge only.
4. **[Diagnostics and troubleshooting](Diagnostics-and-Troubleshooting.md)** – when CAN is red or nothing is regulated.

## Links

- ⚡ [Online flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) (Chrome or Edge on a PC)
- [Ready-made firmware](https://github.com/cyclemat/huawei-soyo-cyd/releases) · [Changelog](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/CHANGELOG.md)
- [README English](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/README_EN.md) · [README Deutsch](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/README.md)
- [Deutsch](Home.md)

## Safety in short

- The battery must be protected against over- and deep discharge by its **BMS**.
- If the grid reading is missing for more than 10 s, Soyo and Huawei go to 0 W.
- If the Huawei CAN is missing, charging stops. The Soyo may keep feeding in.
- Soyo and Huawei never run at the same time. Before every change of direction both stay at 0 W for 5 s.
- Only work on 230 V and on the battery if you know what you are doing. Use at your own risk.

## License

[PolyForm Noncommercial 1.0.0](https://github.com/cyclemat/huawei-soyo-cyd/blob/main/LICENSE): free to use, modify and share for non-commercial purposes. Commercial use, including selling devices running this firmware, requires the author's written permission.
