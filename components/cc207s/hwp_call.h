/**
 * CC207S-V2.0: slimmed version of hwp_call.h.
 *
 * The original carried ~20 optional fields for Hayward PC1000/PC1001
 * installer parameters, plus a text_sensor::TextSensor& "status" reference
 * that was tied to the never-really-functional HeaterStatus feature. Neither
 * this decode's control() implementation (setpoint + power only) touches
 * either, so both are dropped. Only the base shape needed by
 * BaseFrame::control(const HWPCall&) remains: a ClimateCall carrying a
 * reference to the shared data model.
 */
#pragma once
#include "Schema.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace hwp {

class HWPCall : public climate::ClimateCall {
  public:
    HWPCall(climate::Climate* parent, Component& component, heat_pump_data_t& hp_data)
        : climate::ClimateCall(parent), component(component), hp_data(hp_data) {}
    HWPCall(const climate::ClimateCall& climate_call, Component& component, heat_pump_data_t& hp_data)
        : climate::ClimateCall(climate_call), component(component), hp_data(hp_data) {}
    climate::Climate& get_parent() { return *parent_; }

    esphome::Component& component;
    heat_pump_data_t& hp_data;
};

}  // namespace hwp
}  // namespace esphome
