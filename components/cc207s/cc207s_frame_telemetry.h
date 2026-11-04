/**
 * CC207S-V2.0 -- 0xDD telemetry frame (heater -> keypad).
 *
 * Replaces FrameConditionsD.h/.cpp. Same packet layout as before; the class
 * is renamed and stripped of the Hayward `cond_d_t` struct-field naming
 * (unknown_1..unknown_12), which never matched what the raw bytes actually
 * contain on this bus (see the CC207S-protocole.md byte map). Parsing reads
 * packet.data[N] directly instead, exactly as validated all week.
 */
#pragma once

#include "Schema.h"
#include "base_frame.h"

namespace esphome {
namespace hwp {

class CC207SFrameTelemetry : public BaseFrame {
  public:
    static constexpr uint8_t FRAME_ID = 0xDD;

    CC207SFrameTelemetry() : BaseFrame() {}
    CC207SFrameTelemetry(const BaseFrame& base) : BaseFrame(base) {}
    CC207SFrameTelemetry(const CC207SFrameTelemetry& other) : BaseFrame(other) {}
    template <size_t N>
    CC207SFrameTelemetry(const unsigned char (&cmdTrame)[N]) : BaseFrame(cmdTrame) {}

    static std::shared_ptr<BaseFrame> create();
    static bool matches(BaseFrame& specialized, BaseFrame& base);
    bool matches(BaseFrame& base) { return matches(*this, base); }

    void parse(heat_pump_data_t& hp_data) override;
    void traits(climate::ClimateTraits& traits, heat_pump_data_t& hp_data) override;
    const char* type_string() const override { return "CC207S_TELEMETRY"; }

    // Not a command frame -- the heater never receives a control() call
    // targeting 0xDD; setpoint/power changes go through CC207SFrameConfig.
    optional<std::shared_ptr<BaseFrame>> control(const HWPCall& call) override { return nullopt; }
};

}  // namespace hwp
}  // namespace esphome
