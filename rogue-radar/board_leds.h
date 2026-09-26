#pragma once

#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)

#include <Arduino.h>
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef NUM_LEDS
#error "NUM_LEDS must be defined before including board_leds.h"
#endif

#ifndef WS2812_PIN
#error "WS2812_PIN must be defined before including board_leds.h"
#endif

struct rgb_color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

class BoardLedStrip {
public:
    BoardLedStrip()
        : mutex_(xSemaphoreCreateMutexStatic(&mutexStorage_))
    {
    }

    void begin()
    {
        lock();
        beginIfNeeded();
        unlock();
    }

    void write(const rgb_color *colors, uint16_t count, uint8_t brightness = 31)
    {
        lock();
        beginIfNeeded();

        const uint16_t copyCount = count < NUM_LEDS ? count : NUM_LEDS;
        for (uint16_t i = 0; i < copyCount; ++i) {
            pixels_[i] = CRGB(colors[i].red, colors[i].green, colors[i].blue);
        }
        for (uint16_t i = copyCount; i < NUM_LEDS; ++i) {
            pixels_[i] = CRGB::Black;
        }

        FastLED.show(mapBrightness(brightness));
        unlock();
    }

private:
    CRGB pixels_[NUM_LEDS] = {};
    bool initialized_ = false;
    StaticSemaphore_t mutexStorage_ = {};
    SemaphoreHandle_t mutex_ = nullptr;

    void beginIfNeeded()
    {
        if (initialized_) {
            return;
        }

        FastLED.addLeds<WS2812, WS2812_PIN, GRB>(pixels_, NUM_LEDS);
        initialized_ = true;
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

    static uint8_t mapBrightness(uint8_t brightness)
    {
        if (brightness > 31) {
            brightness = 31;
        }
        return static_cast<uint8_t>((static_cast<uint16_t>(brightness) * 255U + 15U) / 31U);
    }
};

#else

#include <APA102.h>

using BoardLedStrip = APA102<APA102_DI, APA102_CLK>;

#endif
