/**
 *
 * Copyright (c) 2024 S. Leclerc (sle118@hotmail.com)
 *
 * This file is part of the Pool Heater Controller component project.
 *
 * @project Pool Heater Controller Component
 * @developer S. Leclerc (sle118@hotmail.com)
 *
 * @license MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @disclaimer Use at your own risk. The developer assumes no responsibility
 * for any damage or loss caused by the use of this software.
 */
#include "protocol_core.h"

#include <cmath>

namespace esphome {
namespace hwp {
namespace protocol {

void invert_bytes(uint8_t* data, size_t data_size, size_t length) {
    if (data == nullptr) {
        return;
    }
    for (size_t i = 0; i < length && i < data_size; i++) {
        data[i] = ~data[i];
    }
}

uint8_t calculate_short_checksum(const uint8_t* data, size_t length) {
    unsigned int total = 0;
    if (data == nullptr || length < 2) {
        return 0;
    }
    for (size_t i = 1; i < length - 1; ++i) {
        total += data[i];
    }
    return total % 256;
}

uint8_t calculate_long_checksum(const uint8_t* data, size_t length) {
    unsigned int total = 0;
    if (data == nullptr || length < 2) {
        return 0;
    }
    for (size_t i = 1; i < length - 1; ++i) {
        total += data[i];
    }
    uint8_t sum8 = total % 256;
    uint8_t type_top_bits = data[0] & 0xC0;
    if (type_top_bits == 0x00) {
        return (sum8 + 0x0B) % 256;
    }
    return sum8;
}
uint8_t packet_checksum_position(size_t length) {
    if (length == 0) {
        return 0;
    }
    return length >= FRAME_DATA_LENGTH ? FRAME_DATA_LENGTH - 1 : length - 1;
}

bool is_supported_packet_length(size_t length) {
    return length == FRAME_DATA_LENGTH_SHORT || length == FRAME_DATA_LENGTH;
}

uint8_t calculate_packet_checksum(const uint8_t* data, size_t length) {
    if (length == FRAME_DATA_LENGTH_SHORT) {
        return calculate_short_checksum(data, length);
    }
    return calculate_long_checksum(data, length);
}

bool is_packet_checksum_valid(const uint8_t* data, size_t length) {
    if (data == nullptr || !is_supported_packet_length(length)) {
        return false;
    }
    return data[packet_checksum_position(length)] == calculate_packet_checksum(data, length);
}

uint8_t defrost_eco_raw_from_bool(bool eco_mode) {
    return eco_mode ? DEFROST_ECO : DEFROST_NORMAL;
}

uint8_t defrost_eco_raw_from_string(const std::string& mode) {
    if (mode == DEFROST_ECO_STRING) {
        return DEFROST_ECO;
    }
    if (mode == DEFROST_NORMAL_STRING) {
        return DEFROST_NORMAL;
    }
    return DEFROST_NORMAL;
}

const char* defrost_eco_to_string(uint8_t value) {
    switch (value) {
    case DEFROST_ECO:
        return DEFROST_ECO_STRING;
    case DEFROST_NORMAL:
        return DEFROST_NORMAL_STRING;
    default:
        return "Unknown";
    }
}

const char* defrost_eco_log_format(uint8_t value) {
    switch (value) {
    case DEFROST_ECO:
        return "ECO ";
    case DEFROST_NORMAL:
        return "NORM";
    default:
        return "UNKN";
    }
}

uint8_t heat_pump_restrict_raw_from_string(const std::string& mode) {
    if (mode == HEAT_PUMP_COOLING_ONLY_STRING) {
        return HEAT_PUMP_COOLING_ONLY;
    }
    if (mode == HEAT_PUMP_ANY_MODE_STRING) {
        return HEAT_PUMP_ANY_MODE;
    }
    if (mode == HEAT_PUMP_HEATING_ONLY_STRING) {
        return HEAT_PUMP_HEATING_ONLY;
    }
    return HEAT_PUMP_ANY_MODE;
}

const char* heat_pump_restrict_to_string(uint8_t value) {
    switch (value) {
    case HEAT_PUMP_COOLING_ONLY:
        return HEAT_PUMP_COOLING_ONLY_STRING;
    case HEAT_PUMP_ANY_MODE:
        return HEAT_PUMP_ANY_MODE_STRING;
    case HEAT_PUMP_HEATING_ONLY:
        return HEAT_PUMP_HEATING_ONLY_STRING;
    default:
        return "Unknown";
    }
}

const char* heat_pump_restrict_log_format(uint8_t value) {
    switch (value) {
    case HEAT_PUMP_COOLING_ONLY:
        return "COOLING ONLY";
    case HEAT_PUMP_ANY_MODE:
        return "ANY MODE    ";
    case HEAT_PUMP_HEATING_ONLY:
        return "HEATING ONLY";
    default:
        return "UNKNOWN     ";
    }
}

uint8_t flow_meter_raw_from_bool(bool enabled) {
    return enabled ? FLOW_METER_ENABLED : FLOW_METER_DISABLED;
}

uint8_t flow_meter_raw_from_string(const std::string& mode) {
    if (mode == FLOW_METER_ENABLED_STRING) {
        return FLOW_METER_ENABLED;
    }
    if (mode == FLOW_METER_DISABLED_STRING) {
        return FLOW_METER_DISABLED;
    }
    return FLOW_METER_DISABLED;
}

const char* flow_meter_to_string(uint8_t value) {
    switch (value) {
    case FLOW_METER_ENABLED:
        return FLOW_METER_ENABLED_STRING;
    case FLOW_METER_DISABLED:
        return FLOW_METER_DISABLED_STRING;
    default:
        return "Unknown";
    }
}

const char* flow_meter_log_format(uint8_t value) {
    switch (value) {
    case FLOW_METER_ENABLED:
        return "ENBL";
    case FLOW_METER_DISABLED:
        return "DIS ";
    default:
        return "UNKN";
    }
}

uint8_t fan_speed_control_temp_raw_from_bool(bool ambient) {
    return ambient ? FAN_SPEED_CONTROL_AMBIENT : FAN_SPEED_CONTROL_COIL;
}

uint8_t fan_speed_control_temp_raw_from_string(const std::string& mode) {
    if (mode == FAN_SPEED_CONTROL_COIL_STRING) {
        return FAN_SPEED_CONTROL_COIL;
    }
    if (mode == FAN_SPEED_CONTROL_AMBIENT_STRING) {
        return FAN_SPEED_CONTROL_AMBIENT;
    }
    return FAN_SPEED_CONTROL_COIL;
}

const char* fan_speed_control_temp_to_string(uint8_t value) {
    switch (value) {
    case FAN_SPEED_CONTROL_COIL:
        return FAN_SPEED_CONTROL_COIL_STRING;
    case FAN_SPEED_CONTROL_AMBIENT:
        return FAN_SPEED_CONTROL_AMBIENT_STRING;
    default:
        return "Unknown";
    }
}

const char* fan_speed_control_temp_log_format(uint8_t value) {
    switch (value) {
    case FAN_SPEED_CONTROL_COIL:
        return "COIL CONTROLLED   ";
    case FAN_SPEED_CONTROL_AMBIENT:
        return "AMBIENT CONTROLLED";
    default:
        return "UNKNOWN           ";
    }
}

uint8_t speed_control_module_raw_from_bool(bool enabled) {
    return enabled ? SPEED_CONTROL_MODULE_ENABLED : SPEED_CONTROL_MODULE_DISABLED;
}

uint8_t speed_control_module_raw_from_string(const std::string& mode) {
    if (mode == SPEED_CONTROL_MODULE_ENABLED_STRING) {
        return SPEED_CONTROL_MODULE_ENABLED;
    }
    if (mode == SPEED_CONTROL_MODULE_DISABLED_STRING) {
        return SPEED_CONTROL_MODULE_DISABLED;
    }
    return SPEED_CONTROL_MODULE_ENABLED;
}

const char* speed_control_module_to_string(uint8_t value) {
    switch (value) {
    case SPEED_CONTROL_MODULE_ENABLED:
        return SPEED_CONTROL_MODULE_ENABLED_STRING;
    case SPEED_CONTROL_MODULE_DISABLED:
        return SPEED_CONTROL_MODULE_DISABLED_STRING;
    default:
        return "Unknown";
    }
}

const char* speed_control_module_log_format(uint8_t value) {
    switch (value) {
    case SPEED_CONTROL_MODULE_ENABLED:
        return "ENBL";
    case SPEED_CONTROL_MODULE_DISABLED:
        return "DIS ";
    default:
        return "UNKN";
    }
}

uint8_t normalize_fan_mode_raw(uint8_t value) {
    if (value <= FAN_MODE_AMBIENT_SCHEDULED) {
        return value;
    }
    return FAN_MODE_LOW_SPEED;
}

const char* fan_mode_to_string(uint8_t value) {
    switch (value) {
    case FAN_MODE_LOW_SPEED:
        return FAN_MODE_LOW_SPEED_STRING;
    case FAN_MODE_HIGH_SPEED:
        return FAN_MODE_HIGH_SPEED_STRING;
    case FAN_MODE_AMBIENT:
        return FAN_MODE_AMBIENT_STRING;
    case FAN_MODE_SCHEDULED:
        return FAN_MODE_SCHEDULED_STRING;
    case FAN_MODE_AMBIENT_SCHEDULED:
        return FAN_MODE_AMBIENT_SCHEDULED_STRING;
    default:
        return FAN_MODE_UNKNOWN_STRING;
    }
}

const char* fan_mode_log_format(uint8_t value) {
    switch (value) {
    case FAN_MODE_LOW_SPEED:
        return "LOW   ";
    case FAN_MODE_HIGH_SPEED:
        return "HIGH  ";
    case FAN_MODE_AMBIENT:
        return "AMBI  ";
    case FAN_MODE_SCHEDULED:
        return "TIME  ";
    case FAN_MODE_AMBIENT_SCHEDULED:
        return "AMBTME";
    default:
        return "UNK ";
    }
}

bool is_custom_fan_mode(uint8_t value) {
    return value == FAN_MODE_AMBIENT || value == FAN_MODE_SCHEDULED ||
           value == FAN_MODE_AMBIENT_SCHEDULED;
}

const char* fan_mode_custom_string(uint8_t value) {
    switch (value) {
    case FAN_MODE_AMBIENT:
        return FAN_MODE_AMBIENT_STRING;
    case FAN_MODE_SCHEDULED:
        return FAN_MODE_SCHEDULED_STRING;
    case FAN_MODE_AMBIENT_SCHEDULED:
        return FAN_MODE_AMBIENT_SCHEDULED_STRING;
    default:
        return nullptr;
    }
}

std::optional<uint8_t> fan_mode_raw_from_custom_string(const std::string& mode) {
    if (mode == FAN_MODE_AMBIENT_STRING) {
        return FAN_MODE_AMBIENT;
    }
    if (mode == FAN_MODE_SCHEDULED_STRING) {
        return FAN_MODE_SCHEDULED;
    }
    if (mode == FAN_MODE_AMBIENT_SCHEDULED_STRING) {
        return FAN_MODE_AMBIENT_SCHEDULED;
    }
    return std::nullopt;
}

uint8_t encode_temperature_extended(float temperature) {
    const uint8_t decimal = (std::fabs(temperature - static_cast<int>(temperature)) >= 0.5f) ? 1 : 0;
    const uint8_t integer = static_cast<uint8_t>(temperature + 30.0f) & 0x7F;
    return static_cast<uint8_t>((integer << 1) | decimal);
}

float decode_temperature_extended(uint8_t value) {
    return static_cast<float>((value >> 1) & 0x7F) - 30.0f +
           ((value & 0x01) != 0 ? 0.5f : 0.0f);
}

uint8_t encode_temperature(float temperature) {
    const float abs_temperature = std::fabs(temperature);
    float encoded_temperature = abs_temperature;
    uint8_t offset = 0;
    if (encoded_temperature >= 2.0f) {
        offset = 1;
        encoded_temperature -= 2.0f;
    }
    const uint8_t decimal =
        (std::fabs(abs_temperature - static_cast<int>(abs_temperature)) >= 0.5f) ? 1 : 0;
    const uint8_t integer = static_cast<uint8_t>(encoded_temperature) & 0x1F;
    const uint8_t negative = temperature < 0.0f ? 1 : 0;
    return static_cast<uint8_t>((negative << 7) | (offset << 6) | (integer << 1) | decimal);
}

float decode_temperature(uint8_t value) {
    float result =
        static_cast<float>((value >> 1) & 0x1F) + ((value & 0x01) != 0 ? 0.5f : 0.0f);
    if ((value & 0x40) != 0) {
        result += 2.0f;
    }
    return (value & 0x80) != 0 ? -result : result;
}

uint8_t encode_decimal_number(float value) {
    const float abs_value = std::fabs(value);
    const uint8_t decimal = (std::fabs(abs_value - static_cast<int>(abs_value)) >= 0.5f) ? 1 : 0;
    const uint8_t integer = static_cast<uint8_t>(abs_value) & 0x3F;
    const uint8_t negative = value < 0.0f ? 1 : 0;
    return static_cast<uint8_t>((negative << 7) | (integer << 1) | decimal);
}

float decode_decimal_number(uint8_t value) {
    float result =
        static_cast<float>((value >> 1) & 0x3F) + ((value & 0x01) != 0 ? 0.5f : 0.0f);
    return (value & 0x80) != 0 ? -result : result;
}

uint8_t encode_small_integer(float value) {
    return static_cast<uint8_t>(std::round(value));
}

float decode_small_integer(uint8_t value) {
    return static_cast<float>(value);
}

namespace {
bool has_long_frame_type(uint8_t* data, size_t length, uint8_t frame_type) {
    return data != nullptr && length == FRAME_DATA_LENGTH && data[0] == frame_type;
}

bool has_const_long_frame_type(const uint8_t* data, size_t length, uint8_t frame_type) {
    return data != nullptr && length == FRAME_DATA_LENGTH && data[0] == frame_type;
}

void update_long_checksum(uint8_t* data) {
    data[FRAME_DATA_LENGTH - 1] = calculate_long_checksum(data, FRAME_DATA_LENGTH);
}
} // namespace

std::optional<uint8_t> read_conf1_h02_mode_restriction(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x81)) {
        return std::nullopt;
    }
    if ((data[2] & 0x08) != 0) {
        return HEAT_PUMP_HEATING_ONLY;
    }
    if ((data[2] & 0x04) != 0) {
        return HEAT_PUMP_ANY_MODE;
    }
    return HEAT_PUMP_COOLING_ONLY;
}

std::optional<float> read_conf1_temperature_parameter(
    const uint8_t* data, size_t length, uint8_t field_number) {
    if (!has_const_long_frame_type(data, length, 0x81) || field_number < 1 ||
        field_number > 7) {
        return std::nullopt;
    }
    const uint8_t raw_value = data[field_number + 2];
    if (field_number <= 3) {
        return decode_temperature_extended(raw_value);
    }
    return decode_temperature_extended(raw_value);
}

std::optional<uint8_t> read_conf2_f01_fan_mode(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x82)) {
        return std::nullopt;
    }
    return normalize_fan_mode_raw((data[2] >> 4) & 0x0F);
}

