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

#ifdef HWP_NATIVE_TEST

#include <initializer_list>
#include <optional>
#include <set>
#include <string>

namespace esphome {

template <typename T> using optional = std::optional<T>;
using std::make_optional;
using std::nullopt;

class Component {
  public:
    void status_momentary_warning(const char*, uint32_t) {}
};

namespace text_sensor {
class TextSensor {};
} // namespace text_sensor

namespace climate {

enum ClimateAction {
    CLIMATE_ACTION_OFF,
    CLIMATE_ACTION_IDLE,
    CLIMATE_ACTION_HEATING,
    CLIMATE_ACTION_COOLING,
};

enum ClimateMode {
    CLIMATE_MODE_OFF,
    CLIMATE_MODE_HEAT,
    CLIMATE_MODE_COOL,
    CLIMATE_MODE_AUTO,
};

enum class ClimateFanMode {
    CLIMATE_FAN_ON,
    CLIMATE_FAN_OFF,
    CLIMATE_FAN_AUTO,
    CLIMATE_FAN_LOW,
    CLIMATE_FAN_MEDIUM,
    CLIMATE_FAN_HIGH,
};

using ClimateModeMask = std::set<ClimateMode>;

class Climate {};

class ClimateCall {
  public:
    explicit ClimateCall(Climate* parent = nullptr) : parent_(parent) {}

    optional<ClimateMode> get_mode() const { return mode_; }
    optional<float> get_target_temperature() const { return target_temperature_; }
    optional<ClimateFanMode> get_fan_mode() const { return fan_mode_; }
    std::string get_custom_fan_mode() const { return custom_fan_mode_; }

  protected:
    Climate* parent_;
    optional<ClimateMode> mode_;
    optional<float> target_temperature_;
    optional<ClimateFanMode> fan_mode_;
    std::string custom_fan_mode_;
};

class ClimateTraits {
  public:
    void add_supported_mode(ClimateMode mode) { supported_modes_.insert(mode); }
    void set_supported_modes(const ClimateModeMask& modes) { supported_modes_ = modes; }
    void set_supported_fan_modes(std::initializer_list<ClimateFanMode>) {}
    void set_visual_min_temperature(float value) { visual_min_temperature_ = value; }
    void set_visual_max_temperature(float value) { visual_max_temperature_ = value; }
    void set_visual_temperature_step(float value) { visual_temperature_step_ = value; }

  private:
    ClimateModeMask supported_modes_;
    float visual_min_temperature_{0.0f};
    float visual_max_temperature_{0.0f};
    float visual_temperature_step_{0.0f};
};

inline const char* climate_mode_to_string(ClimateMode mode) {
    switch (mode) {
    case CLIMATE_MODE_OFF:
        return "OFF";
    case CLIMATE_MODE_HEAT:
        return "HEAT";
    case CLIMATE_MODE_COOL:
        return "COOL";
    case CLIMATE_MODE_AUTO:
        return "AUTO";
    }
    return "UNKNOWN";
}

} // namespace climate
} // namespace esphome

#else

#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"

#endif
