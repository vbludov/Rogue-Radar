#include <cstdlib>
#include <iostream>
#include <string>

#include "../rogue-radar/scan_session_model.h"

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void testSessionTimingAndMode() {
  rogue_radar::ScanSessionModel model;
  model.configure(30000, false);
  check(model.duration() == 30000 && !model.continuous(),
        "configuration exposes total duration and one-shot mode");
  check(!model.running(), "configured session is not started implicitly");

  model.start(1000);
  check(model.running() && !model.suspended(), "start activates session");
  check(model.elapsed(1000) == 0 && model.remaining(1000) == 30000,
        "start establishes a fresh total-duration window");
  check(!model.expired(30999), "session remains live one millisecond before deadline");
  check(model.remaining(30999) == 1, "remaining time reaches one before deadline");
  check(model.expired(31000) && model.remaining(31000) == 0,
        "session expires exactly at total-duration deadline");
  check(model.expired(40000), "session remains expired after deadline");

  model.configure(30000, true);
  model.start(1000);
  check(!model.expired(1000000) && model.remaining(1000000) == 0,
        "continuous mode ignores the duration deadline");

  model.stop();
  check(!model.running() && !model.suspended(), "stop clears active state");
  check(model.elapsed(50000) == 0, "stopped session reports no elapsed time");
}

void testSuspendResumeAndRestart() {
  rogue_radar::ScanSessionModel model;
  model.configure(1000, false);
  model.start(100);
  model.suspend(500);
  check(model.suspended() && model.elapsed(9000) == 400,
        "suspend freezes elapsed time while detail page owns scanner");
  check(model.remaining(9000) == 600 && !model.expired(9000),
        "suspended wall time does not consume session duration");
  model.suspend(10000);
  check(model.elapsed(10000) == 400, "repeated suspend is idempotent");

  model.resume(20000);
  model.resume(21000);
  check(!model.suspended() && model.elapsed(20599) == 999,
        "resume preserves accumulated time and is idempotent");
  check(model.expired(20600), "resumed session expires after remaining active time");

  model.start(30000);
  check(model.elapsed(30000) == 0 && model.remaining(30000) == 1000,
        "new user Start resets prior timing state");
}

void testSessionClockWrap() {
  rogue_radar::ScanSessionModel model;
  model.configure(48, false);
  model.start(UINT32_MAX - 31U);
  check(model.elapsed(0) == 32 && model.remaining(0) == 16,
        "elapsed and remaining cross uint32 wrap");
  model.suspend(7);
  check(model.elapsed(123456) == 39, "suspend accumulation crosses wrap");
  model.resume(UINT32_MAX - 3U);
  check(!model.expired(4), "resumed wrapped session remains live before deadline");
  check(model.expired(5), "resumed wrapped session expires exactly on deadline");
}

void testAlertDedupAndReacquisition() {
  rogue_radar::ScanAlertCache<4> cache;
  check(cache.observe(0xA, 100), "first observation alerts");
  check(!cache.observe(0xA, 200), "continuous duplicate is suppressed");
  check(!cache.observe(0xA, 30199), "absence threshold uses latest observation");
  check(!cache.observe(0xA, 60198), "reappearance one millisecond early is suppressed");
  check(cache.observe(0xA, 90198),
        "previously absent key alerts when absence and cooldown both pass");
  check(!cache.observe(0xA, 120197), "post-alert cooldown boundary is enforced");
  check(cache.observe(0xA, 150197), "exact absence/cooldown boundary re-alerts");

  cache.reset();
  check(cache.observe(0xA, 150198), "new user Start resets alert history");
}

void testAlertCacheCapacityAndWrap() {
  rogue_radar::ScanAlertCache<2> cache;
  check(cache.observe(1, 10), "first key enters bounded cache");
  check(cache.observe(2, 20), "second key enters bounded cache");
  check(!cache.observe(1, 25), "duplicate refreshes last-seen recency");
  check(cache.observe(3, 30), "new key alerts and evicts least-recently-seen key");
  check(!cache.observe(1, 31), "more-recent key survives deterministic eviction");
  check(cache.observe(2, 32), "evicted old key can alert again when it returns");

  cache.reset();
  check(cache.observe(9, UINT32_MAX - 20U, 30, 40), "wrapped key first alerts");
  check(!cache.observe(9, 8, 30, 40), "wrapped absence below threshold is suppressed");
  check(cache.observe(9, 48, 30, 40),
        "absence and cooldown comparisons work across uint32 wrap");
}
}  // namespace

int main() {
  testSessionTimingAndMode();
  testSuspendResumeAndRestart();
  testSessionClockWrap();
  testAlertDedupAndReacquisition();
  testAlertCacheCapacityAndWrap();
  if (failures != 0) {
    std::cerr << failures << " of " << checks << " scan-session checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "PASS: " << checks << " ScanSessionModel checks\n";
  return EXIT_SUCCESS;
}
