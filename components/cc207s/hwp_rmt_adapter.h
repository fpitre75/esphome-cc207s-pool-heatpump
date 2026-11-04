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

#include <cstdint>

namespace esphome {
namespace hwp {

struct hwp_pulse_symbol_t {
    uint32_t duration0;
    uint32_t level0;
    uint32_t duration1;
    uint32_t level1;
};

// Classic ESP32 derives RMT from an 80 MHz source with a maximum divider of 256,
// so 312.5 kHz is the lowest valid resolution on that target.
static constexpr uint32_t hwp_rmt_resolution_hz = 312500;
static constexpr uint32_t hwp_rmt_max_symbol_ticks = 0x7FFF;

constexpr uint32_t hwp_rmt_ticks_to_us(uint32_t ticks) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(ticks) * 1000000ULL + hwp_rmt_resolution_hz / 2) /
        hwp_rmt_resolution_hz);
}

constexpr uint32_t hwp_rmt_us_to_ticks(uint32_t duration_us) {
    uint64_t ticks =
        (static_cast<uint64_t>(duration_us) * hwp_rmt_resolution_hz + 1000000ULL - 1) /
        1000000ULL;
    if (ticks == 0) {
        return 1;
    }
    if (ticks > hwp_rmt_max_symbol_ticks) {
        return hwp_rmt_max_symbol_ticks;
    }
    return static_cast<uint32_t>(ticks);
}

static constexpr uint32_t hwp_rmt_max_symbol_us =
    hwp_rmt_ticks_to_us(hwp_rmt_max_symbol_ticks);

} // namespace hwp
} // namespace esphome

#ifdef HWP_NATIVE_TEST

#include <cstddef>

using esp_err_t = int;
static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;
static constexpr int RMT_CLK_SRC_DEFAULT = 0;

using rmt_channel_handle_t = void*;
using rmt_encoder_handle_t = void*;
using gpio_num_t = int;

struct rmt_symbol_word_t {
    union {
        struct {
            uint32_t duration0 : 15;
            uint32_t level0 : 1;
            uint32_t duration1 : 15;
            uint32_t level1 : 1;
        };
        uint32_t val;
    };
};

struct rmt_rx_done_event_data_t {
    rmt_symbol_word_t* received_symbols;
    size_t num_symbols;
};

using rmt_rx_done_callback_t =
    bool (*)(rmt_channel_handle_t, const rmt_rx_done_event_data_t*, void*);

struct rmt_rx_event_callbacks_t {
    rmt_rx_done_callback_t on_recv_done;
};

struct rmt_rx_channel_config_t {
    gpio_num_t gpio_num;
    int clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    int intr_priority;
    struct {
        uint32_t invert_in : 1;
        uint32_t with_dma : 1;
        uint32_t io_loop_back : 1;
        uint32_t allow_pd : 1;
    } flags;
};

struct rmt_receive_config_t {
    uint32_t signal_range_min_ns;
    uint32_t signal_range_max_ns;
    struct {
        uint32_t en_partial_rx : 1;
    } flags;
};

struct rmt_tx_channel_config_t {
    gpio_num_t gpio_num;
    int clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
    int intr_priority;
    struct {
        uint32_t invert_out : 1;
        uint32_t with_dma : 1;
        uint32_t io_loop_back : 1;
        uint32_t io_od_mode : 1;
        uint32_t allow_pd : 1;
        uint32_t init_level : 1;
    } flags;
};

struct rmt_copy_encoder_config_t {};

struct rmt_transmit_config_t {
    int loop_count;
    struct {
        uint32_t eot_level : 1;
        uint32_t queue_nonblocking : 1;
    } flags;
};

#else

#include <driver/rmt_common.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>

#endif
