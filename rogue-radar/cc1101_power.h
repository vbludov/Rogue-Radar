#pragma once
#include <stdint.h>

// BQ25896 datasheet REG14 (identity), REG11 (VBUS_GD), REG09 (BATFET).
// Keep charging settings untouched. Delay ship mode so the I2C STOP completes
// before power disappears (TI recommends BATFET_DLY for reliable wake).
namespace cc1101Power {
enum class Result { Ready, UsbConnected, Unavailable, WriteFailed };

template <class Bus>
Result requestShutdown(Bus &bus, uint8_t &restoreValue) {
    uint8_t id, vbus, control;
    if (!bus.read(0x14, id) || (id & 0x3b) != 0x02 ||
        !bus.read(0x11, vbus)) return Result::Unavailable;
    if (vbus & 0x80) return Result::UsbConnected;
    if (!bus.read(0x09, control)) return Result::Unavailable;
    // Never replay FORCE_ICO or PUMPX command bits. Preserve other settings;
    // clear BATFET_DIS on recovery if USB arrives during the shutdown delay.
    restoreValue = control & ~uint8_t(0xa3);
    if (!bus.write(0x09, restoreValue | 0x28)) return Result::WriteFailed;
    return Result::Ready;
}
}
