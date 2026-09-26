#pragma once

#include <Arduino.h>

#include "config.h"

// The CC1101 board routes LCD_BL_PIN to an AW9364 one-wire backlight
// controller. Its rising-edge count selects one of 16 constant-current steps;
// PWM would continuously advance that counter instead of setting a duty cycle.
//
// AW9364 timing (Awinic datasheet):
//   enable/ready high  > 20 us
//   dimming pulse high > 0.5 us
//   dimming pulse low  0.5..500 us
//   shutdown/reset low > 2.5 ms

#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)
namespace rogue_radar_backlight {

static portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;
static int16_t lastRequestedLevel = -1;
static uint8_t lastProgrammedStep = 0;

static inline uint8_t levelToStep(uint8_t level)
{
    // Step 1 is full current and step 16 is minimum current.
    const uint8_t currentIndex = static_cast<uint8_t>(
        (static_cast<uint16_t>(level) * 15U + 127U) / 255U);
    return static_cast<uint8_t>(16U - currentIndex);
}

}  // namespace rogue_radar_backlight
#endif

static inline void boardBacklightBegin()
{
#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, LOW);
    rogue_radar_backlight::lastRequestedLevel = 0;
    rogue_radar_backlight::lastProgrammedStep = 0;
#else
    ledcSetup(LCD_BL_CH, LCD_BL_FREQ, LCD_BL_RES);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CH);
#endif
}

static inline void boardBacklightWrite(uint8_t level)
{
#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)
    using namespace rogue_radar_backlight;

    if (lastRequestedLevel == level) {
        return;
    }

    if (level == 0) {
        digitalWrite(LCD_BL_PIN, LOW);
        // Keep interrupts enabled during the comparatively long reset interval.
        delayMicroseconds(3000);
        lastRequestedLevel = 0;
        lastProgrammedStep = 0;
        return;
    }

    const uint8_t targetStep = levelToStep(level);
    if (lastProgrammedStep == targetStep) {
        lastRequestedLevel = level;
        return;
    }

    // Reset to a known state. This exceeds the 2.5 ms maximum shutdown delay,
    // and deliberately stays outside the critical section.
    digitalWrite(LCD_BL_PIN, LOW);
    delayMicroseconds(3000);

    // Interrupt latency cannot stretch a dimming LOW pulse beyond the AW9364
    // 500 us limit. The critical section is at most about 85 us (step 16).
    portENTER_CRITICAL(&pulseMux);
    digitalWrite(LCD_BL_PIN, HIGH);
    delayMicroseconds(25);
    for (uint8_t edge = 1; edge < targetStep; ++edge) {
        digitalWrite(LCD_BL_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(LCD_BL_PIN, HIGH);
        delayMicroseconds(2);
    }
    portEXIT_CRITICAL(&pulseMux);

    lastRequestedLevel = level;
    lastProgrammedStep = targetStep;
#else
    ledcWrite(LCD_BL_CH, level);
#endif
}
