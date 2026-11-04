/**
 * CC207S-V2.0 -- 0xD2/0xCC configuration and command frame.
 *
 * Replaces FrameConditions2.h/.cpp. 0xD2 (controller broadcast) and 0xCC
 * (keypad command) carry an identical 12-byte payload -- confirmed byte for
 * byte, same checksum -- so one class handles both.
 */
#pragma once

#include "Schema.h"
#include "base_frame.h"

namespace esphome {
namespace hwp {

class CC207SFrameConfig : public BaseFrame {
  public:
    static constexpr uint8_t FRAME_ID_CONFIG = 0xD2;      ///< controller broadcast
    static constexpr uint8_t FRAME_ID_COMMAND = 0xCC;     ///< keypad command

    CC207SFrameConfig() : BaseFrame() {}
    CC207SFrameConfig(const BaseFrame& base) : BaseFrame(base) {}
    CC207SFrameConfig(const CC207SFrameConfig& other) : BaseFrame(other) {}
    template <size_t N>
    CC207SFrameConfig(const unsigned char (&cmdTrame)[N]) : BaseFrame(cmdTrame) {}

    static std::shared_ptr<BaseFrame> create();
    static bool matches(BaseFrame& specialized, BaseFrame& base);
    bool matches(BaseFrame& base) { return matches(*this, base); }

    void parse(heat_pump_data_t& hp_data) override;
    optional<std::shared_ptr<BaseFrame>> control(const HWPCall& call) override;
    const char* type_string() const override { return "CC207S_CONFIG"; }
};

}  // namespace hwp
}  // namespace esphome
