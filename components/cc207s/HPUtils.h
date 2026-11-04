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
// #include "esphome/core/hal.h"
#include "CS.h"
#include "hwp_climate_adapter.h"
#include "hwp_time_adapter.h"
#include "protocol_core.h"
#include <cstdint>
#include <type_traits>

// Utility to perform a generic memory comparison using std::memcmp.
#ifndef HWP_NATIVE_TEST
#include "esphome/components/watchdog/watchdog.h"
#endif
namespace esphome {
namespace hwp {

/**
 * @brief Clears the bit at a specified position in a byte.
 *
 * @param byte Pointer to the byte in which the bit is to be cleared.
 * @param position The position of the bit to clear (0-based index).
 */
inline void clear_bit(uint8_t* byte, uint8_t position) {
    if (byte == nullptr) return;
    *byte &= ~(1 << position);
}

/**
 * @brief Sets the bit at a specified position in a byte.
 *
 * @param byte Pointer to the byte in which the bit is to be set.
 * @param position The position of the bit to set (0-based index).
 */
inline void set_bit(uint8_t* byte, uint8_t position) {
    if (byte == nullptr) return;
    *byte |= (1 << position);
}
/**
 * @brief Gets the bit at a specified position in a byte.
 *
 * @param byte Byte in which the bit is to be read
 * @param position The position of the bit to get (0-based index).
 */
inline bool get_bit(uint8_t byte, uint8_t position) { return byte & (1 << position); }

template <typename T> bool memcmp_equal(const T& lhs, const T& rhs);
template <typename T> bool update_if_changed(esphome::optional<T>& original, const T& new_value);
std::string format_bool_diff(bool current, bool reference);
template <typename T>
std::string format_diff(const T& current, const T& reference, const char* sep = " ") {
    CS cs;
    bool changed = (current != reference);
    cs.set_changed_base_color(changed);

    auto cs_inv = changed ? CS::invert : "";
    auto cs_inv_rst = changed ? CS::invert_rst : "";
    cs << cs_inv << current << cs_inv_rst << sep;
    return cs.str();
}

/**
 * @brief Inverts a byte array (i.e., sets each bit to its opposite state).
 *
 * @param buffer The array to be inverted.
 * @param length The number of bytes in the array to be inverted.
 */
template <size_t N> void inverse(uint8_t (&buffer)[N], const size_t length) {
    protocol::invert_bytes(buffer, N, length);
}


} // namespace hwp
} // namespace esphome
