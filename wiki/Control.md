# Control

## Modes

| Mode | Behaviour |
| --- | --- |
| **Zero export** | regulates automatically: Soyo on import, Huawei on export |
| **Charge only** | charges with a fixed power, the Soyo stays off |
| **Off** | both stay at 0 W |

After a restart the device only resumes zero export. "Charge only" is never resumed automatically.

## Zero export

| Grid | What happens |
| --- | --- |
| import above the grid target | the Soyo feeds in and regulates to the grid target |
| import between 0 W and the grid target | nothing. The Huawei does not step in |
| export into the grid | the Huawei charges and regulates to 0 W |

- The **grid target** applies to the Soyo only. The Huawei never creates grid import to reach a positive grid target.
- **Dead band:** deviations up to 20 W do not trigger a control step.
- **Step size:** each control step follows one third of the deviation, 150 W at most.
- **Change of direction:** only through 0 W with a 5 s pause. The Soyo is released only when the Huawei draws less than 40 W.

## Grid target

Adjustable from **−200 to 200 W**, in the web interface under *Grid meter* or on the display under *Settings* with − / +.

| Value | Effect |
| --- | --- |
| positive, e.g. 25 W | a small import remains, nothing is exported for sure |
| 0 W | the target is exactly zero |
| negative, e.g. −50 W | a small export remains. The Huawei only charges when more than 50 W go into the grid |

## Control interval

Time between two control steps, adjustable from **0.5 to 60 s**, default 1.0 s.

- Larger = calmer and slower.
- Smaller = faster.
- A Shelly delivers a new value about once per second. Shorter than 1 s only helps with a grid reading via MQTT.

## Charge only

Hold the button on the display for 1.2 s, choose the power, **hold START for 3 s**. **STOP** and **Cancel** act immediately.

The charge limit is count × charge voltage × max. current, 2000 W at most. The actual power depends on the battery voltage: 34 A at 51 V are about 1570 W.

## When something fails

| Failure | Reaction |
| --- | --- |
| grid reading missing for more than 10 s (MQTT: timeout) | Soyo and Huawei go to 0 W |
| Huawei CAN missing | charging stops, the Soyo may keep feeding in |
| Huawei reports 85 °C or more | charging stops |
| CYD fails or CAN cable unplugged | the Huawei falls back to its offline current, 0 A by default. Not yet tested on the device |
| Shelly in front of the Huawei configured and CAN gone for 15 s | the CYD switches this Huawei off via its Shelly, new attempt after 5 min |
