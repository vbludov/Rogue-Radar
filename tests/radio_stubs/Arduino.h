#pragma once
#include <cstdint>
#include <string>

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

class String {
public:
    String() = default;
    String(const char *value) : value_(value ? value : "") {}
    String(const std::string &value) : value_(value) {}
    const char *c_str() const { return value_.c_str(); }
    size_t length() const { return value_.length(); }
private:
    std::string value_;
};
