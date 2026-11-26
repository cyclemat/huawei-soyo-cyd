# Diagnostics and troubleshooting

The USB serial of the CYD belongs to the Soyo bus, so there is no serial output. Troubleshooting runs over the network.

## Diagnostics page

Web interface → **Device → Diagnostics** (address `http://<IP>/diag`).

| Area | Content |
| --- | --- |
| **Huawei CAN** | driver state, per Huawei its address and "answering / no data", the reason in plain text when something is missing |
| **Counters** | frames received, frames with a foreign ID including the last ID, rejected transmissions, bus-off restarts, error counters TEC / REC, lost frames |
| **Events** | everything since start: mode, missing reading, Wi-Fi, CAN lost with reason. Kept in RAM, also without an SD card |
| **CAN capture** | on request the next 64 frames, sent (TX) and received (RX), 10 s at most |
| **Download report** | everything together as a text file `huawei-soyo-diagnose.txt`, without passwords |

Please attach the report including a capture when asking a question or reporting a bug.

### Reading the counters

| Display | Meaning |
| --- | --- |
| "has never answered", frames stay at 0 | wiring: swap CTX and CRX, check CANH/CANL, is the Huawei on mains? |
| bus-off restarts keep counting, TEC above 96 | nobody acknowledges the transmissions: termination, ground, transceiver |
| "foreign ID" keeps counting, the Huawei still answers | harmless. The Huawei sends status messages of its own |
| "foreign ID" like 0x1082407F and a Huawei is missing | that Huawei sits at address 2: check count or addresses |
| "arbitration lost" keeps counting, error counters stay at 0 | normal when two nodes transmit |

## Log on microSD card

With a card (FAT32) inserted, the CYD writes one line every 10 s and all events to `/logs/log-NNNNN.csv`. The files can be downloaded in the web interface under *Log files*. The log survives restarts and is the better source for faults that only happen occasionally.

## Common problems

| Display | Cause and fix |
| --- | --- |
| "no reading" / "Shelly does not respond" | check the IP, look at the Wi-Fi signal under *Live* (worse than about −75 dBm is too weak) |
| "Shelly requires a password" | turn off authentication on the Shelly |
| "unknown Shelly format" | model not supported, use a reading via MQTT |
| "CAN" red | open the diagnostics page, see the table above |
| "CAN" was green and turns red | Diagnostics → events: the reason is listed there |
| temperature shows "-" | the Huawei reports no readable value. Charging still works; please send a report with a capture |
| "No Huawei configured" / "Charge voltage or max. current missing" | fill in web interface → Huawei |
| Soyo does not feed in | swap A and B, check limiter mode, RX on P1 not connected? |
| grid import shows as negative | turn on *Invert sign* |
| device sits in the setup Wi-Fi (4.3.2.1) | the home Wi-Fi was not reachable at start. Disconnect your phone from the setup Wi-Fi; the device retries every 30 s and restarts by itself |
| flashing aborts | unplug the RS485 converter from P1, hold BOOT and press RESET |

## Known quirks

- **R4850G2:** when idle, the output sensor reports about −29 °C. The higher value of output and inlet is therefore shown.
- At power-up, boot messages briefly run over the Soyo bus. The Soyos discard them.
