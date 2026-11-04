/**
 * @file hwp_web_dashboard.h
 * @brief Firmware-served HWP diagnostic dashboard support.
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
 * copies or substantial portions of the Software.
 */

#pragma once

#include "Schema.h"
#include "base_frame.h"
#include "hwp_bus_types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef HWP_NATIVE_TEST
#include "esphome/core/component.h"
#ifdef USE_WEBSERVER
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#endif
#endif

namespace esphome {
namespace hwp {

static constexpr size_t HWP_WEB_DEFAULT_PACKET_BUFFER_SIZE = 24;
static constexpr size_t HWP_WEB_DEFAULT_GRAPH_HISTORY_SIZE = 240;
static constexpr size_t HWP_WEB_MAX_PACKET_BUFFER_SIZE = 24;
static constexpr size_t HWP_WEB_MAX_GRAPH_HISTORY_SIZE = 24;

struct HWPWebConfig {
    bool enabled{false};
    std::string path{"/hwp"};
    size_t packet_buffer_size{HWP_WEB_DEFAULT_PACKET_BUFFER_SIZE};
    size_t graph_history_size{HWP_WEB_DEFAULT_GRAPH_HISTORY_SIZE};
};

struct HWPWebField {
    std::string id;
    std::string label;
    std::string value;
    std::string unit;
    std::string group;
    std::string frame;
    std::string raw_location;
    std::string source;
    bool numeric{false};
    float numeric_value{0.0f};
    bool changed{false};
    uint32_t seen_ms{0};
};

class HWPWebDashboard {
  public:
    void configure(const HWPWebConfig& config);
#ifndef HWP_NATIVE_TEST
#ifdef USE_WEBSERVER
    void setup(web_server::WebServer* web_server);
#else
    void setup(void* web_server) {}
#endif
    void loop();
#else
    void setup(void* web_server) {}
    void loop() {}
#endif
    bool enabled() const { return this->config_.enabled; }
    const HWPWebConfig& config() const { return this->config_; }

    void record_packet(const BaseFrame& frame, const std::string& kind);
    void update_fields(const heat_pump_data_t& data, const std::string& status, bus_mode_t bus_mode);

    std::string state_json() const;
    std::string graph_state_json() const;
    std::string event_json() const;
    size_t packet_count() const;
    size_t latest_frame_count() const;
    size_t graph_point_count(const std::string& field_id) const;
    static const char* index_html();
    static std::string escape_json(const std::string& value);

  private:
    struct PacketRecord {
        uint32_t sequence{0};
        uint32_t seen_ms{0};
        std::string kind;
        std::string label;
        std::string source;
        std::string frame;
        size_t length{0};
        bool checksum_valid{false};
        bool changed{false};
        std::vector<uint8_t> bytes;
        std::vector<bool> changed_bytes;
    };

    struct GraphPoint {
        uint32_t sequence{0};
        uint32_t seen_ms{0};
        float value{0.0f};
    };

    HWPWebConfig config_{};
    mutable std::mutex data_mutex_{};
    std::vector<PacketRecord> packets_{};
    std::vector<HWPWebField> fields_{};
    std::map<std::string, PacketRecord> latest_frames_{};
    std::map<std::string, std::string> previous_field_values_{};
    std::map<std::string, uint32_t> previous_field_seen_ms_{};
    std::map<std::string, std::vector<uint8_t>> previous_packet_bytes_{};
    std::map<std::string, std::vector<GraphPoint>> graph_history_{};
    uint32_t packet_sequence_{0};
    uint32_t field_sequence_{0};
    uint32_t last_event_ms_{0};
    std::string last_status_{};
    bus_mode_t last_bus_mode_{BUSMODE_RX};
    bool dirty_{false};

#ifndef HWP_NATIVE_TEST
#ifdef USE_WEBSERVER
    web_server::WebServer* web_server_{nullptr};
    web_server_idf::AsyncEventSource* events_{nullptr};
    bool handlers_registered_{false};
#endif
#endif

    void trim_packets();
    void trim_graph(const std::string& field_id);
    void append_field(std::vector<HWPWebField>& fields, HWPWebField field);
    void append_graph_point(const HWPWebField& field);
    std::string packets_json() const;
    std::string frames_json() const;
    static void append_packet_json(std::ostringstream& out, const PacketRecord& packet);
    static void append_fields_json(std::ostringstream& out, const std::vector<HWPWebField>& fields);
    static void append_packets_json(std::ostringstream& out, const std::vector<PacketRecord>& packets);
    static void append_fields_json(std::string& out, const std::vector<HWPWebField>& fields);
    static void append_packets_json(std::string& out, const std::vector<PacketRecord>& packets);
    static void append_packet_json(std::string& out, const PacketRecord& packet);
    static void append_graph_json(
        std::ostringstream& out, const std::map<std::string, std::vector<GraphPoint>>& graphs);
    std::string fields_json() const;
    std::string graph_json() const;
    std::string state_json_(bool include_graphs) const;
    std::string meta_json(const std::string& status, bus_mode_t bus_mode) const;
    static std::string bus_mode_to_string(bus_mode_t mode);
    static std::string frame_source_to_string(frame_source_t source);
    static std::string bytes_to_key(const BaseFrame& frame);
    static std::string bytes_to_json(const std::vector<uint8_t>& bytes);
    static std::string bool_json(bool value) { return value ? "true" : "false"; }
    static HWPWebField make_float_field(const char* id, const char* label, optional<float> value,
        const char* unit, const char* group, const char* frame, const char* raw_location,
        const char* source);
    static HWPWebField make_string_field(const char* id, const char* label, const std::string& value,
        const char* group, const char* frame, const char* raw_location, const char* source);
};

} // namespace hwp
} // namespace esphome
