#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "board_leds.h"

// Serializes all LED output and adds an optional asynchronous visual alert.
// Normal writes made during an alert are cached and restored when it finishes.
class AlertLedStrip {
public:
    AlertLedStrip()
        : mutex_(xSemaphoreCreateMutexStatic(&mutexStorage_))
    {
    }

    void begin()
    {
#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)
        lock();
        strip_.begin();
        unlock();
#endif
    }

    void write(const rgb_color *colors, uint16_t count, uint8_t brightness = 31)
    {
        lock();
        copyFrame(baseFrame_, colors, count);
        baseBrightness_ = clampBrightness(brightness);
        if (!alertActive_) {
            writeUnderlyingLocked(baseFrame_, baseBrightness_);
        }
        unlock();
    }

    bool setAlertEnabled(bool enabled)
    {
        bool createFailed = false;

        lock();
        if (enabled && task_ == nullptr && !taskCreationFailed_) {
            TaskHandle_t createdTask = nullptr;
            const BaseType_t result = xTaskCreate(
                taskEntry,
                "rr-led-alert",
                3072,
                this,
                1,
                &createdTask);
            if (result == pdPASS) {
                task_ = createdTask;
            } else {
                taskCreationFailed_ = true;
                createFailed = true;
                enabled = false;
            }
        }

        alertEnabled_ = enabled && task_ != nullptr;
        if (!alertEnabled_) {
            alertPending_ = false;
            alertActive_ = false;
            overlayValid_ = false;
            writeUnderlyingLocked(baseFrame_, baseBrightness_);
        }
        const bool actualEnabled = alertEnabled_;
        unlock();

        if (createFailed) {
            Serial.println(F("[LED] Unable to create visual alert task; alerts disabled."));
        }
        return actualEnabled;
    }

    void triggerAlert()
    {
        TaskHandle_t taskToNotify = nullptr;

        lock();
        // One pending notification is enough. Triggers received after the
        // animation starts are represented by the animation already running.
        if (alertEnabled_ && task_ != nullptr &&
            !alertActive_ && !alertPending_) {
            alertPending_ = true;
            taskToNotify = task_;
        }
        unlock();

        if (taskToNotify != nullptr) {
            xTaskNotifyGive(taskToNotify);
        }
    }

    void setBrightnessLimit(uint8_t limit)
    {
        lock();
        const uint8_t nextLimit = clampBrightness(limit);
        if (nextLimit == brightnessLimit_) {
            unlock();
            return;
        }
        brightnessLimit_ = nextLimit;
        if (alertActive_ && overlayValid_) {
            writeUnderlyingLocked(overlayFrame_, LED_BRIGHTNESS);
        } else {
            writeUnderlyingLocked(baseFrame_, baseBrightness_);
        }
        unlock();
    }

private:
    BoardLedStrip strip_;
    rgb_color baseFrame_[NUM_LEDS] = {};
    rgb_color overlayFrame_[NUM_LEDS] = {};
    uint8_t baseBrightness_ = 0;
    uint8_t brightnessLimit_ = 31;
    bool alertEnabled_ = false;
    bool alertPending_ = false;
    bool alertActive_ = false;
    bool overlayValid_ = false;
    bool taskCreationFailed_ = false;
    StaticSemaphore_t mutexStorage_ = {};
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;

    static void taskEntry(void *context)
    {
        static_cast<AlertLedStrip *>(context)->taskLoop();
    }

    void taskLoop()
    {
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            lock();
            if (!alertEnabled_ || !alertPending_) {
                alertPending_ = false;
                unlock();
                continue;
            }
            alertPending_ = false;
            alertActive_ = true;
            fillRed(overlayFrame_, 255);
            overlayValid_ = true;
            writeUnderlyingLocked(overlayFrame_, LED_BRIGHTNESS);
            unlock();

            vTaskDelay(pdMS_TO_TICKS(240));

            bool keepRunning = true;
            for (uint16_t step = 0;
                 keepRunning && step < static_cast<uint16_t>(NUM_LEDS * 2U);
                 ++step) {
                lock();
                keepRunning = alertEnabled_ && alertActive_;
                if (keepRunning) {
                    makeChaseFrame(step);
                    overlayValid_ = true;
                    writeUnderlyingLocked(overlayFrame_, LED_BRIGHTNESS);
                }
                unlock();

                if (keepRunning) {
                    vTaskDelay(pdMS_TO_TICKS(80));
                }
            }

            lock();
            alertActive_ = false;
            overlayValid_ = false;
            writeUnderlyingLocked(baseFrame_, baseBrightness_);
            unlock();
        }
    }

    void makeChaseFrame(uint16_t step)
    {
        clearFrame(overlayFrame_);
        const uint16_t head = step % NUM_LEDS;
        const uint16_t tail1 = (head + NUM_LEDS - 1U) % NUM_LEDS;
        const uint16_t tail2 = (head + NUM_LEDS - 2U) % NUM_LEDS;

        overlayFrame_[tail2] = redPixel(31);  // 12% tail
        overlayFrame_[tail1] = redPixel(89);  // 35% tail
        overlayFrame_[head] = redPixel(255);
    }

    void writeUnderlyingLocked(rgb_color *frame, uint8_t brightness)
    {
        const uint8_t effectiveBrightness =
            brightness < brightnessLimit_ ? brightness : brightnessLimit_;
        strip_.write(frame, NUM_LEDS, effectiveBrightness);
    }

    static void copyFrame(rgb_color *destination,
                          const rgb_color *source,
                          uint16_t count)
    {
        const uint16_t copyCount = count < NUM_LEDS ? count : NUM_LEDS;
        for (uint16_t i = 0; i < copyCount; ++i) {
            destination[i] = source != nullptr ? source[i] : redPixel(0);
        }
        for (uint16_t i = copyCount; i < NUM_LEDS; ++i) {
            destination[i] = redPixel(0);
        }
    }

    static void clearFrame(rgb_color *frame)
    {
        fillRed(frame, 0);
    }

    static void fillRed(rgb_color *frame, uint8_t red)
    {
        for (uint16_t i = 0; i < NUM_LEDS; ++i) {
            frame[i] = redPixel(red);
        }
    }

    static rgb_color redPixel(uint8_t red)
    {
        const rgb_color pixel = {red, 0, 0};
        return pixel;
    }

    static uint8_t clampBrightness(uint8_t brightness)
    {
        return brightness > 31 ? 31 : brightness;
    }

    void lock()
    {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }

    void unlock()
    {
        if (mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }
    }
};
