#pragma once
#include <cstdint>

using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

extern uint32_t fakeMillis;
void fakeArduinoDelayHook(uint32_t ms);
inline uint32_t millis() { return fakeMillis; }
inline void delay(uint32_t ms) {
    fakeMillis += ms;
    fakeArduinoDelayHook(ms);
}