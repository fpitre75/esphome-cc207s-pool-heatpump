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

#include <deque>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "SpinLock.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hwp {

extern const char* SPINLOCK_TAG; // External declaration for log tag

/**
 * @class SpinLockQueue
 * @brief A thread-safe queue using spinlock and FreeRTOS semaphores for synchronization.
 *
 * This class provides a queue that supports concurrent access, using a spinlock
 * for mutual exclusion and FreeRTOS semaphores for signaling and blocking.
 */
template <typename T> class SpinLockQueue {
  public:
    /**
     * @brief Constructs a new SpinLockQueue object with a default maximum length of 20.
     */
    SpinLockQueue() : logging_enabled(false), max_len_(20), task_handle(nullptr) {
        this->data_available = xSemaphoreCreateBinary();
    }

    /**
     * @brief Constructs a new SpinLockQueue object with a specified maximum length.
     * @param max_len Maximum number of elements in the queue.
     */
    explicit SpinLockQueue(size_t max_len)
        : logging_enabled(false), max_len_(max_len), task_handle(nullptr) {
        this->data_available = xSemaphoreCreateBinary();
    }

    /**
     * @brief Destroys the SpinLockQueue object and deletes the semaphore.
     */
    ~SpinLockQueue() {
        if (this->data_available != nullptr) {
            vSemaphoreDelete(this->data_available);
        }
    }

    /**
     * @brief Sets the maximum size of the queue.
     * @param max_len Maximum number of elements in the queue.
     */
    void set_max_size(size_t max_len) { this->max_len_ = max_len; }

    /**
     * @brief Sets the task handle to notify when new data is available in the queue.
     * @param handle Task handle to notify.
     */
    void set_task_handle(TaskHandle_t handle) {
        if (this->logging_enabled) {
            ESP_LOGV(SPINLOCK_TAG, "set_task_handle: setting task handle");
        }
        this->task_handle = handle;
    }

    /**
     * @brief Enable or disable logging for this queue instance.
     * @param enabled Set to true to enable logging, false to disable logging.
     */
    void set_logging_enabled(bool enabled) { this->logging_enabled = enabled; }

    /**
     * @brief Enqueues an element to the queue.
     *
     * If the queue exceeds the maximum length, the oldest element is removed.
     *
     * @param element The element to enqueue.
     */
    bool inline enqueue(const T& element) {
        this->spinlock.lock();
        if (this->logging_enabled) {
            ESP_LOGV(SPINLOCK_TAG, "enqueue: Attempting to enqueue element");
        }

        while (this->queue.size() >= this->max_len_ && !this->queue.empty()) {
            this->queue.pop_front();
            if (this->logging_enabled) {
                ESP_LOGW(SPINLOCK_TAG, "enqueue: Queue at max length, dropping oldest element");
            }
        }

        if (this->max_len_ == 0) {
            this->spinlock.unlock();
            ESP_LOGE(SPINLOCK_TAG, "enqueue: Queue max length is zero");
            return false;
        }

        this->queue.push_back(element);
        if (this->logging_enabled) {
            ESP_LOGI(SPINLOCK_TAG, "enqueue: Successfully enqueued element. Queue size: %d",
                this->queue.size());
        }
        this->spinlock.unlock();
        xSemaphoreGive(this->data_available);

        if (this->task_handle != nullptr) {
            xTaskNotifyGive(this->task_handle);
        }
        return true;
    }

    /**
     * @brief Checks if the queue has next element.
     * @return true If the queue is not empty.
     * @return false If the queue is empty.
     */
    bool has_next() { return !this->queue.empty(); }

    /**
     * @brief Dequeues an element from the queue.
     *
     * This function blocks until an element is available.
     *
     * @param element The element to dequeue.
     * @return true If an element was successfully dequeued.
     * @return false If the queue is empty.
     */
    bool dequeue(T* element) {
        if (this->logging_enabled) {
            ESP_LOGV(SPINLOCK_TAG, "dequeue: Attempting to take semaphore");
        }

        if (xSemaphoreTake(this->data_available, portMAX_DELAY) == pdTRUE) {
            if (this->logging_enabled) {
                ESP_LOGV(SPINLOCK_TAG, "dequeue: Semaphore taken, locking spinlock");
            }

            this->spinlock.lock();
            if (!this->queue.empty()) {
                *element = this->queue.front();
                this->queue.pop_front();

                if (this->logging_enabled) {
                    ESP_LOGI(SPINLOCK_TAG, "dequeue: Successfully dequeued element. Queue size: %d",
                        this->queue.size());
                }

                this->spinlock.unlock();

                // Check if more items are still available and adjust semaphore accordingly
                if (this->queue.size() > 0) {
                    // Give the semaphore again to signal more items are available
                    xSemaphoreGive(this->data_available);
                }

                return true;
            }

            this->spinlock.unlock();
        }

        if (this->logging_enabled) {
            ESP_LOGW(SPINLOCK_TAG,
                "dequeue: Failed to dequeue element (empty or semaphore not available)");
        }

        return false;
    }

    /**
     * @brief Attempts to dequeue an element from the queue without blocking.
     * @param element The element to dequeue.
     * @return true If an element was successfully dequeued.
     * @return false If the queue is empty.
     */
    bool try_dequeue(T* element,TickType_t xTicksToWait=0) {
        if (this->logging_enabled) {
            ESP_LOGV(SPINLOCK_TAG, "try_dequeue: Attempting to take semaphore with zero timeout");
        }

        if (xSemaphoreTake(this->data_available, xTicksToWait) == pdTRUE) {
            if (this->logging_enabled) {
                ESP_LOGV(SPINLOCK_TAG, "try_dequeue: Semaphore taken, locking spinlock");
            }

            this->spinlock.lock();
            if (!this->queue.empty()) {
                *element = this->queue.front();
                this->queue.pop_front();

                if (this->logging_enabled) {
                    ESP_LOGI(SPINLOCK_TAG,
                        "try_dequeue: Successfully dequeued element. Queue size: %d",
                        this->queue.size());
                }

                this->spinlock.unlock();
                // Check if more items are still available and adjust semaphore accordingly
                if (this->queue.size() > 0) {
                    // Give the semaphore again to signal more items are available
                    xSemaphoreGive(this->data_available);
                }

                return true;
            }

            this->spinlock.unlock();
        }

        if (this->logging_enabled) {
            ESP_LOGW(SPINLOCK_TAG,
                "try_dequeue: Failed to dequeue element (empty or semaphore not available)");
        }

        return false;
    }

  public:
    bool logging_enabled; ///< Public boolean to enable or disable logging.

  private:
    size_t max_len_;                  ///< Maximum number of elements in the queue.
    TaskHandle_t task_handle;         ///< Task handle to notify when new data is available.
    std::deque<T> queue;              ///< Internal queue storage.
    Spinlock spinlock;                ///< Spinlock for mutual exclusion.
    SemaphoreHandle_t data_available; ///< Semaphore to signal availability of data.
};

} // namespace hwp
} // namespace esphome
