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
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace esphome {
namespace hwp {
namespace protocol {

static constexpr uint8_t FRAME_DATA_LENGTH = 14;
static constexpr uint8_t FRAME_DATA_LENGTH_SHORT = 9;

static constexpr uint8_t DEFROST_ECO = 0;
static constexpr uint8_t DEFROST_NORMAL = 1;
static constexpr const char* DEFROST_ECO_STRING = "Eco";
static constexpr const char* DEFROST_NORMAL_STRING = "Normal";

static constexpr uint8_t HEAT_PUMP_COOLING_ONLY = 0;
static constexpr uint8_t HEAT_PUMP_ANY_MODE = 1;
static constexpr uint8_t HEAT_PUMP_HEATING_ONLY = 2;
static constexpr const char* HEAT_PUMP_COOLING_ONLY_STRING = "Cooling Only";
static constexpr const char* HEAT_PUMP_ANY_MODE_STRING = "Any Mode";
static constexpr const char* HEAT_PUMP_HEATING_ONLY_STRING = "Heating Only";

static constexpr uint8_t FLOW_METER_ENABLED = 0;
static constexpr uint8_t FLOW_METER_DISABLED = 1;
static constexpr const char* FLOW_METER_ENABLED_STRING = "Enabled";
static constexpr const char* FLOW_METER_DISABLED_STRING = "Disabled";

static constexpr uint8_t FAN_SPEED_CONTROL_COIL = 0;
static constexpr uint8_t FAN_SPEED_CONTROL_AMBIENT = 1;
static constexpr const char* FAN_SPEED_CONTROL_COIL_STRING = "Coil Temperature";
static constexpr const char* FAN_SPEED_CONTROL_AMBIENT_STRING = "Ambient Temperature";

static constexpr uint8_t SPEED_CONTROL_MODULE_ENABLED = 0;
static constexpr uint8_t SPEED_CONTROL_MODULE_DISABLED = 1;
static constexpr const char* SPEED_CONTROL_MODULE_ENABLED_STRING = "Enabled";
static constexpr const char* SPEED_CONTROL_MODULE_DISABLED_STRING = "Disabled";

static constexpr uint8_t FAN_MODE_LOW_SPEED = 0x00;
static constexpr uint8_t FAN_MODE_HIGH_SPEED = 0x01;
static constexpr uint8_t FAN_MODE_AMBIENT = 0x02;
static constexpr uint8_t FAN_MODE_SCHEDULED = 0x03;
static constexpr uint8_t FAN_MODE_AMBIENT_SCHEDULED = 0x04;
static constexpr uint8_t FAN_MODE_UNKNOWN = 0xFF;
static constexpr const char* FAN_MODE_LOW_SPEED_STRING = "Low Speed";
static constexpr const char* FAN_MODE_HIGH_SPEED_STRING = "High Speed";
static constexpr const char* FAN_MODE_AMBIENT_STRING = "Ambient";
static constexpr const char* FAN_MODE_SCHEDULED_STRING = "Scheduled";
static constexpr const char* FAN_MODE_AMBIENT_SCHEDULED_STRING = "Scheduled Ambient";
static constexpr const char* FAN_MODE_UNKNOWN_STRING = "Unknown";

void invert_bytes(uint8_t* data, size_t data_size, size_t length);

uint8_t calculate_short_checksum(const uint8_t* data, size_t length);
uint8_t calculate_long_checksum(const uint8_t* data, size_t length);
uint8_t packet_checksum_position(size_t length);
bool is_supported_packet_length(size_t length);
uint8_t calculate_packet_checksum(const uint8_t* data, size_t length);
bool is_packet_checksum_valid(const uint8_t* data, size_t length);

uint8_t defrost_eco_raw_from_bool(bool eco_mode);
uint8_t defrost_eco_raw_from_string(const std::string& mode);
const char* defrost_eco_to_string(uint8_t value);
const char* defrost_eco_log_format(uint8_t value);

uint8_t heat_pump_restrict_raw_from_string(const std::string& mode);
const char* heat_pump_restrict_to_string(uint8_t value);
const char* heat_pump_restrict_log_format(uint8_t value);

uint8_t flow_meter_raw_from_bool(bool enabled);
uint8_t flow_meter_raw_from_string(const std::string& mode);
const char* flow_meter_to_string(uint8_t value);
const char* flow_meter_log_format(uint8_t value);

uint8_t fan_speed_control_temp_raw_from_bool(bool ambient);
uint8_t fan_speed_control_temp_raw_from_string(const std::string& mode);
const char* fan_speed_control_temp_to_string(uint8_t value);
const char* fan_speed_control_temp_log_format(uint8_t value);

uint8_t speed_control_module_raw_from_bool(bool enabled);
uint8_t speed_control_module_raw_from_string(const std::string& mode);
const char* speed_control_module_to_string(uint8_t value);
const char* speed_control_module_log_format(uint8_t value);

uint8_t normalize_fan_mode_raw(uint8_t value);
const char* fan_mode_to_string(uint8_t value);
const char* fan_mode_log_format(uint8_t value);
bool is_custom_fan_mode(uint8_t value);
const char* fan_mode_custom_string(uint8_t value);
std::optional<uint8_t> fan_mode_raw_from_custom_string(const std::string& mode);

uint8_t encode_temperature_extended(float temperature);
float decode_temperature_extended(uint8_t value);
uint8_t encode_temperature(float temperature);
float decode_temperature(uint8_t value);
uint8_t encode_decimal_number(float value);
float decode_decimal_number(uint8_t value);
uint8_t encode_small_integer(float value);
float decode_small_integer(uint8_t value);

std::optional<uint8_t> read_conf1_h02_mode_restriction(const uint8_t* data, size_t length);
std::optional<float> read_conf1_temperature_parameter(const uint8_t* data, size_t length, uint8_t field_number);
std::optional<uint8_t> read_conf2_f01_fan_mode(const uint8_t* data, size_t length);
std::optional<float> read_conf2_d01_defrost_start(const uint8_t* data, size_t length);
std::optional<uint8_t> read_conf2_f10_fan_speed_control_temp(const uint8_t* data, size_t length);
std::optional<float> read_conf2_f13_max_fan_voltage_pct(const uint8_t* data, size_t length);
std::optional<float> read_conf4_fan_parameter(const uint8_t* data, size_t length, uint8_t field_number);
std::optional<float> read_conf5_d05_min_economy_defrost_time_minutes(
    const uint8_t* data, size_t length);
std::optional<uint8_t> read_conf5_d06_defrost_eco_mode(const uint8_t* data, size_t length);
std::optional<uint8_t> read_conf5_f11_speed_control_module(const uint8_t* data, size_t length);
std::optional<float> read_conf1_f12_min_fan_voltage_pct(const uint8_t* data, size_t length);
std::optional<float> read_cond1_inlet_temperature(const uint8_t* data, size_t length);
std::optional<uint8_t> read_cond1b_water_flow(const uint8_t* data, size_t length);
std::optional<float> read_cond2_temperature(const uint8_t* data, size_t length, uint8_t field_number);
std::optional<float> read_conf3_setpoint_limit(const uint8_t* data, size_t length, uint8_t field_number);

bool set_conf1_temperature_parameter(uint8_t* data, size_t length, uint8_t field_number, float value);
bool set_conf1_f12_min_fan_voltage_pct(uint8_t* data, size_t length, float value);
bool set_conf2_f01_fan_mode(uint8_t* data, size_t length, uint8_t value);
bool set_conf2_d01_defrost_start(uint8_t* data, size_t length, float value);
bool set_conf2_f10_fan_speed_control_temp(uint8_t* data, size_t length, uint8_t value);
bool set_conf2_f13_max_fan_voltage_pct(uint8_t* data, size_t length, float value);
bool set_conf4_fan_parameter(uint8_t* data, size_t length, uint8_t field_number, float value);
bool set_conf5_d05_min_economy_defrost_time_minutes(uint8_t* data, size_t length, float value);
bool set_conf5_d06_defrost_eco_mode(uint8_t* data, size_t length, uint8_t value);
bool set_conf5_f11_speed_control_module(uint8_t* data, size_t length, uint8_t value);

} // namespace protocol
} // namespace hwp
} // namespace esphome
