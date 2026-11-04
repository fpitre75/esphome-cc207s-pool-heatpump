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

#define ESPHOME_LOG_LEVEL_NONE 0
#define ESPHOME_LOG_LEVEL_ERROR 1
#define ESPHOME_LOG_LEVEL_WARN 2
#define ESPHOME_LOG_LEVEL_INFO 3
#define ESPHOME_LOG_LEVEL_CONFIG 4
#define ESPHOME_LOG_LEVEL_DEBUG 5
#define ESPHOME_LOG_LEVEL_VERBOSE 6
#define ESPHOME_LOG_LEVEL_VERY_VERBOSE 7

#define ESP_LOGE(tag, fmt, ...) ((void) 0)
#define ESP_LOGW(tag, fmt, ...) ((void) 0)
#define ESP_LOGI(tag, fmt, ...) ((void) 0)
#define ESP_LOGCONFIG(tag, fmt, ...) ((void) 0)
#define ESP_LOGD(tag, fmt, ...) ((void) 0)
#define ESP_LOGV(tag, fmt, ...) ((void) 0)
#define ESP_LOGVV(tag, fmt, ...) ((void) 0)
#define LOG_STR_ARG(value) (value)
#define ESPHOME_LOG_FORMAT(format) format
#define esp_log_printf_(level, tag, line, format, ...) ((void) 0)

namespace esphome {
namespace hwp {

inline bool hwp_log_active(const char*, int = ESPHOME_LOG_LEVEL_VERBOSE) { return false; }

} // namespace hwp
} // namespace esphome

#else

#include "esphome/components/logger/logger.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hwp {

inline bool hwp_log_active(const char*, int min_level = ESPHOME_LOG_LEVEL_VERBOSE) {
    auto* log = logger::global_logger;
    return log != nullptr && log->get_log_level() >= min_level;
}

} // namespace hwp
} // namespace esphome

#endif