std::optional<float> read_conf2_d01_defrost_start(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x82)) {
        return std::nullopt;
    }
    return decode_temperature_extended(data[3]);
}

std::optional<uint8_t> read_conf2_f10_fan_speed_control_temp(
    const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x82)) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((data[2] >> 3) & 0x01);
}

std::optional<float> read_conf2_f13_max_fan_voltage_pct(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x82)) {
        return std::nullopt;
    }
    return decode_small_integer(data[10]);
}

std::optional<float> read_conf4_fan_parameter(
    const uint8_t* data, size_t length, uint8_t field_number) {
    if (!has_const_long_frame_type(data, length, 0x84) || field_number < 2 ||
        field_number > 9) {
        return std::nullopt;
    }
    if (field_number >= 2 && field_number <= 7) {
        return decode_temperature_extended(data[field_number]);
    }
    return decode_small_integer(data[field_number]);
}

std::optional<uint8_t> read_conf5_f11_speed_control_module(
    const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x85)) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((data[2] >> 3) & 0x01);
}

std::optional<uint8_t> read_conf5_d06_defrost_eco_mode(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x85)) {
        return std::nullopt;
    }
    return ((data[2] >> 6) & 0x01) != 0 ? DEFROST_ECO : DEFROST_NORMAL;
}

std::optional<float> read_conf5_d05_min_economy_defrost_time_minutes(
    const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x85)) {
        return std::nullopt;
    }
    return decode_decimal_number(data[3]);
}

