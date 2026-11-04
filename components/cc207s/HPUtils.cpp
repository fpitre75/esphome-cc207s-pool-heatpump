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
#include "HPUtils.h"
#include "CS.h"

namespace esphome {
namespace hwp {
std::string format_bool_diff(bool current, bool reference) {
    CS cs;
    bool changed = (current != reference);
    cs.set_changed_base_color(changed);

    auto cs_inv = changed ? CS::invert : "";
    auto cs_inv_rst = changed ? CS::invert_rst : "";
    cs << cs_inv << (current ? "TRUE " : "FALSE") << cs_inv_rst;
    return cs.str();
}

template <typename T> bool memcmp_equal(const T& lhs, const T& rhs) {
    static_assert(std::is_trivially_copyable<T>::value,
        "Type must be trivially copyable for memcmp comparison.");
    return memcmp(&lhs, &rhs, sizeof(T)) == 0;
}
template <typename T> bool update_if_changed(esphome::optional<T>& original, const T& new_value) {
    bool changed = (!original.has_value() || !memcmp_equal(*original, new_value));
    if (changed) {
        original = new_value;
    }
    return changed;
}

} // namespace hwp
} // namespace esphome
