"""
CC207S-V2.0 pool heat pump controller -- ESPHome component schema.

Replaces the original climate.py/__init__.py, which carried a generic
schema-registry system for ~50 Hayward PC1000/PC1001 entities. This targets
one board family only: the CC207S, tested on a V2.0 only (other versions
are probably compatible but untested), found in Turcotte TTi, Hayward, and
other rebadged pool heat pumps sharing the same Chico Electronic controller
and keypad protocol.

Decoded and validated Aug 2026 -- see docs/CC207S-protocole.md for the full
byte map, test methodology, and known-behaviour notes.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import include_builtin_idf_component
from esphome import pins
from esphome.components import climate, switch, sensor, binary_sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_INPUT,
    CONF_OUTPUT,
    UNIT_CELSIUS,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

# ESPHome reads AUTO_LOAD from the *platform* module (this file) for a
# platform component like climate.cc207s -- not from the package __init__.py.
# Without this the switch/sensor/binary_sensor/text_sensor components are
# never pulled into the build, and compilation fails with a missing
# binary_sensor.h include.
AUTO_LOAD = ["switch", "sensor", "binary_sensor", "text_sensor"]
DEPENDENCIES = ["esp32"]

cc207s_ns = cg.esphome_ns.namespace("hwp")
CC207SClimate = cc207s_ns.class_("CC207SClimate", climate.Climate, cg.PollingComponent)
ActiveModeSwitch = cc207s_ns.class_("ActiveModeSwitch", switch.Switch, cg.Component)
UpdateStatusSwitch = cc207s_ns.class_("UpdateStatusSwitch", switch.Switch, cg.Component)

CONF_PIN_TXRX = "pin_txrx"
CONF_START_BUS_ON_SETUP = "start_bus_on_setup"
CONF_ACTIVE_MODE_SWITCH = "active_mode_switch"
CONF_UPDATE_SENSORS_SWITCH = "update_sensors_switch"

# ---- the 4 additional temperature sensors (0xDD frame) --------------------
CONF_T_SUCTION = "t_suction"
CONF_T_DEFROST = "t_defrost"
CONF_T_AMBIENT = "t_ambient"
CONF_T_DISCHARGE = "t_discharge"

# ---- decoded diagnostics ---------------------------------------------------
CONF_FAULT_CODES = "fault_codes"
CONF_UNKNOWN_BITS_DETECTED = "unknown_bits_detected"
CONF_COMPRESSOR_NOT_COMPRESSING = "compressor_not_compressing"
CONF_BUS_COMMUNICATION = "bus_communication_lost"
CONF_COMMAND_NOT_CONFIRMED = "command_not_confirmed"

# ---- relay state (not diagnostics -- simple activity indicators) -----------
CONF_COMPRESSOR_ON = "compressor_on"
CONF_FAN_ON = "fan_on"


def _temperature_sensor_schema(icon):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=icon,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = (
    climate.climate_schema(CC207SClimate)
    .extend(cv.polling_component_schema("10s"))
    .extend(
        {
            cv.Required(CONF_PIN_TXRX): pins.gpio_pin_schema(
                {CONF_OUTPUT: True, CONF_INPUT: True}
            ),
            cv.Optional(CONF_NAME, default="Pool Heater"): cv.string,
            cv.Optional(CONF_START_BUS_ON_SETUP, default=True): cv.boolean,
            cv.Optional(
                CONF_ACTIVE_MODE_SWITCH, default={"name": "Active Mode"}
            ): switch.switch_schema(
                ActiveModeSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_OFF",
                icon="mdi:upload-network",
            ),
            cv.Optional(
                CONF_UPDATE_SENSORS_SWITCH, default={"name": "Update Sensors"}
            ): switch.switch_schema(
                UpdateStatusSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_ON",
                icon="mdi:upload-network",
            ),
            cv.Optional(CONF_T_SUCTION): _temperature_sensor_schema("mdi:thermometer-low"),
            cv.Optional(CONF_T_DEFROST): _temperature_sensor_schema("mdi:snowflake-thermometer"),
            cv.Optional(CONF_T_AMBIENT): _temperature_sensor_schema(
                "mdi:home-thermometer-outline"
            ),
            cv.Optional(CONF_T_DISCHARGE): _temperature_sensor_schema("mdi:thermometer-high"),
            cv.Optional(CONF_FAULT_CODES): text_sensor.text_sensor_schema(
                icon="mdi:alert-circle-outline",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_UNKNOWN_BITS_DETECTED): binary_sensor.binary_sensor_schema(
                icon="mdi:help-circle-outline",
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_COMPRESSOR_NOT_COMPRESSING): binary_sensor.binary_sensor_schema(
                icon="mdi:alert-octagon-outline",
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Fires if no frame at all has been seen for ~60s -- catches a
            # broken bus wire, a powered-off controller, or a disconnected
            # adapter. Added after a soldered bus wire snapped in a test
            # enclosure (30 Aug 2026) and went unnoticed: the ESP32 kept
            # running normally while quietly showing stale sensor values.
            # true = communication lost (a "problem"), matching the other
            # diagnostic sensors on this component. Not device_class
            # "connectivity" -- that class means the opposite (on=connected),
            # which would show a confusing inverted badge here.
            cv.Optional(CONF_BUS_COMMUNICATION): binary_sensor.binary_sensor_schema(
                icon="mdi:lan-disconnect",
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Fires false if the last injected 0xCC command was not echoed
            # back correctly within the acknowledgement timeout. Observation
            # only -- does not retry automatically.
            cv.Optional(CONF_COMMAND_NOT_CONFIRMED): binary_sensor.binary_sensor_schema(
                icon="mdi:check-network-outline",
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Relay states, not faults -- no device_class "problem" here,
            # they're simple activity indicators (data[10] bits 0x02/0x10).
            # Added 15 Sept 2026 to let P3/P4 sensor-fault occurrences be
            # correlated against real compressor/fan start-stop transients
            # in Home Assistant's own history graphs.
            cv.Optional(CONF_COMPRESSOR_ON): binary_sensor.binary_sensor_schema(
                icon="mdi:hvac",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FAN_ON): binary_sensor.binary_sensor_schema(
                icon="mdi:fan",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
)


async def to_code(config):
    # ESP-IDF 5.3+ split the RMT driver out of the base "driver" component
    # into its own idf component (esp_driver_rmt), and since ESPHome's
    # Feb 2026 change, unused built-in IDF components are excluded from the
    # build by default. hwp_rmt_adapter.h includes <driver/rmt_common.h>
    # directly, so this component must explicitly re-include it or the
    # build fails with "driver/rmt_common.h: No such file or directory"
    # (first seen on ESPHome 2026.9.0 / ESP-IDF 5.5.5).
    # NOTE: add_idf_component() (tried first) is for *external* IDF
    # components fetched via repo/ref/path -- it raises ValueError with
    # none of those set. include_builtin_idf_component() is the correct
    # call to re-enable a component that ships inside ESP-IDF itself.
    include_builtin_idf_component("esp_driver_rmt")

    pin_component = await cg.gpio_pin_expression(config[CONF_PIN_TXRX])
    heater = cg.new_Pvariable(config[CONF_ID], pin_component)
    cg.add(heater.set_start_bus_on_setup(config[CONF_START_BUS_ON_SETUP]))

    await cg.register_component(heater, config)
    await climate.register_climate(heater, config)

    if am_conf := config.get(CONF_ACTIVE_MODE_SWITCH):
        am_switch = await switch.new_switch(am_conf)
        await cg.register_component(am_switch, am_conf)
        await cg.register_parented(am_switch, heater)

    if us_conf := config.get(CONF_UPDATE_SENSORS_SWITCH):
        us_switch = await switch.new_switch(us_conf)
        await cg.register_component(us_switch, us_conf)
        await cg.register_parented(us_switch, heater)

    for key in (CONF_T_SUCTION, CONF_T_DEFROST, CONF_T_AMBIENT, CONF_T_DISCHARGE):
        if sensor_conf := config.get(key):
            sens = await sensor.new_sensor(sensor_conf)
            cg.add(getattr(heater, f"set_{key}_sensor")(sens))

    if fc_conf := config.get(CONF_FAULT_CODES):
        fc_sensor = await text_sensor.new_text_sensor(fc_conf)
        cg.add(heater.set_fault_codes_sensor(fc_sensor))

    if ub_conf := config.get(CONF_UNKNOWN_BITS_DETECTED):
        ub_sensor = await binary_sensor.new_binary_sensor(ub_conf)
        cg.add(heater.set_unknown_bits_detected_sensor(ub_sensor))

    if cnc_conf := config.get(CONF_COMPRESSOR_NOT_COMPRESSING):
        cnc_sensor = await binary_sensor.new_binary_sensor(cnc_conf)
        cg.add(heater.set_compressor_not_compressing_sensor(cnc_sensor))

    if bc_conf := config.get(CONF_BUS_COMMUNICATION):
        bc_sensor = await binary_sensor.new_binary_sensor(bc_conf)
        cg.add(heater.set_bus_communication_sensor(bc_sensor))

    if cc_conf := config.get(CONF_COMMAND_NOT_CONFIRMED):
        cc_sensor = await binary_sensor.new_binary_sensor(cc_conf)
        cg.add(heater.set_command_not_confirmed_sensor(cc_sensor))

    if comp_conf := config.get(CONF_COMPRESSOR_ON):
        comp_sensor = await binary_sensor.new_binary_sensor(comp_conf)
        cg.add(heater.set_compressor_on_sensor(comp_sensor))

    if fan_conf := config.get(CONF_FAN_ON):
        fan_sensor = await binary_sensor.new_binary_sensor(fan_conf)
        cg.add(heater.set_fan_on_sensor(fan_sensor))