std::optional<float> read_conf1_f12_min_fan_voltage_pct(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0x81)) {
        return std::nullopt;
    }
    return decode_small_integer(data[10]);
}

std::optional<float> read_cond1_inlet_temperature(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0xD1)) {
        return std::nullopt;
    }
    return decode_temperature(data[9]);
}

std::optional<uint8_t> read_cond1b_water_flow(const uint8_t* data, size_t length) {
    if (!has_const_long_frame_type(data, length, 0xD1)) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((data[3] >> 1) & 0x01);
}

std::optional<float> read_cond2_temperature(
    const uint8_t* data, size_t length, uint8_t field_number) {
    if (!has_const_long_frame_type(data, length, 0xD2)) {
        return std::nullopt;
    }
    switch (field_number) {
    case 3:
        return decode_temperature(data[4]);
    case 4:
        return decode_temperature(data[6]);
    case 6:
        return decode_temperature(data[5]);
    case 8:
        return decode_temperature(data[8]);
    default:
        return std::nullopt;
    }
}

std::optional<float> read_conf3_setpoint_limit(
    const uint8_t* data, size_t length, uint8_t field_number) {
    if (!has_const_long_frame_type(data, length, 0x83)) {
        return std::nullopt;
    }
    if (field_number < 8 || field_number > 11) {
        return std::nullopt;
    }
    return decode_temperature_extended(data[field_number - 1]);
}

