/**
 * CC207S-V2.0 pool heat pump controller -- top-level climate component.
 *
 * Replaces PoolHeater.h + switch_helpers.h + helper_components.h + HeaterStatus.h.
 * Consolidated into one file since this project targets exactly one board
 * family (CC207S) rather than the original multi-protocol PC1000/PC1001
 * design. The two switch classes (ActiveModeSwitch, UpdateStatusSwitch) are
 * kept as-is -- their logic never depended on anything Hayward-specific.
 *
 * Dropped from the original: the ~40 Hayward installer-parameter
 * number/select entities (defrost timing, fan speed curves, flow meter
 * calibration...), the GenerateCodeButton dev utility, the never-implemented
 * preferences-restore stub, and HeaterStatus (it only ever reported a
 * permanent "S00" on this board -- see docs/CC207S-protocole.md).
 */
#pragma once

#include "Bus.h"
#include "base_frame.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

// Note: hwp_web_dashboard.h/.cpp are kept in this component folder because
// Bus.cpp (unmodified hardware layer) forward-declares and guardedly calls
// into HWPWebDashboard. This project never instantiates one -- the working
// web interface comes from ESPHome's own standard `web_server:` component,
// which lists every entity automatically. The dashboard files just need to
// exist so Bus.cpp compiles; nothing here activates them.

namespace esphome {
namespace hwp {

class CC207SClimate;

/**
 * @brief Base for switches that control CC207SClimate behaviour.
 * Restores its state on boot per its configured restore_mode, then applies
 * that state to the parent immediately.
 */
class CC207SSwitch : public switch_::Switch, public Component, public Parented<CC207SClimate> {
  public:
    CC207SSwitch() = default;
    using Parented<CC207SClimate>::Parented;

    void setup() override {
        this->state = this->get_initial_state_with_restore_mode().value_or(false);
        this->write_state(state);
    }

  protected:
    virtual void write_state(bool state) override;
};

/// @brief ON = commands built from Home Assistant are injected as 0xCC
/// frames. OFF = read-only/passive: changes are logged but never sent.
class ActiveModeSwitch : public CC207SSwitch {
  protected:
    void write_state(bool new_state) override;
};

/// @brief ON = sensor values are published on every update. OFF blocks all
/// sensor publication (kept for parity with the original component; there
/// is little reason to ever turn this off on a CC207S installation).
class UpdateStatusSwitch : public CC207SSwitch {
  protected:
    void write_state(bool new_state) override;
};

class CC207SClimate : public climate::Climate, public PollingComponent {
  public:
    explicit CC207SClimate(InternalGPIOPin* gpio_pin);

    void setup() override;
    void loop() override;
    void update() override;
    void dump_config() override;
    climate::ClimateTraits traits() override;

    void set_passive_mode(bool passive) { this->passive_mode_ = passive; this->publish_state(); }
    void set_update_active(bool active) { this->update_active_ = active; }
    bool get_passive_mode() const { return this->passive_mode_; }
    bool is_update_active() const { return this->update_active_; }

