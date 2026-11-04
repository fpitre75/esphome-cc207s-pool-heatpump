"""
CC207S-V2.0 pool heat pump controller.

Minimal component-level file: ESPHome looks for the actual "climate"
platform schema and codegen in climate.py, not here. This file only needs
to exist and declare AUTO_LOAD so the sub-components used by climate.py
(switch, sensor, binary_sensor, text_sensor) are pulled in automatically.
"""

AUTO_LOAD = [
    "binary_sensor",
    "switch",
    "sensor",
    "text_sensor",
]