bool set_conf1_temperature_parameter(
    uint8_t* data, size_t length, uint8_t field_number, float value) {
    if (!has_long_frame_type(data, length, 0x81) || field_number < 1 || field_number > 7) {
        return false;
    }
    if (field_number <= 3) {
        data[field_number + 2] = encode_temperature_extended(value);
    } else {
        data[field_number + 2] = encode_temperature_extended(value);
    }
    update_long_checksum(data);
    return true;
}

bool set_conf1_f12_min_fan_voltage_pct(uint8_t* data, size_t length, float value) {
    if (!has_long_frame_type(data, length, 0x81)) {
        return false;
    }
    data[10] = encode_small_integer(value);
    update_long_checksum(data);
    return true;
}

bool set_conf2_f01_fan_mode(uint8_t* data, size_t length, uint8_t value) {
    if (!has_long_frame_type(data, length, 0x82)) {
        return false;
    }
    data[2] = static_cast<uint8_t>((data[2] & 0x0F) | ((value & 0x0F) << 4));
    update_long_checksum(data);
    return true;
}

bool set_conf2_d01_defrost_start(uint8_t* data, size_t length, float value) {
    if (!has_long_frame_type(data, length, 0x82)) {
        return false;
    }
    data[3] = encode_temperature_extended(value);
    update_long_checksum(data);
    return true;
}

