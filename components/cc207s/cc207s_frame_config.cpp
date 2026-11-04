/**
 * CC207S-V2.0 -- 0xD2/0xCC parsing and command construction.
 * Replaces FrameConditions2.cpp. Logic unchanged from the validated version
 * (23-25 Aug 2026 field testing, injection confirmed working); only the
 * surrounding class structure was simplified.
 */
#include "cc207s_frame_config.h"
#include "protocol_core.h"
#include <cmath>

namespace esphome {
namespace hwp {

static constexpr const char* TAG = "cc207s";

std::shared_ptr<BaseFrame> CC207SFrameConfig::create() {
    return std::make_shared<CC207SFrameConfig>();
}

/**
 * Accepts both the controller's 0xD2 broadcast and the keypad's 0xCC
 * command -- identical 12-byte payload (verified: same bytes, same
 * checksum, only the type differs) -- so a keypad press updates the setpoint
 * immediately through the same parser instead of waiting for the 0xD2 echo.
 */
bool CC207SFrameConfig::matches(BaseFrame& specialized, BaseFrame& base) {
    uint8_t type = base.packet.get_type();
    return (type == FRAME_ID_CONFIG || type == FRAME_ID_COMMAND) &&
           base.size() == frame_data_length;
}

/**
 * CC207S-V2.0 finding (23 Aug 2026): 0xD2 is the controller's periodic
 * re-broadcast of the stored configuration, not a temperature frame --
 * unlike the equivalent frame on the Hayward PC1000/PC1001 protocol this
 * project originally targeted. Captured proof (identical payload, identical
 * checksum, only type byte differs):
 *
 *   17:48:03  CC  18 3C 3C 08 0E 3C 7E 06 14 50 01 00  cs=CB
 *   17:48:05  D2  18 3C 3C 08 0E 3C 7E 06 14 50 01 00  cs=CB
 *
 * Flow: keypad press -> one-shot 0xCC -> controller stores the value (24C02
 * EEPROM on the board) -> re-broadcasts it in 0xD2 every ~6s forever. A
 * 16-minute idle capture contained 164 0xD2 frames and zero 0xCC frames,
 * which is what makes the setpoint readable at all times from 0xD2 alone.
 *
 *   d2       target temperature, raw/2 = degC (0x3C = 60 -> 30.0C -> 86F)
 *   d7 0x40  power state, ON=0x7E OFF=0x3E (confirmed via keypad Power button)
 *
 * Remaining payload bytes were constant across every capture -- installer
 * parameters (limits, defrost thresholds, timers). d3 and d6 also decode to
 * 30.0C and may be setpoint clamps; unverified, not published.
 */
void CC207SFrameConfig::parse(heat_pump_data_t& hp_data) {
    const uint8_t* d = this->packet.data;

    float setpoint_c = d[2] / 2.0f;
    hp_data.target_temperature = setpoint_c;

    bool power_on = (d[7] & 0x40) != 0;
    hp_data.mode = power_on ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;

    ESP_LOGD(TAG,
        "CC207S-CONFIG: setpoint=%.1fC (%.0fF) raw=0x%02X power=%s | cfg[%02X %02X %02X %02X "
        "%02X %02X %02X %02X %02X %02X]",
        setpoint_c, setpoint_c * 9.0f / 5.0f + 32.0f, d[2], power_on ? "ON" : "OFF", d[1], d[3],
        d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11]);
}

/**
 * Builds a 0xCC command frame to change setpoint and/or power state.
 *
 * Takes the last received 0xD2 payload, changes the type byte to 0xCC, and
 * overwrites only the fields the caller actually requested:
 *
 *   d2      setpoint, raw = degC * 2
 *   d7 0x40 power (set = ON, clear = OFF)
 *
 * Checksum = sum of d1..d12 mod 256, computed by protocol::calculate_packet_
 * checksum() -- same formula for all three frame types on this bus.
 *
 * Injected in parallel on the shared bus, no MITM needed: a 16-minute idle
 * capture showed the keypad emits zero 0xCC frames on its own, so there is
 * no ongoing traffic to collide with. A human pressing a button afterwards
 * simply overwrites this command, which is the desired behaviour -- see
 * docs/CC207S-protocole.md for the full known-behaviour writeup on this
 * race and the ~8s injection-to-echo latency.
 *
 * Verification is free: watch the next 0xD2. If it doesn't reflect the new
 * value, the command was not accepted and nothing on the unit has changed.
 *
 * Setpoint is rounded to the nearest 0.5C, the controller's own resolution,
 * so we never send a value it would not have generated itself.
 */
optional<std::shared_ptr<BaseFrame>> CC207SFrameConfig::control(const HWPCall& call) {
    if (this->packet.data_len != frame_data_length) {
        ESP_LOGW(TAG, "CC207S: cannot control yet, waiting for a first 0xD2 config frame");
        call.component.status_momentary_warning("Waiting for initial config frame", 5000);
        return nullopt;
    }

    CC207SFrameConfig cmd(*this);
    uint8_t* d = cmd.packet.data;
    bool changed = false;

    if (call.get_target_temperature().has_value()) {
        float requested = call.get_target_temperature().value();
        uint8_t raw = (uint8_t) lroundf(requested * 2.0f);
        if (raw != d[2]) {
            ESP_LOGI(TAG, "CC207S-CONTROL: setpoint %.1fC -> %.1fC (raw 0x%02X -> 0x%02X)",
                d[2] / 2.0f, raw / 2.0f, d[2], raw);
            d[2] = raw;
            changed = true;
        }
    }

    if (call.get_mode().has_value()) {
        climate::ClimateMode requested_mode = call.get_mode().value();
        bool want_on = (requested_mode != climate::CLIMATE_MODE_OFF);
        bool is_on = (d[7] & 0x40) != 0;
        if (want_on != is_on) {
            ESP_LOGI(TAG, "CC207S-CONTROL: power %s -> %s", is_on ? "ON" : "OFF",
                want_on ? "ON" : "OFF");
            if (want_on) {
                d[7] |= 0x40;
            } else {
                d[7] &= (uint8_t) ~0x40;
            }
            changed = true;
        }
    }

    if (!changed) {
        ESP_LOGD(TAG, "CC207S-CONTROL: nothing to change");
        return nullopt;
    }

    d[0] = FRAME_ID_COMMAND;
    cmd.packet.data_len = frame_data_length;
    cmd.packet.get_checksum_byte_ref() =
        protocol::calculate_packet_checksum(cmd.packet.data, cmd.packet.data_len);

    ESP_LOGI(TAG,
        "CC207S-CONTROL: TX 0xCC [%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X] "
        "cs=%02X",
        d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13]);

    return optional<std::shared_ptr<BaseFrame>>{std::make_shared<CC207SFrameConfig>(cmd)};
}

}  // namespace hwp
}  // namespace esphome
