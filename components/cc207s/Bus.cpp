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
#include "Bus.h"
#include <memory>

#include "HPUtils.h"
#include "base_frame.h"
#include "hwp_logger_adapter.h"
#include "hwp_web_dashboard.h"
#ifndef HWP_NATIVE_TEST
#include "esphome/core/hal.h"
#endif

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef USE_ARDUINO
#include <esp32-hal.h>
#endif
#else
#define millis (uint32_t)(esp_timer_get_time() / 1000)

#endif

namespace esphome {
namespace hwp {
const uint8_t default_frame_transmit_count = 8;
// max duration would be all bits having their longest duration, plus the frame spacing and
// frame heading
const uint32_t single_frame_max_duration_ms =
    frame_data_length * 8 * (bit_long_high_duration_ms + bit_low_duration_ms) +
    controler_frame_spacing_duration_ms + frame_heading_total_duration_ms;

Bus::Bus(size_t maxWriteLength, size_t transmitCount)
    : mode(BUSMODE_RX), transmit_count(transmitCount),
      // maxBufferCount(maxBufferCount),
      maxWriteLength(maxWriteLength), tx_packets_queue(maxWriteLength) {}

rmt_symbol_word_t Bus::make_rmt_symbol_us(uint32_t low_us, uint32_t high_us) {
    rmt_symbol_word_t symbol{};
    symbol.level0 = 0;
    symbol.duration0 = hwp_rmt_us_to_ticks(low_us);
    symbol.level1 = 1;
    symbol.duration1 = hwp_rmt_us_to_ticks(high_us);
    return symbol;
}

rmt_symbol_word_t Bus::make_rmt_symbol_ms(uint32_t low_ms, uint32_t high_ms) {
    return make_rmt_symbol_us(low_ms * 1000, high_ms * 1000);
}

hwp_pulse_symbol_t Bus::normalize_symbol(const rmt_symbol_word_t& symbol) {
    return hwp_pulse_symbol_t{
        hwp_rmt_ticks_to_us(symbol.duration0),
        static_cast<uint32_t>(symbol.level0),
        hwp_rmt_ticks_to_us(symbol.duration1),
        static_cast<uint32_t>(symbol.level1),
    };
}

bool Bus::setup_rmt() {
#ifdef HWP_NATIVE_TEST
    return false;
#else
    gpio_num_t gpio_num = static_cast<gpio_num_t>(this->gpio_pin_->get_pin());
    this->rmt_receive_config_ = {};
    this->rmt_receive_config_.signal_range_min_ns = 1000;
    this->rmt_receive_config_.signal_range_max_ns = hwp_rmt_max_symbol_us * 1000;
    this->rmt_transmit_config_ = {};
    this->rmt_transmit_config_.loop_count = 0;
    this->rmt_transmit_config_.flags.eot_level = 1;

    if (this->rmt_rx_channel_ == nullptr) {
        rmt_rx_channel_config_t rx_config = {};
        rx_config.gpio_num = gpio_num;
        rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
        rx_config.resolution_hz = hwp_rmt_resolution_hz;
        rx_config.mem_block_symbols = 128;
        rx_config.intr_priority = 0;
        esp_err_t err = rmt_new_rx_channel(&rx_config, &this->rmt_rx_channel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to create RMT RX channel: %d", err);
            return false;
        }

        rmt_rx_event_callbacks_t callbacks = {};
        callbacks.on_recv_done = &Bus::rmt_rx_done_callback;
        err = rmt_rx_register_event_callbacks(this->rmt_rx_channel_, &callbacks, this);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to register RMT RX callback: %d", err);
            return false;
        }
    }

    if (this->rmt_tx_channel_ == nullptr) {
        rmt_tx_channel_config_t tx_config = {};
        tx_config.gpio_num = gpio_num;
        tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
        tx_config.resolution_hz = hwp_rmt_resolution_hz;
        tx_config.mem_block_symbols = 128;
        tx_config.trans_queue_depth = 1;
        tx_config.intr_priority = 0;
        tx_config.flags.io_od_mode = 1;
        tx_config.flags.init_level = 1;
        esp_err_t err = rmt_new_tx_channel(&tx_config, &this->rmt_tx_channel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to create RMT TX channel: %d", err);
            return false;
        }

        rmt_copy_encoder_config_t copy_config = {};
        err = rmt_new_copy_encoder(&copy_config, &this->rmt_copy_encoder_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to create RMT copy encoder: %d", err);
            return false;
        }

        err = rmt_enable(this->rmt_tx_channel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to enable RMT TX channel: %d", err);
            return false;
        }
        this->rmt_tx_enabled_ = true;
    }
    return true;
#endif
}

bool Bus::arm_receive() {
#ifdef HWP_NATIVE_TEST
    return false;
#else
    if (this->rmt_rx_channel_ == nullptr) {
        return false;
    }
    esp_err_t err = rmt_receive(this->rmt_rx_channel_, this->rmt_rx_symbols_.data(),
        this->rmt_rx_symbols_.size() * sizeof(rmt_symbol_word_t), &this->rmt_receive_config_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_BUS, "Failed to arm RMT RX: %d", err);
        return false;
    }
    return true;
#endif
}