bool set_conf2_f10_fan_speed_control_temp(uint8_t* data, size_t length, uint8_t value) {
    if (!has_long_frame_type(data, length, 0x82)) {
        return false;
    }
    data[2] = static_cast<uint8_t>((data[2] & ~0x08) | ((value & 0x01) << 3));
    update_long_checksum(data);
    return true;
}

bool set_conf2_f13_max_fan_voltage_pct(uint8_t* data, size_t length, float value) {
    if (!has_long_frame_type(data, length, 0x82)) {
        return false;
    }
    data[10] = encode_small_integer(value);
    update_long_checksum(data);
    return true;
}

bool set_conf4_fan_parameter(uint8_t* data, size_t length, uint8_t field_number, float value) {
    if (!has_long_frame_type(data, length, 0x84) || field_number < 2 || field_number > 9) {
        return false;
    }
    if (field_number >= 2 && field_number <= 7) {
        data[field_number] = encode_temperature_extended(value);
    } else {
        data[field_number] = encode_small_integer(value);
    }
    update_long_checksum(data);
    return true;
}

bool set_conf5_f11_speed_control_module(uint8_t* data, size_t length, uint8_t value) {
    if (!has_long_frame_type(data, length, 0x85)) {
        return false;
    }
    data[2] = static_cast<uint8_t>((data[2] & ~0x08) | ((value & 0x01) << 3));
    update_long_checksum(data);
    return true;
}

bool set_conf5_d05_min_economy_defrost_time_minutes(uint8_t* data, size_t length, float value) {
    if (!has_long_frame_type(data, length, 0x85)) {
        return false;
    }
    data[3] = encode_decimal_number(value);
    update_long_checksum(data);
    return true;
}

bool set_conf5_d06_defrost_eco_mode(uint8_t* data, size_t length, uint8_t value) {
    if (!has_long_frame_type(data, length, 0x85)) {
        return false;
    }
    uint8_t bit_value = value == DEFROST_ECO ? 1 : 0;
    data[2] = static_cast<uint8_t>((data[2] & ~0x40) | (bit_value << 6));
    update_long_checksum(data);
    return true;
}

} // namespace protocol
} // namespace hwp
} // namespace esphome
