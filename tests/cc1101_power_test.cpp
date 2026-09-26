#include "../rogue-radar/cc1101_power.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

struct Bus {
    uint8_t regs[0x15] = {};
    int failRead = -1, writes = 0;
    bool failWrite = false;
    Bus() { regs[0x14] = 2; regs[0x09] = 0x54; }
    bool read(uint8_t r, uint8_t &v) {
        if (r == failRead) return false;
        v = regs[r]; return true;
    }
    bool write(uint8_t r, uint8_t v) {
        assert(r == 9); ++writes;
        if (failWrite) return false;
        regs[r] = v; return true;
    }
};

int main() {
    using cc1101Power::Result;
    uint8_t restore = 0;
    for (int r : {0x14, 0x11, 0x09}) {
        Bus bus; bus.failRead = r;
        assert(cc1101Power::requestShutdown(bus, restore) == Result::Unavailable);
        assert(bus.writes == 0);
    }
    for (int id : {0, 0xff, 0x1a}) {
        Bus bus; bus.regs[0x14] = id;
        assert(cc1101Power::requestShutdown(bus, restore) == Result::Unavailable);
        assert(bus.writes == 0);
    }
    Bus usb; usb.regs[0x11] = 0x80;
    assert(cc1101Power::requestShutdown(usb, restore) == Result::UsbConnected);
    assert(usb.writes == 0);
    Bus broken; broken.failWrite = true;
    assert(cc1101Power::requestShutdown(broken, restore) == Result::WriteFailed);
    // Exhaustively preserve non-command controls, enable delayed shutdown,
    // and ensure recovery re-enables the battery path without replaying commands.
    for (int control = 0; control < 256; ++control) {
        Bus bus; bus.regs[9] = control;
        assert(cc1101Power::requestShutdown(bus, restore) == Result::Ready);
        assert(bus.writes == 1);
        assert((bus.regs[9] & 0x28) == 0x28);
        assert((bus.regs[9] & 0x54) == (control & 0x54));
        assert((bus.regs[9] & 0x83) == 0);
        assert((restore & 0xa3) == 0);
        assert((restore & 0x5c) == (control & 0x5c));
    }
    std::puts("CC1101 power fault-injection and register-preservation tests passed");
}