void Bus::stop_receive() {
#ifndef HWP_NATIVE_TEST
    if (this->rmt_rx_channel_ != nullptr && this->rmt_rx_enabled_) {
        rmt_disable(this->rmt_rx_channel_);
        this->rmt_rx_enabled_ = false;
    }
#endif
}

void Bus::resume_receive() {
#ifndef HWP_NATIVE_TEST
    if (this->rmt_rx_channel_ == nullptr) {
        return;
    }
    if (!this->rmt_rx_enabled_) {
        esp_err_t err = rmt_enable(this->rmt_rx_channel_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_BUS, "Failed to enable RMT RX channel: %d", err);
            this->mode = BUSMODE_ERROR;
            return;
        }
        this->rmt_rx_enabled_ = true;
    }
    this->arm_receive();
#endif
}

bool IRAM_ATTR Bus::rmt_rx_done_callback(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* event_data, void* user_data) {
    Bus* instance = static_cast<Bus*>(user_data);
    if (instance == nullptr || event_data == nullptr || event_data->received_symbols == nullptr ||
        instance->rb_ == nullptr) {
        return false;
    }
    const size_t symbols_size = event_data->num_symbols * sizeof(rmt_symbol_word_t);
    if (symbols_size == 0) {
        return false;
    }

    BaseType_t high_task_wakeup = pdFALSE;
    if (xRingbufferSendFromISR(
            instance->rb_, event_data->received_symbols, symbols_size, &high_task_wakeup) !=
        pdTRUE) {
        instance->rx_overflow_count_++;
    }
    return high_task_wakeup == pdTRUE;
}

void Bus::setup() {
    this->current_frame.reset("From setup");
    start_receive();
}
void IRAM_ATTR Bus::process_pulse(hwp_pulse_symbol_t* item) {
    if (!item) return;
    this->log_pulse_item(item);
    if (Decoder::is_start_frame(item)) {
        ESP_LOGVV(TAG_BUS, "Start frame detected");
        if (this->current_frame.is_complete()) {
            ESP_LOGV(TAG_BUS, "Finalizing complete frame");
            this->finalize_frame(false);
        } else if (this->current_frame.is_started()) {
            ESP_LOGV(TAG_BUS, "Resetting started frame");
            if (!(this->current_frame.is_long_frame() || this->current_frame.is_short_frame())) {
                ESP_LOGV(TAG_BUS, "Invalid length");
            } else if (this->current_frame.is_size_valid()) {
                this->current_frame.debug("Starting new frame");
            } else if (!this->current_frame.is_checksum_valid()) {
                ESP_LOGV(TAG_BUS, "Invalid checksum");
                BaseFrame inv_bf = BaseFrame(this->current_frame);
                inv_bf.inverse();
                ESP_LOGV(TAG_BUS, "    checksum: %s",
                    this->current_frame.packet.explain_checksum().c_str());
                ESP_LOGV(TAG_BUS, "Inv checksum: %s", inv_bf.packet.explain_checksum().c_str());
            }
        }
        this->reset_pulse_log();
        // log the frame start
        this->log_pulse_item(item);
        this->current_frame.start_new_frame();
    } else {
        if (this->current_frame.is_started()) {
            bool is_long = Decoder::is_long_bit(item);
            bool is_short = Decoder::is_short_bit(item);
            if (is_long || is_short) {
                // Long bit detected
                this->current_frame.append_bit(is_long);
            } else {
                if (Decoder::is_frame_end(item)) {
                    if (this->current_frame.is_complete()) {
                        this->finalize_frame(true);
                    } else {
                        ESP_LOGVV(TAG_BUS, "Bus idle state detected (1:%dus/0:%d)",
                            Decoder::get_high_duration(item), Decoder::get_low_duration(item));
                        this->log_pulses();
                    }
                } else {
                    if (this->current_frame.is_complete()) {
                        this->finalize_frame(true);
                        this->reset_pulse_log();
                    } else {
                        // Invalid length, possibly due to collisions
                        ESP_LOGV(TAG_BUS, "Invalid pulse length detected (1:%dus/0:%d)",
                            Decoder::get_high_duration(item), Decoder::get_low_duration(item));
                        if (this->current_frame.is_size_valid()) {
                            this->current_frame.debug("Invalid frame - ");
                        }
                        this->current_frame.reset("Invalid pulse and invalid frame");
                        this->log_pulses();
                    }
                }
            }
        }
    }
}

