#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rogue_radar {

// Pure timing state for a user-started scan session. Time passed while a
// detail page owns the radio is excluded, so returning to the scan preserves
// the remaining session duration. Unsigned subtraction keeps the model valid
// across the uint32_t millisecond counter wrap.
class ScanSessionModel {
 public:
  void configure(uint32_t durationMs, bool continuous) {
    durationMs_ = durationMs;
    continuous_ = continuous;
  }

  void start(uint32_t now) {
    accumulatedMs_ = 0;
    segmentStartMs_ = now;
    running_ = true;
    suspended_ = false;
  }

  void stop() {
    accumulatedMs_ = 0;
    segmentStartMs_ = 0;
    running_ = false;
    suspended_ = false;
  }

  void suspend(uint32_t now) {
    if (!running_ || suspended_) return;
    accumulatedMs_ += now - segmentStartMs_;
    suspended_ = true;
  }

  void resume(uint32_t now) {
    if (!running_ || !suspended_) return;
    segmentStartMs_ = now;
    suspended_ = false;
  }

  uint32_t elapsed(uint32_t now) const {
    if (!running_) return 0;
    return accumulatedMs_ + (suspended_ ? 0U : now - segmentStartMs_);
  }

  uint32_t remaining(uint32_t now) const {
    const uint32_t used = elapsed(now);
    return used >= durationMs_ ? 0U : durationMs_ - used;
  }

  bool expired(uint32_t now) const {
    return running_ && !continuous_ && elapsed(now) >= durationMs_;
  }

  bool running() const { return running_; }
  bool suspended() const { return suspended_; }
  bool continuous() const { return continuous_; }
  uint32_t duration() const { return durationMs_; }

 private:
  uint32_t durationMs_ = 0;
  uint32_t accumulatedMs_ = 0;
  uint32_t segmentStartMs_ = 0;
  bool continuous_ = false;
  bool running_ = false;
  bool suspended_ = false;
};

// Fixed-size alert de-duplication cache. A key alerts when first observed, or
// after it has been absent long enough and its alert cooldown has also elapsed.
// When full, the least-recently-seen entry is replaced; a sufficiently old
// evicted key can therefore alert again if it returns.
template <size_t Capacity>
class ScanAlertCache {
  static_assert(Capacity > 0, "ScanAlertCache capacity must be positive");

 public:
  ScanAlertCache() { reset(); }

  void reset() {
    for (size_t i = 0; i < Capacity; ++i) entries_[i].valid = false;
  }

  bool observe(uint64_t key, uint32_t now,
               uint32_t absenceMs = 30000,
               uint32_t cooldownMs = 30000) {
    size_t freeIndex = Capacity;
    size_t oldestIndex = 0;
    uint32_t oldestAge = 0;

    for (size_t i = 0; i < Capacity; ++i) {
      Entry &entry = entries_[i];
      if (entry.valid && entry.key == key) {
        const bool alert = now - entry.lastSeenMs >= absenceMs &&
                           now - entry.lastAlertMs >= cooldownMs;
        entry.lastSeenMs = now;
        if (alert) entry.lastAlertMs = now;
        return alert;
      }
      if (!entry.valid) {
        if (freeIndex == Capacity) freeIndex = i;
      } else {
        const uint32_t age = now - entry.lastSeenMs;
        if (age > oldestAge) {
          oldestAge = age;
          oldestIndex = i;
        }
      }
    }

    const size_t index = freeIndex != Capacity ? freeIndex : oldestIndex;
    entries_[index] = Entry{key, now, now, true};
    return true;
  }

 private:
  struct Entry {
    uint64_t key;
    uint32_t lastSeenMs;
    uint32_t lastAlertMs;
    bool valid;
  };

  Entry entries_[Capacity];
};

}  // namespace rogue_radar
