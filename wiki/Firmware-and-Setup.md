# Firmware and setup

## Flashing the firmware

**Easiest:** open the ⚡ [online flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) in Chrome or Edge on a PC, connect the CYD via USB, press the button, pick the port "USB-SERIAL CH340".

| Button | Effect |
| --- | --- |
| **Firmware only (update)** | settings are kept |
| **Complete (set up again)** | bootloader, partitions and firmware. **All settings are lost** |

- Unplug the RS485 converter from connector P1 first; it shares the serial port.
- If it hangs at "Connecting…": hold the BOOT button and briefly press RESET.

**With esptool:** the files are under [Releases](https://github.com/cyclemat/huawei-soyo-cyd/releases).

```
esptool.py --chip esp32 write_flash 0x0 huawei-soyo-cyd-…-komplett.bin
esptool.py --chip esp32 write_flash 0x10000 huawei-soyo-cyd-…-update.bin
```

The first line erases the settings, the second keeps them. There is no update through the web interface.

**Build it yourself:** PlatformIO, `pio run -e cyd -t upload`.

## First start

1. Join the Wi-Fi **HuaweiSoyo-XXXX**, factory password `panelsat`. The settings page opens by itself on a phone, otherwise open `http://4.3.2.1`.
2. **Set your own access password** (8 to 63 characters).
3. Tap your home Wi-Fi and enter its password.
4. Set up grid meter, Soyo and Huawei, then **Save and restart**.
5. The address of the web interface is then shown on the display under **Settings**.
6. Check under **Live** that grid import shows as **positive**. Otherwise turn on *Invert sign*.

**Language:** switch with DE/EN at the top right of the web interface. The display language is set under *Access* and applies after the next restart.

If the home Wi-Fi is not reachable at start, the setup Wi-Fi appears again. While no phone is connected to it, the device retries the home Wi-Fi every 30 s and restarts by itself once it is back.

## Settings in the web interface

| Section | Settings |
| --- | --- |
| **Grid meter** | Shelly (IP, channel) or MQTT topic (topic, JSONPath, timeout) · sign · grid target · control interval |
| **SoyoSource** | count, max. power per unit, optionally a Shelly for the actual value |
| **Huawei** | model, count, charge voltage (45 to 60 V), max. current · behaviour without CAN signal · optionally a Shelly per Huawei |
| **MQTT / Home Assistant** | broker, credentials, topic prefix, discovery |
| **Access** | password of the setup Wi-Fi, language |
| **Display and log** | display sleep, light sensor, log to SD card |
| **Device** | change access password, calibrate touch, diagnostics, sign out |

**Huawei model:** it sets the current factor (1024 ÷ rated current). R4830G = 34.13, R4850G2 = 20.48, R4875G1 = 13.65. The Huawei limits the charge voltage to its own maximum (R4850G2: 58.5 V).

## Resetting

Switch the device off and on quickly, under 10 s each time:

| Count | Effect |
| --- | --- |
| 3 times | setup Wi-Fi on immediately (until the next restart) |
| 5 times | access and Wi-Fi password back to `panelsat`. The home Wi-Fi stays stored |
