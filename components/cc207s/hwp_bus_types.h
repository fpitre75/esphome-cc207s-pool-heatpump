/**
 * @file hwp_bus_types.h
 * @brief Shared lightweight bus type declarations.
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

namespace esphome {
namespace hwp {

/**
 * @enum bus_mode_t
 * @brief Represents the bus mode (transmit, receive, or error).
 */
typedef enum { BUSMODE_TX, BUSMODE_RX, BUSMODE_ERROR } bus_mode_t;

} // namespace hwp
} // namespace esphome