    // ---- optional entity setters, called from __init__.py codegen ---------
    void set_t_suction_sensor(sensor::Sensor* s) { this->t_suction_sensor_ = s; }
    void set_t_defrost_sensor(sensor::Sensor* s) { this->t_defrost_sensor_ = s; }
    void set_t_ambient_sensor(sensor::Sensor* s) { this->t_ambient_sensor_ = s; }
    void set_t_discharge_sensor(sensor::Sensor* s) { this->t_discharge_sensor_ = s; }
    void set_fault_codes_sensor(text_sensor::TextSensor* s) { this->fault_codes_sensor_ = s; }
    void set_unknown_bits_detected_sensor(binary_sensor::BinarySensor* s) {
        this->unknown_bits_sensor_ = s;
    }
    void set_compressor_not_compressing_sensor(binary_sensor::BinarySensor* s) {
        this->compressor_not_compressing_sensor_ = s;
    }
    /// @brief Fires if no telemetry/config frame has been received for
    /// COMM_LOST_TIMEOUT_MS. Distinct from is_heater_offline() (which only
    /// sets an internal warning flag, never exposed as an entity) -- this
    /// is the entity a user can actually see and get notified on.
    void set_bus_communication_sensor(binary_sensor::BinarySensor* s) {
        this->bus_communication_sensor_ = s;
    }
    /// @brief Fires if the last injected 0xCC command was not reflected by
    /// the controller's next 0xD2 echo within COMMAND_ACK_TIMEOUT_MS.
    /// Observation only -- does not retry the command (see
    /// docs/CC207S-protocole.md, "reste a faire" for a future retry-based
    /// version inspired by the ECOLO-80 project).
    void set_command_not_confirmed_sensor(binary_sensor::BinarySensor* s) {
        this->command_not_confirmed_sensor_ = s;
    }
    /// @brief Relay state indicators (data[10] bits 0x02/0x10). Added
    /// 15 Sept 2026 to let sensor-fault occurrences (P3/P4) be correlated
    /// against real compressor/fan start-stop transients from HA history.
    void set_compressor_on_sensor(binary_sensor::BinarySensor* s) {
        this->compressor_on_sensor_ = s;
    }
    void set_fan_on_sensor(binary_sensor::BinarySensor* s) {
        this->fan_on_sensor_ = s;
    }
    void set_start_bus_on_setup(bool v) { this->start_bus_on_setup_ = v; }
    InternalGPIOPin* get_gpio_pin() { return this->driver_.get_gpio_pin(); }

  protected:
    void control(const climate::ClimateCall& call) override;
    void control(const HWPCall& call);

    bool is_heater_offline() const {
        return !this->hp_data_.last_heater_frame.has_value() ||
               (millis() - this->hp_data_.last_heater_frame.value()) > 30000;
    }

    template <typename T, typename U>
    void publish_sensor_value(const optional<U>& value, T* sensor) {
        if (sensor == nullptr || !this->update_active_ || !value.has_value()) return;
        sensor->publish_state(value.value());
    }

    Bus driver_;
    heat_pump_data_t hp_data_;

    bool passive_mode_{false};
    bool update_active_{true};
    bool start_bus_on_setup_{true};

    // ---- bus communication watchdog ----------------------------------------
    // Conservative on purpose: the controller ticks every ~6s, so 60s tolerates
    // roughly ten missed/garbled frames before alarming -- long enough to ride
    // out a brief glitch without a false alarm, short enough to catch a real
    // break (e.g. a soldered bus wire snapping, as happened during testing).
    static constexpr uint32_t COMM_LOST_TIMEOUT_MS = 60000;
    bool bus_comm_lost_{false};
    bool bus_comm_state_known_{false}; ///< true once update() has run at least once

    // ---- last-command acknowledgement tracking -----------------------------
    // Set when control() injects a 0xCC; cleared as soon as a 0xD2 echo
    // matching the requested setpoint/power is parsed, or flagged as
    // unconfirmed if that doesn't happen within the timeout. This is
    // observation only: unlike the ECOLO-80 project's retry logic, nothing
    // here re-sends the command automatically.
    static constexpr uint32_t COMMAND_ACK_TIMEOUT_MS = 15000;
    bool command_pending_{false};
    uint32_t command_sent_at_ms_{0};
    optional<float> pending_target_temperature_;
    optional<climate::ClimateMode> pending_mode_;

    sensor::Sensor* t_suction_sensor_{nullptr};
    sensor::Sensor* t_defrost_sensor_{nullptr};
    sensor::Sensor* t_ambient_sensor_{nullptr};
    sensor::Sensor* t_discharge_sensor_{nullptr};
    text_sensor::TextSensor* fault_codes_sensor_{nullptr};
    binary_sensor::BinarySensor* unknown_bits_sensor_{nullptr};
    binary_sensor::BinarySensor* compressor_not_compressing_sensor_{nullptr};
    binary_sensor::BinarySensor* bus_communication_sensor_{nullptr};   ///< CC207S bus-alive alert
    binary_sensor::BinarySensor* command_not_confirmed_sensor_{nullptr};   ///< CC207S last-command-ack alert
    binary_sensor::BinarySensor* compressor_on_sensor_{nullptr};   ///< CC207S data[10] bit 0x02
    binary_sensor::BinarySensor* fan_on_sensor_{nullptr};          ///< CC207S data[10] bit 0x10

    std::string last_fault_codes_;
};

}  // namespace hwp
}  // namespace esphome
