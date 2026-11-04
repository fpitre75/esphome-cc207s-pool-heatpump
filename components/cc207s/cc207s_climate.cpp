/**
 * CC207S-V2.0 pool heat pump controller -- implementation.
 * Replaces PoolHeater.cpp + switch_helpers.cpp-equivalent inline defs +
 * helper_components.cpp (GenerateCodeButton, dropped) + HeaterStatus usage.
 */
#include "cc207s_climate.h"
#include "cc207s_frame_telemetry.h"
#include "cc207s_frame_config.h"
#include <cinttypes>
#include <cmath>

namespace esphome {
namespace hwp {

static constexpr const char* TAG = "cc207s";

// ---- switch implementations ---------------------------------------------
void ActiveModeSwitch::write_state(bool new_state) {
    this->publish_state(new_state);
    this->parent_->set_passive_mode(!new_state);
}

void UpdateStatusSwitch::write_state(bool new_state) {
    this->publish_state(new_state);
    this->parent_->set_update_active(new_state);
}

// ---- registration ----------------------------------------------------
// CC207S-V2.0: the CC207S bus has exactly two frame shapes -- 0xDD
// telemetry and 0xD2/0xCC config/command -- so only two classes are
// registered here, down from eight in the original multi-protocol design.
//
// This still uses explicit registration rather than the self-registering
// static-initializer pattern: that pattern was found (Aug 2026) to never
// actually run under ESP-IDF's --gc-sections linker option, since its only
// purpose is a side effect the linker sees no live reference to. A normal
// function call from setup() is immune to that class of dead-code
// elimination and to static-initialization-order concerns.
static void register_frame_classes() {
    BaseFrame::register_frame_class(
        &CC207SFrameTelemetry::create, &CC207SFrameTelemetry::matches);
    BaseFrame::register_frame_class(&CC207SFrameConfig::create, &CC207SFrameConfig::matches);
}

CC207SClimate::CC207SClimate(InternalGPIOPin* gpio_pin) {
    this->driver_.set_gpio_pin(gpio_pin);
}

void CC207SClimate::setup() {
    register_frame_classes();
    ESP_LOGI(TAG, "CC207S component starting");
    this->mode = climate::CLIMATE_MODE_HEAT;  // until the first config frame arrives
    this->driver_.set_data_model(hp_data_);
    // No HWPWebDashboard instantiated -- see the note in cc207s_climate.h.
    // Home Assistant / ESPHome's own web_server: component already lists
    // every entity this component registers.
    if (this->start_bus_on_setup_) {
        this->driver_.setup();
    } else {
        ESP_LOGW(TAG, "Bus startup disabled by configuration");
    }
    this->current_temperature = NAN;

    // Give the two watchdog sensors an initial state at boot rather than
    // leaving them unpublished (shows as "Unknown" in Home Assistant
    // forever if the state they'd naturally settle into never changes).
    // bus_communication_lost updates for real on the first update() call
    // regardless; command_not_confirmed only has something to report once a
    // command has actually been sent, so it starts optimistic ("no problem
    // yet") until then.
    if (this->command_not_confirmed_sensor_ != nullptr) {
        this->command_not_confirmed_sensor_->publish_state(false);
    }

    ESP_LOGI(TAG, "CC207S setup complete");
}

void CC207SClimate::loop() { /* nothing to do -- no dashboard to service */ }

void CC207SClimate::update() {
    if (this->driver_.get_bus_mode() == BUSMODE_ERROR) {
        this->status_momentary_error("Bus Error", 5000);
    }
    if (is_heater_offline()) {
        this->status_set_warning("Heater offline");
    } else {
        this->status_clear_warning();
    }

    // ---- bus communication watchdog ----------------------------------------
    // Added after a field incident (30 Aug 2026): a soldered bus wire snapped
    // in a temporary enclosure. The ESP32 kept running normally -- WiFi, API,
    // web server all healthy -- while quietly showing stale, no-longer-true
    // sensor values until a manual reboot revealed the loss as a wall of NA.
    //
    // Deliberately NOT reusing is_heater_offline(): that pre-existing check
    // only looks at last_heater_frame, but the decoder classifies frames as
    // SOURCE_HEATER or SOURCE_CONTROLLER based on the RMT signal polarity it
    // detects (Decoder.cpp) -- which of the two fields actually gets written
    // depends on wiring/hardware specifics, not on frame type. On at least
    // one installation, telemetry was flowing perfectly (temperatures updated
    // fine) while is_heater_offline() stayed permanently true because only
    // last_controller_frame was being populated. Checking both fields and
    // taking whichever is more recent avoids that trap entirely.
    uint32_t last_any_frame_ms = 0;
    bool have_any_frame = false;
    if (this->hp_data_.last_heater_frame.has_value()) {
        last_any_frame_ms = this->hp_data_.last_heater_frame.value();
        have_any_frame = true;
    }
    if (this->hp_data_.last_controller_frame.has_value() &&
        (!have_any_frame ||
         this->hp_data_.last_controller_frame.value() > last_any_frame_ms)) {
        last_any_frame_ms = this->hp_data_.last_controller_frame.value();
        have_any_frame = true;
    }
    bool comm_lost = !have_any_frame || (millis() - last_any_frame_ms > COMM_LOST_TIMEOUT_MS);
    // Publish on the very first update() too, not only on state changes --
    // otherwise, if the bus is already healthy when the sensor initialises,
    // it never transitions and Home Assistant is left showing "Unknown"
    // forever despite everything actually working.
    if (comm_lost != this->bus_comm_lost_ || !this->bus_comm_state_known_) {
        this->bus_comm_lost_ = comm_lost;
        this->bus_comm_state_known_ = true;
        if (this->bus_communication_sensor_ != nullptr) {
            this->bus_communication_sensor_->publish_state(comm_lost);
        }
        if (comm_lost) {
            ESP_LOGW(TAG, "CC207S-COMM: no frame received in over %" PRIu32 " ms -- "
                "bus wire, controller power, or connector likely at fault",
                (uint32_t) COMM_LOST_TIMEOUT_MS);
        } else {
            ESP_LOGI(TAG, "CC207S-COMM: communication restored");
        }
    }

    // ---- command acknowledgement watchdog ----------------------------------
    // control() sets command_pending_ when it injects a 0xCC. Here we check
    // whether the most recent 0xD2 (parsed into hp_data_ well before this
    // update() call, since frames arrive independently on the bus) reflects
    // what was requested. Observation only: no automatic retry.
    if (this->command_pending_) {
        bool setpoint_matches = true;
        if (this->pending_target_temperature_.has_value()) {
            float actual = this->hp_data_.target_temperature.value_or(NAN);
            setpoint_matches = !std::isnan(actual) &&
                fabsf(actual - this->pending_target_temperature_.value()) < 0.25f;
        }
        bool mode_matches = true;
        if (this->pending_mode_.has_value()) {
            mode_matches = this->hp_data_.mode.has_value() &&
                this->hp_data_.mode.value() == this->pending_mode_.value();
        }

        if (setpoint_matches && mode_matches) {
            ESP_LOGI(TAG, "CC207S-COMM: last command confirmed by controller echo");
            this->command_pending_ = false;
            if (this->command_not_confirmed_sensor_ != nullptr) {
                // "problem" device class: false = no problem = confirmed.
                this->command_not_confirmed_sensor_->publish_state(false);
            }
        } else if (millis() - this->command_sent_at_ms_ > COMMAND_ACK_TIMEOUT_MS) {
            ESP_LOGW(TAG, "CC207S-COMM: last command NOT confirmed within %" PRIu32 " ms -- "
                "frame may have been lost or overwritten by keypad activity",
                (uint32_t) COMMAND_ACK_TIMEOUT_MS);
            this->command_pending_ = false;
            if (this->command_not_confirmed_sensor_ != nullptr) {
                // true = problem = not confirmed.
                this->command_not_confirmed_sensor_->publish_state(true);
            }
        }
        // else: still within the timeout window, keep waiting silently.
    }

    this->current_temperature = this->hp_data_.current_temperature.value_or(this->current_temperature);
    this->target_temperature = this->hp_data_.target_temperature.value_or(this->target_temperature);
    this->action = this->hp_data_.action.value_or(this->action);

    if (this->hp_data_.mode == climate::CLIMATE_MODE_OFF) {
        this->action = climate::CLIMATE_ACTION_OFF;
    }
    this->mode = this->hp_data_.mode.value_or(this->mode);

    // Decoded fault codes ("OK" or a space-separated list, e.g. "E1", "P4 P5")
    if (this->fault_codes_sensor_ != nullptr) {
        std::string codes =
            this->hp_data_.fault_codes.empty() ? std::string("OK") : this->hp_data_.fault_codes;
        if (codes != this->last_fault_codes_) {
            this->last_fault_codes_ = codes;
            if (this->update_active_) {
                this->fault_codes_sensor_->publish_state(codes);
            }
            if (codes != "OK") {
                ESP_LOGW(TAG, "CC207S: heat pump reporting fault(s): %s", codes.c_str());
            }
        }
    }

    publish_sensor_value(this->hp_data_.t_suction, this->t_suction_sensor_);
    publish_sensor_value(this->hp_data_.t_defrost, this->t_defrost_sensor_);
    publish_sensor_value(this->hp_data_.t_ambient, this->t_ambient_sensor_);
    publish_sensor_value(this->hp_data_.t_discharge, this->t_discharge_sensor_);
    publish_sensor_value(this->hp_data_.unknown_bits_detected, this->unknown_bits_sensor_);
    publish_sensor_value(
        this->hp_data_.compressor_not_compressing, this->compressor_not_compressing_sensor_);
    // hp_data_.compressor_on / fan_on already decoded from data[10] bits
    // 0x02/0x10 in cc207s_frame_telemetry.cpp -- just exposing them here.
    publish_sensor_value(this->hp_data_.compressor_on, this->compressor_on_sensor_);
    publish_sensor_value(this->hp_data_.fan_on, this->fan_on_sensor_);

    climate::Climate::publish_state();
}

void CC207SClimate::dump_config() {
    ESP_LOGCONFIG(TAG, "cc207s:");
    if (this->driver_.get_gpio_pin()) {
        ESP_LOGCONFIG(TAG, "  - txrx_pin: %d", this->driver_.get_gpio_pin()->get_pin());
    }
    ESP_LOGCONFIG(TAG, "  - passive_mode: %s", ONOFF(this->passive_mode_));
    ESP_LOGCONFIG(TAG, "  - update_active: %s", ONOFF(this->update_active_));
    this->driver_.dump_known_packets(TAG);
}

void CC207SClimate::control(const HWPCall& hwpcall) {
    auto ctrl_frames = this->driver_.control(hwpcall);
    if (this->passive_mode_) {
        ESP_LOGW(TAG, "Passive mode. Ignoring inbound changes");
        this->status_momentary_warning("Passive mode. Ignoring changes", 5000);
        this->publish_state();
        return;
    }

    bool success = true;
    for (size_t i = 0; i < ctrl_frames.size(); i++) {
        if (!this->driver_.queue_frame_data(ctrl_frames[i])) {
            success = false;
            ESP_LOGE(TAG, "Control queuing error for control frame %d", (int) i);
        }
    }
    if (!success) {
        this->status_momentary_error("Control queuing error");
        this->publish_state();
        return;
    }

    // Arm the acknowledgement watchdog only if the queuing actually
    // succeeded and something was requested (mirrors what control() was
    // asked for, so update() can compare it against the next real 0xD2).
    if (!ctrl_frames.empty()) {
        this->command_pending_ = true;
        this->command_sent_at_ms_ = millis();
        this->pending_target_temperature_ = hwpcall.get_target_temperature();
        this->pending_mode_ = hwpcall.get_mode();
        if (this->command_not_confirmed_sensor_ != nullptr) {
            // "problem" device class, false = no problem yet. update() will
            // flip this to true only if the ack timeout actually elapses.
            this->command_not_confirmed_sensor_->publish_state(false);
        }
    }
}

void CC207SClimate::control(const climate::ClimateCall& call) {
    HWPCall hwpcall(call, *this, this->hp_data_);
    this->control(hwpcall);
}

climate::ClimateTraits CC207SClimate::traits() {
    auto traits = climate::ClimateTraits();
    traits.set_visual_min_temperature(this->hp_data_.get_min_target());
    traits.set_visual_max_temperature(this->hp_data_.get_max_target());
    traits.set_visual_temperature_step(0.5f);
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
    // Calls CC207SFrameTelemetry::traits() via the registry -- sets
    // supports_current_temperature/supports_action. Kept as a driver call
    // rather than inlined here so a future second frame class (e.g. if a
    // Heat/Cool keypad capture reveals a mode field) can extend traits
    // without touching this function.
    this->driver_.traits(traits, this->hp_data_);
    return traits;
}

}  // namespace hwp
}  // namespace esphome