bool Bus::queue_frame_data(std::shared_ptr<BaseFrame> frame) {
    if (frame == nullptr) {
        ESP_LOGE(TAG_BUS, "Cannot queue null frame for transmission");
        return false;
    }
    ESP_LOGD(TAG_BUS, "Queueing frame data for transmission");
    return tx_packets_queue.enqueue(frame);
}
void Bus::start_receive() {
    this->current_frame.reset();
    if (this->gpio_pin_ == nullptr) {
        ESP_LOGE(TAG_BUS, "Invalid pin. Cannot start receive");
        this->mode = BUSMODE_ERROR;

    } else {
        ESP_LOGI(TAG_BUS, "Starting reception on pin %d", this->gpio_pin_->get_pin());

        if (this->RxTaskHandle == nullptr) {
            size_t ring_buf_size = 12 * frame_data_length * (8 + 2) * sizeof(rmt_symbol_word_t);
            this->rb_ = xRingbufferCreate(ring_buf_size, RINGBUF_TYPE_NOSPLIT);
            if (this->rb_ == nullptr) {
                ESP_LOGE(TAG_BUS, "Failed to create RX ring buffer");
                this->mode = BUSMODE_ERROR;
                return;
            }
            ESP_LOGD(TAG_BUS, "Created ring buffer with size %u (frame data length %u)",
                ring_buf_size, frame_data_length);

            if (!this->setup_rmt()) {
                vRingbufferDelete(this->rb_);
                this->rb_ = nullptr;
                this->mode = BUSMODE_ERROR;
                return;
            }

            ESP_LOGD(TAG_BUS, "Creating io Tasks");
            if (xTaskCreate(RxTask, "RX", 1024 * 11, this, 1, &this->RxTaskHandle) != pdPASS) {
                ESP_LOGE(TAG_BUS, "Failed to create RX task");
                vRingbufferDelete(this->rb_);
                this->rb_ = nullptr;
                this->mode = BUSMODE_ERROR;
                return;
            }
            if (xTaskCreate(TxTask, "TX", 1024 * 15, this, 1, &this->TxTaskHandle) != pdPASS) {
                ESP_LOGE(TAG_BUS, "Failed to create TX task");
                vTaskDelete(this->RxTaskHandle);
                this->RxTaskHandle = nullptr;
                vRingbufferDelete(this->rb_);
                this->rb_ = nullptr;
                this->mode = BUSMODE_ERROR;
                return;
            }
        }
        this->current_frame.reset();
        this->reset_pulse_log();
        this->resume_receive();
        ESP_LOGI(TAG_BUS, "Done Starting reception on pin %d", this->gpio_pin_->get_pin());
        this->mode = BUSMODE_RX;
    }
}

bool Bus::has_time_to_send() {
    bool result = false;
    static unsigned long last_send_time = 0; // Tracks the last time a message was sent

    unsigned long current_time = millis();
    uint32_t end_of_transmit = current_time + this->transmit_count * single_frame_max_duration_ms;

    if (this->has_controller() && this->is_controller_timeout()) {
        return true; // assume controller was disconnected
    }

    if (this->next_controller_packet().has_value()) {
        auto next = this->next_controller_packet().value();
        result = (end_of_transmit < next);
    } else {
        result = true;
    }

    // Check if at least 1 second has passed since the last message
    if (current_time - last_send_time < 1000) {
        ESP_LOGV(TAG_BUS, "Has time to send: %s. (next: %d, needed: %d)", result ? "true" : "false",
            this->next_controller_packet().has_value()
                ? (this->next_controller_packet().value() / 1000)
                : 0,
            end_of_transmit / 1000);
        last_send_time = current_time;
    }

    return result;
}

bool Bus::transmit_frame(BaseFrame& packet) {
#ifdef HWP_NATIVE_TEST
    return false;
#else
    if (this->rmt_tx_channel_ == nullptr || this->rmt_copy_encoder_ == nullptr) {
        ESP_LOGE(TAG_BUS, "RMT TX channel is not ready");
        return false;
    }

    std::vector<rmt_symbol_word_t> symbols;
    symbols.reserve(this->transmit_count * (1 + packet.size() * 8 + 1) + 1);

    for (size_t repeat = 0; repeat < this->transmit_count; ++repeat) {
        symbols.push_back(make_rmt_symbol_ms(frame_heading_low_duration_ms, frame_heading_high_duration_ms));
        for (size_t byte_index = 0; byte_index < packet.size(); ++byte_index) {
            for (int bit_index = 0; bit_index <= 7; ++bit_index) {
                symbols.push_back(make_rmt_symbol_ms(bit_low_duration_ms,
                    get_bit(packet[byte_index], bit_index) ? bit_long_high_duration_ms
                                                           : bit_short_high_duration_ms));
            }
        }
        if (repeat + 1 < this->transmit_count) {
            symbols.push_back(
                make_rmt_symbol_ms(bit_low_duration_ms, controler_frame_spacing_duration_ms));
        }
    }
    symbols.push_back(make_rmt_symbol_ms(bit_low_duration_ms, controler_group_spacing_ms));

    this->stop_receive();
    esp_err_t err = rmt_transmit(this->rmt_tx_channel_, this->rmt_copy_encoder_, symbols.data(),
        symbols.size() * sizeof(rmt_symbol_word_t), &this->rmt_transmit_config_);
    if (err == ESP_OK) {
        const int timeout_ms =
            static_cast<int>(this->transmit_count * single_frame_max_duration_ms + 1000);
        err = rmt_tx_wait_all_done(this->rmt_tx_channel_, timeout_ms);
    }
    this->resume_receive();

    if (err != ESP_OK) {
        ESP_LOGE(TAG_BUS, "RMT transmit failed: %d", err);
        return false;
    }
    return true;
#endif
}

void Bus::process_send_queue() {
    std::shared_ptr<BaseFrame> packet;

    if (!this->tx_packets_queue.has_next()) return;
    this->tx_packets_queue.logging_enabled = true;
    if (this->current_frame.is_started()) {
        ESP_LOGV(TAG_BUS, "Packet being received. waiting");
        return;
    }
    if (!this->is_time_for_next()) {
        ESP_LOGD(TAG_BUS, "Queue has next but need to throttle. Waiting.");
        return;
    }

    if (!this->has_time_to_send()) {
        ESP_LOGW(TAG_BUS, "No time to send before next panel message. Waiting.");
        return;
    }
    ESP_LOGI(TAG_BUS, "Retrieving packet from queue");
    if (this->tx_packets_queue.try_dequeue(&packet)) {
        ESP_LOGI(TAG_BUS, "Packet received, type: %s", packet->type_string());
        this->mode = BUSMODE_TX;
        ESP_LOGD(TAG_BUS, "Resetting existing packet (if any)");
        this->current_frame.reset("TX Start");
        this->reset_pulse_log();
        packet->print("SEND", TAG_BUS, ESPHOME_LOG_LEVEL_INFO, __LINE__);
        if (this->web_dashboard_ != nullptr) {
            this->web_dashboard_->record_packet(*packet, "SEND");
        }
        if (!this->transmit_frame(*packet)) {
            this->mode = BUSMODE_ERROR;
            return;
        }
        this->previous_sent_packet_ = millis();
        this->current_frame.reset("TX Done");
        this->reset_pulse_log();
        this->mode = BUSMODE_RX;
    }
}
void IRAM_ATTR Bus::finalize_frame(bool timeout) {
    if (this->hp_data_ == nullptr) {
        ESP_LOGE(TAG_BUS, "Cannot finalize frame before data model is attached");
        this->current_frame.reset("Missing data model");
        this->mode = BUSMODE_ERROR;
        return;
    }
    auto finalized_frame = this->current_frame.finalize(*this->hp_data_);
    if (finalized_frame) {
        ESP_LOGVV(TAG_BUS, "New Frame finalized %s", timeout ? "after timeout" : "");
        if (this->web_dashboard_ != nullptr) {
            this->web_dashboard_->record_packet(*finalized_frame, finalized_frame->is_changed() ? "Chg" : "Same");
            this->web_dashboard_->update_fields(*this->hp_data_, "", this->mode);
        }
        if (finalized_frame->get_source() == SOURCE_CONTROLLER) {
            this->controler_packets_received_ = true;
            this->previous_controller_packet_time_ = millis();
        }
        // Reset the current frame for the next sequence
        this->current_frame.reset();
        this->reset_pulse_log();
    } else if (timeout) {
        // Reset the frame in case of a timeout
        this->log_pulses();
        this->current_frame.reset("Timeout - ");
    }
}
void Bus::dump_known_packets(const char* caller_tag) {

    // BaseFrame::dump_known_packets(caller_tag);

    BaseFrame::dump_c_code(caller_tag);
}
void Bus::sendHeader() {
    if (this->gpio_pin_ == nullptr) return;

    // ESP_LOGV(TAG_BUS, "Sending frame header");

    _sendLow(frame_heading_low_duration_ms);
    _sendHigh(frame_heading_high_duration_ms);
}

void Bus::TxTask(void* arg) {
    Bus* instance = static_cast<Bus*>(arg);
    ESP_LOGD(TAG_BUS, "Starting TxTask for bus on GPIO%d", instance->gpio_pin_->get_pin());
    instance->tx_packets_queue.set_task_handle(xTaskGetCurrentTaskHandle());
    ESP_LOGD(TAG_BUS, "Waiting for 15s");
    delay(15000);
    while (true) {
        instance->process_send_queue();
        delay(1500);
    }
}
void Bus::RxTask(void* arg) {
    Bus* instance = static_cast<Bus*>(arg);
    size_t rx_size = 0;
    bool received_msg = true; // force display at least once
    instance->mode = BUSMODE_RX;

    while (true) {
        if (instance->rx_overflow_count_ > 0) {
            uint32_t overflows = instance->rx_overflow_count_;
            instance->rx_overflow_count_ = 0;
            ESP_LOGW(TAG_BUS, "Dropped %u RMT RX batch(es); RX ring buffer full", overflows);
        }

        auto* items = reinterpret_cast<rmt_symbol_word_t*>(xRingbufferReceive(
            instance->rb_, &rx_size, 120 * portTICK_PERIOD_MS));

        if (items && rx_size > 0) {
            size_t count = static_cast<size_t>(rx_size / sizeof(rmt_symbol_word_t));
            if (instance->mode == BUSMODE_RX) {
                instance->current_frame.passes_count++;

                // ESP_LOGVV(TAG_BUS, "Received %d bytes from the ring buffer (%d items)", rx_size,
                // count);
                for (size_t i = 0; i < count; i++) {
                    hwp_pulse_symbol_t pulse = normalize_symbol(items[i]);
                    instance->process_pulse(&pulse);
                    if (instance->current_frame.is_complete()) {
                        instance->finalize_frame(false);
                    }
                }
                received_msg = true;
            } else {
                ESP_LOGD(TAG_BUS,
                    "Received %d frames from the ring buffer. Ignoring since mode is not RX",
                    count);
            }
            // Return the items to the ring buffer
            vRingbufferReturnItem(instance->rb_, (void*)items);
            if (instance->mode == BUSMODE_RX) {
                instance->arm_receive();
            }
        } else {
            if (received_msg) {
                ESP_LOGVV(TAG_BUS, "No item received from the ring buffer");
                // only display once
                received_msg = false;
            }
        }
    }

    vTaskDelete(NULL);
}
std::vector<std::shared_ptr<BaseFrame>> Bus::control(const HWPCall& call) {
    auto& registry = BaseFrame::get_registry();

    std::vector<std::shared_ptr<BaseFrame>> result;
    for (size_t i = 0; i < registry.size(); i++) {
        auto frame = registry[i].instance->control(call);
        if (frame.has_value()) {
            frame.value()->print("PUSH", TAG_BUS, ESPHOME_LOG_LEVEL_VERBOSE, __LINE__);
            result.push_back(frame.value());
        }
    }
    return result;
}

void Bus::traits(climate::ClimateTraits& traits, heat_pump_data_t& hp_data) {
    auto& registry = BaseFrame::get_registry();
    for (size_t i = 0; i < registry.size(); i++) {
        registry[i].instance->traits(traits, hp_data);
    }
}

} // namespace hwp
} // namespace esphome
